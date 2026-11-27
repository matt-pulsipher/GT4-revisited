#include "gt4r/audio/app_sound.h"

#include "audio/audio_engine.h"
#include "audio/music_file.h"
#include "base/config.h"
#include "base/log.h"
#include "vfs/vfs.h"

#include <cstdio>

namespace gt4r::audio {

namespace {

bool ends_with(const std::string& s, const char* tail) {
    const size_t n = std::char_traits<char>::length(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

} // namespace

StreamPlayer stream_player(gt3r::AudioEngine& audio, gt3r::Vfs& vfs, StreamVoices* voices, std::shared_ptr<bool> recording, bool menus) {
    if (!recording) recording = std::make_shared<bool>(false);
    // (the menus' streams leave a recording of their music on the engine's stream)
    const auto engine_is_ours = [recording, menus] { return !menus || !*recording; };
    StreamPlayer p;
    p.play = [&audio, &vfs, voices, recording, engine_is_ours](const std::string& path, bool repeat, float level) {
        if (voices) {
            voices->stop();
            if (voices->play(path, repeat, level)) {
                if (engine_is_ours()) audio.stop_music(), *recording = false;
                if (sound_log()) LOG_INFO("Sound: stream %s%s at %.4f on two voices", path.c_str(), repeat ? ", repeating" : "", double(level));
                return true;
            }
        }
        const bool ok = audio.play_music(vfs, path, repeat, stream_request(level));
        *recording = false;
        if (sound_log()) LOG_INFO("Sound: stream %s%s at %.4f%s", path.c_str(), repeat ? ", repeating" : "", double(level), ok ? "" : ": not played");
        return ok;
    };
    p.stop = [&audio, voices, recording, engine_is_ours] {
        if (sound_log() && (audio.music_playing() || (voices && voices->playing()))) LOG_INFO("Sound: stream stopped");
        if (voices) voices->stop();
        if (engine_is_ours()) audio.stop_music(), *recording = false;
    };
    p.pause = [&audio, voices, engine_is_ours](bool paused) {
        if (sound_log()) LOG_INFO("Sound: stream %s", paused ? "paused" : "on");
        if (voices) voices->pause(paused);
        if (!engine_is_ours()) return;
        if (paused)
            audio.pause_music();
        else
            audio.resume_music();
    };
    p.playing = [&audio, voices, engine_is_ours] { return (voices && voices->playing()) || (engine_is_ours() && audio.music_playing()); };
    p.play_file = [&audio, voices, recording](const std::string& path, float level, float trim) {
        if (voices) voices->stop();
        *recording = false;
        const bool ok = audio.play_music_file(path, false, stream_request(level), true, trim);
        if (sound_log()) LOG_INFO("Sound: song %s at %.4f, gain %.3f%s", path.c_str(), double(level), double(trim), ok ? "" : ": not played");
        return ok;
    };
    p.set_trim = [&audio](float trim) { audio.set_stream_trim(trim); };
    return p;
}

MusicRecordings music_recordings(gt3r::AudioEngine& audio, gt3r::Vfs& vfs, std::string region, std::shared_ptr<bool> recording) {
    MusicRecordings r;
    r.find = [&vfs, region](const std::string& piece) -> std::string {
        if (vfs.mods().empty()) return {};
        const std::string want = gt3r::to_lower(piece);
        for (const std::string dir : {"music/" + region + "/", std::string("music/")}) {
            for (const std::string& f : vfs.list_dir(dir)) {
                if (f.empty() || f.back() == '/' || gt3r::music_file_type(f) == gt3r::MusicFileType::None) continue;
                if (gt3r::to_lower(f.substr(0, f.rfind('.'))) != want) continue;
                // (a mod's file: its place on the host, which the engine's file player reads)
                const std::string path = dir + f, from = vfs.source_of(path);
                for (const gt3r::ModInfo& m : vfs.mods())
                    if (m.id == from && m.active()) return m.root + "/files/" + path;
            }
        }
        return {};
    };
    r.play = [&audio, recording](const std::string& file, float level) {
        const bool ok = audio.play_music_file(file, true, stream_request(level));
        *recording = ok;
        LOG_INFO("Sound: the menus' music from %s at %.4f%s", file.c_str(), double(level), ok ? "" : ": not played");
        return ok;
    };
    r.stop = [&audio, recording] {
        if (*recording) audio.stop_music();
        *recording = false;
    };
    r.playing = [&audio, recording] { return *recording && audio.music_playing(); };
    r.set_level = [&audio, recording](float level) {
        if (*recording) audio.set_stream_level(stream_request(level));
    };
    return r;
}

AppSound::AppSound(gt3r::AudioEngine& audio, gt3r::Vfs& vfs)
    : audio_(audio), vfs_(vfs), driver_(audio.voices()), stream_voices_(audio.voices(), vfs), library_(std::make_shared<SoundLibrary>(vfs)),
      menu_(library_, driver_, stream_player(audio, vfs, &stream_voices_, recording_, true)) {
    menu_.set_recordings(music_recordings(audio, vfs, library_->region(), recording_));
    // The game's output level on the app's mixer while a disc is open (the shared engine's own
    // screens keep its half level).
    with_output_level(audio_.voices());
    with_levels_at_once(audio_.voices());
    // The game's surround cue while a disc is open; the shared engine's screens get their own
    // setting back.
    surround_cue_before_ = audio_.voices().surround_cue();
    with_surround_cue(audio_.voices());
    // The mixer has one tick hook: the shared engine's sequencer keeps its turn in it.
    audio_.voices().set_tick_hook(
        [this] {
            audio_.sequencer().update(audio_.voices());
            driver_.update();
        },
        SoundDriver::kSamplesPerUpdate);
}

AppSound::~AppSound() {
    stream_voices_.stop();
    gt3r::AudioEngine& audio = audio_;
    audio.voices().set_master_volume(gt3r::VoiceMixer::kOutputTrim);
    audio.voices().set_surround_cue(surround_cue_before_);
    with_levels_at_once(audio.voices(), false);
    audio.voices().set_tick_hook([&audio] { audio.sequencer().update(audio.voices()); }, audio.sequencer().samples_per_update());
    driver_.stop_all();
}

void AppSound::stop() {
    stream_voices_.stop();
    driver_.stop_all();
    preview_note_ = 0;
}

std::string AppSound::preview(const std::string& path) {
    const bool sqt = ends_with(path, ".sqt"), ins = ends_with(path, ".ins");
    if (!sqt && !ins) return {};
    if (path != preview_path_) {
        preview_path_ = path;
        preview_next_ = 0;
    }
    const std::string stem = path.substr(0, path.size() - 4);
    char line[160];
    // A sequence bank and its instruments.
    if (sqt || vfs_.exists(stem + ".sqt")) {
        const MusicBank bank = library_->music_bank(stem);
        if (!bank || bank.sequences->sequences.empty()) return ", could not read this sequence bank";
        const int count = int(bank.sequences->sequences.size()), n = preview_next_++ % count;
        const sound::SqtSummary s = sound::summarize_sequence(*bank.sequences, size_t(n));
        audio_.stop_music();
        stream_voices_.stop();
        driver_.stop_all();
        driver_.set_volume(SoundDriver::Kind::Music, library_->levels().level(1, sound::VolumeTable::Sequence));
        driver_.play_music(bank.bank, bank.sequences, n);
        std::snprintf(line, sizeof line, ", sequence %d of %d: %u notes, loop of %.1f s after %.1f s, playing", n + 1, count, s.notes, s.loop_seconds,
                      s.intro_seconds);
        return line;
    }
    // An effect bank: its effect sequences, then its programs' notes.
    std::shared_ptr<const sound::InstBank> bank;
    try {
        if (const auto d = vfs_.read(path)) bank = std::make_shared<const sound::InstBank>(sound::InstBank::parse(*d));
    } catch (const std::exception&) {
    }
    if (!bank) return ", could not read this bank";
    std::vector<std::pair<int, int>> sequences;
    for (size_t s = 0; s < bank->effect_sequences.size(); ++s)
        for (size_t e = 0; e < bank->effect_sequences[s].size(); ++e) sequences.emplace_back(int(s), int(e));
    std::vector<int> programs;
    for (size_t p = 0; p < bank->programs.size(); ++p)
        if (bank->programs[p] && !bank->programs[p]->tones.empty() && !bank->programs[p]->tones[0].empty()) programs.push_back(int(p));
    const int count = int(sequences.size() + programs.size());
    if (!count) return ", a bank without sounds";
    const int n = preview_next_++ % count;
    if (preview_note_) driver_.note_off(preview_note_);
    preview_note_ = 0;
    SoundParams p;
    p.left = p.right = library_->levels().level(1, sound::VolumeTable::Menu);
    if (size_t(n) < sequences.size()) {
        driver_.play_effect(bank, sequences[size_t(n)].first, sequences[size_t(n)].second, p);
        std::snprintf(line, sizeof line, ", effect sequence %d of %zu (%zu programs), playing", n + 1, sequences.size(), programs.size());
    } else {
        const int program = programs[size_t(n) - sequences.size()];
        preview_note_ = driver_.note_on(bank, program, p);
        std::snprintf(line, sizeof line, ", program %d (%zu programs, %zu effect sequences), playing", program, programs.size(), sequences.size());
    }
    return line;
}

} // namespace gt4r::audio
