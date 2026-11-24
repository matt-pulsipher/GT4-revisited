// gt4tool sound: the sound files of a disc.
//
//   gt4tool sound <disc> survey                     read every sound file and count what is in them
//   gt4tool sound <disc> bank <path.ins>            a bank's programs, tones and effect sequences
//   gt4tool sound <disc> effects                    the effect index: name, bank, what it plays
//   gt4tool sound <disc> music                      the music indexes: groups, pieces, lengths
//   gt4tool sound <disc> pages                      the music and the navigation sound each menu page names
//   gt4tool sound <disc> seq <bank> [n]             a sequence's events (bank: "music/us/arcade")
//   gt4tool sound <disc> seq-wav <bank> <n> <out.wav> [loops] [volume]   a sequence rendered to a WAV file
//                                                   (volume: the music's master, 0..1; 1 by default)
//   gt4tool sound <disc> se-wav <name> <out.wav>    an effect of the index rendered to a WAV file
//   gt4tool sound <disc> car <label>                the sound files a car uses
//   gt4tool sound <disc> car-wav <label> <out.wav>  a car's engine swept through its revs
//   gt4tool sound <disc> race-tables                the program's tables that mix a race's sound
//   gt4tool sound <disc> pass-wav <label> <out.wav> [km/h]   a car passing 10 m from a still camera,
//                                                   mixed by the program's tables (5 s)
//   gt4tool sound <disc> drive-wav <label> <out.wav> <rpm>[:<rpm>] <seconds> [incar|chase|other] [rpm per m/s]
//                                                   the car driven flat out from the first revs to the
//                                                   second (its speed the revs over the last number,
//                                                   200 by default), heard from that view of it, mixed by
//                                                   the program's tables at the new game's levels
//   gt4tool sound <disc> stream-wav <path.ads> <out.wav> [seconds] [level]   a streamed piece decoded
//                                                   (from the start, at its own rate, x level)
//
// The WAV files are for measuring (length, level, pitch); never commit them.

#include "base/file.h"
#include "formats/ads.h"
#include "formats/sound/crss.h"
#include "formats/sound/engn.h"
#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/menu_sound.h"
#include "gt4r/audio/race_tables.h"
#include "gt4r/audio/render.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/sound/inst_bank.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/formats/sound/sqt.h"
#include "gt4r/menu/layout.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gt4r {

namespace {

using Args = std::vector<std::string>;

bool ends_with(const std::string& s, const char* tail) {
    const size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}
bool starts_with(const std::string& s, const char* head) { return s.rfind(head, 0) == 0; }

Bytes read(Disc& disc, const std::string& path) {
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) throw FormatError("no such file: " + path);
    return disc.volume().read(*e);
}

bool write_wav(const std::string& path, const std::vector<s16>& pcm) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (!gt3r::write_host_file(path, audio::wav_file(pcm))) {
        std::fprintf(stderr, "error: cannot write %s\n", path.c_str());
        return false;
    }
    const audio::Level l = audio::measure(pcm);
    std::printf("%s: %.2f s, peak %.1f dBFS, rms %.1f dBFS, %zu clipped, sounding until %.2f s\n", path.c_str(), l.seconds,
                l.peak ? 20 * std::log10(l.peak / 32768.0) : -99.0, l.rms > 0 ? 20 * std::log10(l.rms) : -99.0, l.clipped, l.sounding_until);
    return true;
}

int survey(Disc& disc) {
    struct Count {
        size_t files = 0, failed = 0;
        std::string first_error;
    };
    std::map<std::string, Count> counts;
    size_t programs = 0, tones = 0, effect_sequences = 0, sequences = 0, notes = 0, layers = 0, records = 0, markers = 0;
    std::map<int, size_t> controllers;
    for (const ImageFile& f : disc.files()) {
        const bool sound_folder = starts_with(f.path, "sound/") || starts_with(f.path, "music/") || starts_with(f.path, "bgm/") ||
                                  starts_with(f.path, "narration/") || starts_with(f.path, "crs/");
        if (!sound_folder || ends_with(f.path, ".ads") || f.size < 4) continue;
        if (starts_with(f.path, "crs/")) continue; // (course ambience sits inside the course files: see docs/formats/course.md)
        Bytes d;
        try {
            d = read(disc, f.path);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s: %s\n", f.path.c_str(), e.what());
            continue;
        }
        std::string kind;
        try {
            if (gt3r::has_magic(d, "INST")) {
                kind = sound::InstBank::is_gt4(d) ? "INST" : "INST (Gran Turismo 3 layout)";
                if (sound::InstBank::is_gt4(d)) {
                    const sound::InstBank b = sound::InstBank::parse(d);
                    for (const auto* list : {&b.programs, &b.effect_programs})
                        for (const auto& p : *list) programs += bool(p);
                    tones += b.tone_count();
                    for (const auto& set : b.effect_sequences) effect_sequences += set.size();
                }
            } else if (gt3r::has_magic(d, "Ssqt")) {
                kind = "Ssqt";
                const sound::SqtBank b = sound::SqtBank::parse(d);
                for (size_t i = 0; i < b.sequences.size(); ++i) {
                    ++sequences;
                    for (const sound::SqtEvent& e : sound::decode_sequence(b, i)) {
                        notes += e.kind == sound::SqtEvent::Kind::NoteOn;
                        if (e.kind == sound::SqtEvent::Kind::Control) ++controllers[e.a];
                    }
                }
            } else if (gt3r::has_magic(d, "ENGN")) {
                kind = "ENGN";
                layers += audio::parse_engine_bank(d).layers.size();
                try {
                    gt3r::sound::EngineBank::parse(d);
                } catch (const FormatError&) {
                    std::printf("%s: layers not in data order\n", f.path.c_str());
                }
            } else if (gt3r::has_magic(d, "GTSE")) {
                kind = "GTSE";
                sound::EffectIndex::parse(d);
            } else if (gt3r::has_magic(d, "MSEQ") || gt3r::has_magic(d, "MADS")) {
                kind = std::string(reinterpret_cast<const char*>(d.data()), 4) + " v" + std::to_string(rd32(d, 4));
                records += sound::MusicIndex::parse(d).records();
            } else if (gt3r::has_magic(d, "BMRK")) {
                kind = "BMRK";
                const auto m = sound::parse_markers(d);
                for (const auto& t : m.tracks) markers += t.size();
            } else if (gt3r::has_magic(d, "SDVL")) {
                kind = "SDVL";
                sound::VolumeTable::parse(d);
            } else {
                kind = "other";
            }
            ++counts[kind].files;
        } catch (const std::exception& e) {
            Count& c = counts[kind];
            ++c.files;
            if (!c.failed++) c.first_error = f.path + ": " + e.what();
        }
    }
    for (const auto& [kind, c] : counts) {
        std::printf("%-30s %5zu files, %zu failed", kind.c_str(), c.files, c.failed);
        if (c.failed) std::printf("  (%s)", c.first_error.c_str());
        std::printf("\n");
    }
    std::printf("banks: %zu programs, %zu tones with a sample, %zu effect sequences\n", programs, tones, effect_sequences);
    std::printf("sequences: %zu, %zu notes; controllers:", sequences, notes);
    for (const auto& [c, n] : controllers) std::printf(" %02X x%zu", c, n);
    std::printf("\nengine sounds: %zu layers; music index records: %zu; stream markers: %zu\n", layers, records, markers);
    size_t failed = 0;
    for (const auto& [kind, c] : counts) failed += c.failed;
    return failed ? 1 : 0;
}

void print_program(const char* what, size_t n, const sound::Program& p) {
    const char* mode = p.mode == sound::Program::Mode::ByKey ? "by key" : p.mode == sound::Program::Mode::Layered ? "layered" : "first match";
    std::printf("%s %zu: %s, volume %d, keys from %d", what, n, mode, p.volume, p.key_base);
    if (p.mode == sound::Program::Mode::ByKey) std::printf(" to %d", p.key_top);
    std::printf(", %zu tones\n", p.tones.size());
    for (size_t i = 0; i < p.tones.size(); ++i) {
        const sound::Tone& t = p.tones[i];
        if (t.empty()) continue;
        std::printf("    %2zu: %s %3d-%3d center %3d%+d/16 flags %02X sample %06X adsr %04X %04X volume %3d pan %3d bend %d lfo %d\n", i,
                    p.mode == sound::Program::Mode::ByKey ? "group/priority" : "keys", t.key_lo, t.key_hi, t.center, t.fine, t.flags, t.address,
                    t.adsr1, t.adsr2, t.volume, t.pan, t.bend_range, t.lfo);
    }
}

int bank(Disc& disc, const std::string& path) {
    const Bytes d = read(disc, path);
    const sound::InstBank b = sound::InstBank::parse(d);
    std::printf("%s: %zu bytes of samples, address unit %d\n", path.c_str(), b.adpcm->size(), b.address_unit);
    for (size_t i = 0; i < b.programs.size(); ++i)
        if (b.programs[i]) print_program("program", i, *b.programs[i]);
    for (size_t i = 0; i < b.effect_programs.size(); ++i)
        if (b.effect_programs[i]) print_program("effect program", i, *b.effect_programs[i]);
    for (size_t s = 0; s < b.effect_sequences.size(); ++s)
        for (size_t e = 0; e < b.effect_sequences[s].size(); ++e)
            std::printf("effect sequence %zu.%zu at %X\n", s, e, b.effect_sequences[s][e].offset);
    return 0;
}

int effects(Disc& disc) {
    const sound::EffectIndex index = sound::EffectIndex::parse(read(disc, "sound/gt4se.inf"));
    for (const sound::EffectEntry& e : index.entries) {
        const char* file = sound::effect_bank_file(e.bank);
        std::printf("%-16s %-22s %s %d%s%d\n", e.name.c_str(), file ? file : "?", e.sequence ? "sequence" : "program", e.index,
                    e.sequence ? "." : " key ", e.key);
    }
    return 0;
}

int music(Disc& disc) {
    for (const ImageFile& f : disc.files()) {
        if (!ends_with(f.path, ".inf") || f.size < 0x14) continue;
        const Bytes d = read(disc, f.path);
        if (!gt3r::has_magic(d, "MSEQ") && !gt3r::has_magic(d, "MADS")) continue;
        const sound::MusicIndex index = sound::MusicIndex::parse(d);
        const bool streams = gt3r::has_magic(d, "MADS");
        std::printf("%s: version %u, %zu groups, %zu pieces\n", f.path.c_str(), index.version, index.groups.size(), index.records());
        for (size_t g = 0; g < index.groups.size(); ++g)
            for (const sound::MusicRecord& r : index.groups[g]) {
                std::printf("  %2zu %-16s %-14s %-16s %-28.28s %-20.20s %s", g, streams && sound::stream_group_name(g) ? sound::stream_group_name(g) : "",
                            r.name.c_str(), r.file.c_str(), r.title.c_str(), r.artist.c_str(), r.genre.c_str());
                if (!streams) std::printf(" sequence %u", r.value);
                std::printf("\n");
            }
    }
    return 0;
}

void page_sounds(const menu::Widget& w, const std::string& project, std::map<std::string, size_t>& music, size_t& pages) {
    if (w.type == "RootWindow") {
        ++pages;
        const std::string bgm = w.text("bgm"), nav = w.text("navigate_sound");
        if (!bgm.empty()) ++music[bgm];
        if (!bgm.empty() || !nav.empty())
            std::printf("  %-22s %-28s music %-12s navigation sound %s\n", project.c_str(), w.name().c_str(), bgm.empty() ? "-" : bgm.c_str(),
                        nav.empty() ? "-" : nav.c_str());
    }
    for (const menu::Widget& c : w.children()) page_sounds(c, project, music, pages);
}

int pages(Disc& disc) {
    std::map<std::string, size_t> music;
    size_t count = 0;
    for (const ImageFile& f : disc.files()) {
        if (!starts_with(f.path, "projects/") || !ends_with(f.path, ".mproject")) continue;
        try {
            const Bytes d = read(disc, f.path);
            const std::string project = f.path.substr(9, f.path.find('/', 9) - 9);
            page_sounds(menu::parse_layout(ByteSpan(d)), project, music, count);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", f.path.c_str(), e.what());
        }
    }
    std::printf("%zu pages;", count);
    for (const auto& [name, n] : music) std::printf(" %s x%zu", name.c_str(), n);
    std::printf("\n");
    return 0;
}

int seq(Disc& disc, const Args& a) {
    const sound::SqtBank b = sound::SqtBank::parse(read(disc, a[1] + ".sqt"));
    for (size_t i = 0; i < b.sequences.size(); ++i) {
        const sound::SqtSummary s = sound::summarize_sequence(b, i);
        std::printf("sequence %zu: %u ticks at %u a beat, %u notes, channels %04X, intro %.2f s, loop %.2f s, programs", i, s.ticks,
                    b.sequences[i].resolution, s.notes, s.channels, s.intro_seconds, s.loop_seconds);
        for (u8 p : s.programs) std::printf(" %d", p);
        std::printf("\n");
        if (a.size() > 2 && size_t(std::atoi(a[2].c_str())) == i)
            for (const sound::SqtEvent& e : sound::decode_sequence(b, i)) {
                static const char* const kNames[] = {"off", "on", "control", "program", "bend", "tempo", "end", "meta", ""};
                std::printf("  %7u ch %2d %-8s %3d %3d %u\n", e.tick, e.channel, kNames[size_t(e.kind)], e.a, e.b, e.tempo);
            }
    }
    return 0;
}

int seq_wav(Disc& disc, const Args& a) {
    const auto bank = std::make_shared<const sound::InstBank>(sound::InstBank::parse(read(disc, a[1] + ".ins")));
    const auto seqs = std::make_shared<const sound::SqtBank>(sound::SqtBank::parse(read(disc, a[1] + ".sqt")));
    const int loops = a.size() > 4 ? std::atoi(a[4].c_str()) : 1;
    const float volume = a.size() > 5 ? float(std::atof(a[5].c_str())) : 1.0f;
    const audio::MusicRender r = audio::render_music(bank, seqs, std::atoi(a[2].c_str()), loops, 600, 2, volume);
    const audio::Level level = audio::measure(r.pcm);
    std::printf("peak %d, rms %.4f, %zu samples at full scale\n", level.peak, level.rms, level.clipped);
    std::printf("stopped at %.2f s, most voices %d, loop ends at", double(r.end_frame) / gt3r::VoiceMixer::kRate, r.max_voices);
    for (u64 f : r.loop_frames) std::printf(" %.3f s", double(f) / gt3r::VoiceMixer::kRate);
    std::printf("\n");
    double contrast = 0;
    const int cents = audio::tuning_offset_cents(r.pcm, gt3r::VoiceMixer::kRate * 5, gt3r::VoiceMixer::kRate * 60, &contrast);
    std::printf("tuning: %+d cents from A440 (contrast %.2f)\n", cents, contrast);
    return write_wav(a[3], r.pcm) ? 0 : 1;
}

int se_wav(Disc& disc, const Args& a) {
    gt3r::Vfs vfs(&disc.volume());
    audio::OfflineSound s;
    audio::MenuSound menu(vfs, s.driver(), disc.info().game == Game::Prologue ? "jp" : "us");
    if (!menu.play(a[1])) {
        std::fprintf(stderr, "error: no effect named %s\n", a[1].c_str());
        return 1;
    }
    s.advance_seconds(0.25);
    while (s.driver().voices() > 0 && s.pcm.size() < size_t(gt3r::VoiceMixer::kRate) * 2 * 20) s.advance(6);
    s.advance_seconds(1.0);
    return write_wav(a[2], s.pcm) ? 0 : 1;
}

int car(Disc& disc, const Args& a) {
    gt3r::Vfs vfs(&disc.volume());
    const audio::CarSoundFiles files = audio::car_sound_files(vfs, a[1]);
    if (files.engine.empty()) {
        std::fprintf(stderr, "error: no sound found for car %s\n", a[1].c_str());
        return 1;
    }
    std::printf("%s: engine sound %d, %s\n  %s\n  %s\n", a[1].c_str(), files.id, files.variant.c_str(), files.engine.c_str(), files.exhaust.c_str());
    for (const std::string* path : {&files.engine, &files.exhaust}) {
        const auto data = vfs.read(*path);
        if (!data) {
            std::printf("  %s: missing\n", path->c_str());
            continue;
        }
        const gt3r::sound::EngineBank b = audio::parse_engine_bank(*data);
        std::printf("  %s: %zu layers at", path->c_str(), b.layers.size());
        for (const auto& l : b.layers) std::printf(" %d", l.base_rpm);
        std::printf(" rpm\n");
    }
    if (a.size() < 3) return 0;
    audio::OfflineSound s;
    audio::CarSound sound(vfs, s.mixer());
    if (!sound.load(a[1])) {
        std::fprintf(stderr, "error: the car's engine sounds do not load\n");
        return 1;
    }
    // Ten seconds from 1000 to 8000 rpm at full throttle, then two of coasting down.
    const int up = 10 * audio::SoundDriver::kUpdateRate, down = 2 * audio::SoundDriver::kUpdateRate;
    s.advance(up + down, [&](int i) {
        audio::CarSoundState in;
        in.rpm = i < up ? 1000.0f + 7000.0f * float(i) / float(up) : 8000.0f - 6000.0f * float(i - up) / float(down);
        in.throttle = i < up ? 1.0f : 0.0f;
        sound.update(in, audio::SoundListener{}, true);
    });
    sound.stop();
    s.advance_seconds(0.5);
    return write_wav(a[2], s.pcm) ? 0 : 1;
}

int race_tables(Disc& disc) {
    const audio::RaceSoundTables t = audio::RaceSoundTables::from_core(disc.read_program());
    std::printf("gains %s, microphones %s, numbers %s\n", t.found_gains ? "found" : "not found", t.found_mics ? "found" : "not found",
                t.found_params ? "found" : "not found");
    static const char* const kViews[] = {"in-car, its car", "in-car, others", "chase, its car", "chase, others", "other cameras"};
    std::printf("view gains (engine exhaust turbine b_off whine clash rumble in_car wind on_road rain on_skill - off_skill off_road on_bound):\n");
    for (int v = 0; v < audio::RaceSoundTables::kViews; ++v) {
        std::printf("  %-16s", kViews[v]);
        for (int c = 0; c < 16; ++c) std::printf(" %5.2f", double(t.gain[size_t(v)][size_t(c)]));
        std::printf("\n");
    }
    static const char* const kModes[] = {"mono", "stereo", "surround"};
    if (!t.found_mics) std::printf("microphones: none: every sound at 32 / its distance, at most 1 (Prologue's panner)\n");
    for (int m = 0; m < audio::RaceSoundTables::kModes && t.found_mics; ++m) {
        std::printf("microphones, %s:", kModes[m]);
        for (int n = 0; n < audio::RaceSoundTables::kMics; ++n)
            std::printf(" (slope %.2f, reach %.1f m)", double(t.mics[size_t(m)][size_t(n)].slope), double(t.mics[size_t(m)][size_t(n)].reach));
        std::printf("\n");
    }
    const auto& p = t.params;
    std::printf("wind: gain %.6f, %.1f..%.1f m/s -> %.0f..%.0f rpm, at most %.2f\n", double(p.wind_gain), double(p.wind_speed_lo), double(p.wind_speed_hi),
                double(p.wind_rpm_lo), double(p.wind_rpm_hi), double(p.wind_max));
    std::printf("cabin: gain %.6f, %.1f..%.1f m/s -> pitch %.2f..%.2f, at most %.2f\n", double(p.incar_gain), double(p.incar_speed_lo),
                double(p.incar_speed_hi), double(p.incar_pitch_lo), double(p.incar_pitch_hi), double(p.incar_max));
    std::printf("road roar: gain %.2f (narrow x%.2f, wide x%.2f), %.1f..%.1f m/s -> pitch %.2f..%.2f, at most %.2f\n", double(p.roar_gain),
                double(p.roar_narrow), double(p.roar_wide), double(p.roar_speed_lo), double(p.roar_speed_hi), double(p.roar_pitch_lo),
                double(p.roar_pitch_hi), double(p.roar_max));
    std::printf("gear whine: gain %.2f..%.2f at %.0f N m, pitch %.2f + %.5f per rad/s\n", double(p.whine_gain_lo), double(p.whine_gain_hi),
                double(p.whine_torque), double(p.whine_pitch_lo), double(p.whine_pitch_rate));
    std::printf("levels: engine %.2f, exhaust %.2f, spool %.2f, blow-off %.2f; contacts x%.2f\n", double(p.engine_level), double(p.exhaust_level),
                double(p.turbine_level), double(p.blowoff_scale), double(p.clash_scale));
    return 0;
}

int pass_wav(Disc& disc, const Args& a) {
    gt3r::Vfs vfs(&disc.volume());
    audio::OfflineSound s;
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables = audio::RaceSoundTables::from_core(disc.read_program());
    mix->master = library->levels().level(int(audio::OutputMode::Stereo), sound::VolumeTable::Race) * 115.0f / 128.0f;
    audio::CarSound car(vfs, s.mixer(), &s.driver(), library);
    car.set_mix(mix);
    if (!car.load(a[1])) {
        std::fprintf(stderr, "error: the car's engine sounds do not load\n");
        return 1;
    }
    const float speed = (a.size() > 3 ? float(std::atof(a[3].c_str())) : 150.0f) / 3.6f;
    const int steps = 5 * audio::SoundDriver::kUpdateRate;
    s.advance(steps, [&](int i) {
        audio::CarSoundState in;
        const float t = float(i - steps / 2) / 60.0f;
        in.position = {10.0f, 0, speed * t}; // passing on the camera's right
        in.velocity = {0, 0, speed};
        in.speed = speed;
        in.rpm = 6000, in.throttle = 1, in.torque_full = 3, in.torque_friction = 1;
        for (auto& w : in.wheels) w.position = in.position, w.rim_speed = speed;
        car.update(in, audio::SoundListener{}, false);
        if (i % 30 == 0) {
            const auto& r = car.report();
            std::printf("  %5.2f s: z %6.1f m, Doppler %.3f, engine %.3f exhaust %.3f wind %.3f (L %.2f R %.2f)\n", double(t), double(in.position.z),
                        double(r.doppler), double(r.engine_gain), double(r.exhaust_gain), double(r.wind_gain), double(r.engine.left), double(r.engine.right));
        }
    });
    car.stop();
    s.advance_seconds(0.5);
    return write_wav(a[2], s.pcm) ? 0 : 1;
}

int drive_wav(Disc& disc, const Args& a) {
    gt3r::Vfs vfs(&disc.volume());
    audio::OfflineSound s;
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables = audio::RaceSoundTables::from_core(disc.read_program());
    mix->master = library->levels().level(int(audio::OutputMode::Stereo), sound::VolumeTable::Race) * 115.0f / 128.0f;
    audio::CarSound car(vfs, s.mixer(), &s.driver(), library);
    car.set_mix(mix);
    if (!car.load(a[1])) {
        std::fprintf(stderr, "error: the car's engine sounds do not load\n");
        return 1;
    }
    float rpm0 = 0, rpm1 = 0;
    if (std::sscanf(a[3].c_str(), "%f:%f", &rpm0, &rpm1) < 2) rpm1 = rpm0;
    const double seconds = a.size() > 4 ? std::atof(a[4].c_str()) : 5.0;
    const std::string view = a.size() > 5 ? a[5] : "chase";
    const float per = a.size() > 6 ? float(std::atof(a[6].c_str())) : 200.0f;
    audio::SoundListener at;
    at.kind = view == "incar" ? audio::SoundListener::Kind::InCar : view == "other" ? audio::SoundListener::Kind::Other : audio::SoundListener::Kind::Chase;
    const bool own = view != "other";
    const int steps = int(seconds * audio::SoundDriver::kUpdateRate);
    s.advance(steps, [&](int i) {
        audio::CarSoundState in;
        in.rpm = rpm0 + (rpm1 - rpm0) * float(i) / float(std::max(1, steps - 1));
        in.throttle = 1, in.torque_full = 3, in.torque_friction = 1;
        in.speed = in.rpm / per;
        // The chase camera 5 m behind the car; another camera 20 m to the side; in the car: at it.
        const float behind = at.kind == audio::SoundListener::Kind::Chase ? 5.0f : 0.0f;
        in.position = at.kind == audio::SoundListener::Kind::Other ? race::Vec3{20, 0, 0} : race::Vec3{0, 0, behind};
        in.velocity = {0, 0, at.kind == audio::SoundListener::Kind::Other ? in.speed : 0.0f}; // the camera follows the car
        for (auto& w : in.wheels) w.position = in.position, w.rim_speed = in.speed;
        car.update(in, at, own);
        if (i % 60 == 0) {
            const auto& r = car.report();
            std::printf("  %5.2f s: %5.0f rpm, levels %d / %d, engine %.3f exhaust %.3f wind %.3f, cabin %.3f, roar %.3f\n", double(i) / 60.0,
                        double(in.rpm), r.engine_level, r.exhaust_level, double(r.engine_gain), double(r.exhaust_gain), double(r.wind_gain), double(r.cabin),
                        double(r.wheel[0][1]));
        }
    });
    car.stop();
    s.advance_seconds(0.3);
    return write_wav(a[2], s.pcm) ? 0 : 1;
}

int stream_wav(Disc& disc, const Args& a) {
    const Bytes data = read(disc, a[1]);
    const gt3r::AdsHeader h = gt3r::AdsHeader::parse(data);
    gt3r::AdsStream stream(h, [&data](u64 off, void* out, size_t len) {
        const size_t n = off < data.size() ? std::min<size_t>(len, data.size() - size_t(off)) : 0;
        std::memcpy(out, data.data() + off, n);
        std::memset(static_cast<u8*>(out) + n, 0, len - n);
    }, false);
    const double seconds = a.size() > 3 ? std::atof(a[3].c_str()) : 1e9;
    const float level = a.size() > 4 ? float(std::atof(a[4].c_str())) : 1.0f;
    const size_t want = size_t(std::min(seconds * h.sample_rate, double(h.total_frames())));
    std::vector<s16> pcm;
    std::vector<s16> buf(4096 * std::max<u32>(1, h.channels));
    for (size_t got = 0; got < want;) {
        const size_t n = stream.read(buf.data(), std::min<size_t>(4096, want - got));
        if (n == 0) break;
        for (size_t f = 0; f < n; ++f) {
            const s16 l = buf[f * h.channels], r = h.channels > 1 ? buf[f * h.channels + 1] : l;
            pcm.push_back(s16(std::clamp(std::lround(l * level), -32768L, 32767L)));
            pcm.push_back(s16(std::clamp(std::lround(r * level), -32768L, 32767L)));
        }
        got += n;
    }
    std::printf("%s: %u Hz, %u sides, %.2f s\n", a[1].c_str(), h.sample_rate, h.channels, double(h.total_frames()) / h.sample_rate);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(a[2]).parent_path(), ec);
    return gt3r::write_host_file(a[2], audio::wav_file(pcm, h.sample_rate)) ? 0 : 1;
}

} // namespace

int cmd_sound(Disc& disc, const std::vector<std::string>& a) {
    const std::string& what = a[0];
    if (what == "survey") return survey(disc);
    if (what == "bank" && a.size() > 1) return bank(disc, a[1]);
    if (what == "effects") return effects(disc);
    if (what == "music") return music(disc);
    if (what == "pages") return pages(disc);
    if (what == "seq" && a.size() > 1) return seq(disc, a);
    if (what == "seq-wav" && a.size() > 3) return seq_wav(disc, a);
    if (what == "se-wav" && a.size() > 2) return se_wav(disc, a);
    if ((what == "car" || what == "car-wav") && a.size() > 1) return car(disc, a);
    if (what == "race-tables") return race_tables(disc);
    if (what == "pass-wav" && a.size() > 2) return pass_wav(disc, a);
    if (what == "drive-wav" && a.size() > 3) return drive_wav(disc, a);
    if (what == "stream-wav" && a.size() > 2) return stream_wav(disc, a);
    std::fprintf(stderr, "usage: gt4tool sound <disc> survey | bank <path.ins> | effects | music | pages | seq <bank> [n] |\n"
                         "       seq-wav <bank> <n> <out.wav> [loops] [volume] | se-wav <name> <out.wav> | car <label> [out.wav] |\n"
                         "       race-tables | pass-wav <label> <out.wav> [km/h] |\n"
                         "       drive-wav <label> <out.wav> <rpm>[:<rpm>] <seconds> [incar|chase|other] [rpm per m/s] |\n"
                         "       stream-wav <path.ads> <out.wav> [seconds] [level]\n");
    return 2;
}

} // namespace gt4r
