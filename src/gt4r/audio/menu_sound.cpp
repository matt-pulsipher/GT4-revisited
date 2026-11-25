#include "gt4r/audio/menu_sound.h"

#include <algorithm>

namespace gt4r::audio {

namespace {

constexpr int kStereo = 1;           // the output mode whose mix levels are used
constexpr int kMenuPriority = 0x7FFFFFFF; // a menu effect takes a voice from anything
constexpr int kIntroFlag = 0x1000;

} // namespace

MenuSound::MenuSound(gt3r::Vfs& vfs, SoundDriver& driver, std::string region, StreamPlayer streams)
    : MenuSound(std::make_shared<SoundLibrary>(vfs, std::move(region)), driver, std::move(streams)) {}

MenuSound::MenuSound(std::shared_ptr<SoundLibrary> library, SoundDriver& driver, StreamPlayer streams)
    : library_(std::move(library)), driver_(driver), streams_(std::move(streams)) {}

bool MenuSound::play(const std::string& name) {
    const sound::EffectEntry* e = library_->effects().find(name);
    if (!e) return false;
    const std::shared_ptr<const sound::InstBank> bank = library_->effect_bank(e->bank);
    if (!bank) return false;
    SoundParams p;
    p.left = p.right = effect_option_ * library_->levels().level(kStereo, sound::VolumeTable::Menu);
    p.priority = kMenuPriority;
    if (e->sequence) return driver_.play_effect(bank, e->index, e->key, p) != 0;
    // An effect that is a held note (the race's): one note, left to ring.
    return driver_.note_on(bank, e->index, p) != 0;
}

void MenuSound::load(const std::string& group) {
    unload();
    const int g = library_->music_group(group);
    if (g >= 0 && library_->music_bank(g)) group_ = g;
}

void MenuSound::unload() { stop_music(); }

void MenuSound::stop(bool remember) {
    if (remember && group_ >= 0 && driver_.music_active()) {
        if (size_t(group_) >= next_.size()) next_.resize(size_t(group_) + 1, 0);
        next_[size_t(group_)] = driver_.music_number() + 1;
    }
    driver_.stop_music(true);
}

bool MenuSound::start(int group, int number, bool restart) {
    if (group < 0) return false;
    // The same group still playing plays on.
    if (!restart && group == group_ && driver_.music_active()) return true;
    if (driver_.music_active()) stop(true);
    const MusicBank bank = library_->music_bank(group);
    if (!bank) return false;
    group_ = group;
    const std::vector<MusicPiece> pieces = library_->music_pieces(group);
    if (number < 0) number = size_t(group) < next_.size() ? next_[size_t(group)] : 0;
    if (!pieces.empty()) number = pieces[size_t(number) % pieces.size()].number;
    driver_.set_volume(SoundDriver::Kind::Music, music_volume_ * music_option_ * library_->levels().level(kStereo, sound::VolumeTable::Sequence));
    driver_.play_music(bank.bank, bank.sequences, number);
    return true;
}

bool MenuSound::start_music(const std::string& group) {
    stop_music();
    return start(library_->music_group(group), -1, true);
}

bool MenuSound::keep_music(const std::string& group) {
    if (group == "keep") return true;
    return start(library_->music_group(group), -1, false);
}

void MenuSound::stop_music() { stop(true); }

void MenuSound::fade_out_music(float seconds) {
    if (!driver_.music_active()) return;
    if (group_ >= 0) {
        if (size_t(group_) >= next_.size()) next_.resize(size_t(group_) + 1, 0);
        next_[size_t(group_)] = driver_.music_number() + 1;
    }
    driver_.fade_music(SoundDriver::Fade::DownAndStop, 0.0f, 1.0f / std::max(seconds * float(SoundDriver::kUpdateRate), 1.0f));
}

bool MenuSound::fade_in_music(const std::string& group, float seconds) {
    if (group == "keep") return true;
    const int g = library_->music_group(group);
    const bool starting = g >= 0 && !(g == group_ && driver_.music_active());
    if (!start(g, -1, false)) return false;
    // The game raises the volume from where the sequence's is: a sequence just started is at
    // full volume already, so nothing is heard to rise.
    if (starting) driver_.fade_music(SoundDriver::Fade::Up, 1.0f, 1.0f / std::max(seconds * float(SoundDriver::kUpdateRate), 1.0f));
    return true;
}

std::string MenuSound::music_group() const {
    if (group_ < 0 || !driver_.music_active()) return {};
    const std::vector<std::string>& groups = library_->music_groups();
    return size_t(group_) < groups.size() ? groups[size_t(group_)] : std::string();
}

int MenuSound::music_number() const { return driver_.music_number(); }

int MenuSound::draw(int count) const {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return count > 0 ? int(random_ % u32(count)) : 0;
}

std::string MenuSound::stream_file(const std::string& kind, const std::string& code, int number) const {
    if (kind == "narration") return library_->narration_file(code);
    if (kind != "pcm") return {};
    const int group = sound::stream_group(code);
    const sound::MusicIndex& index = library_->streams();
    if (group < 0 || size_t(group) >= index.groups.size() || index.groups[size_t(group)].empty()) return {};
    const int count = int(index.groups[size_t(group)].size());
    bool intro = false;
    if (number == -1) {
        const u32 keep = random_;
        number = draw(count);
        random_ = keep;
    } else if (number & kIntroFlag) {
        intro = true;
        number &= ~kIntroFlag;
    }
    return library_->stream_file(group, number, intro);
}

bool MenuSound::start_stream(const std::string& kind, const std::string& code, int number, bool repeat, float volume) {
    const std::string path = stream_file(kind, code, number);
    if (number == -1 && kind == "pcm") draw(1); // (the draw stream_file() looked at is taken)
    if (path.empty() || !streams_.play) return false;
    // The level (0x4608C8, kind -2 -> 0x460220 -> 0x45FFF8 -> 0x45FFC0): the script's volume x the mix table's
    // stream level, no option of its own. The pages that follow the menus' music option pass it themselves
    // (`option.menu_bgm_volume`, raw / 128: 0x180AA8): 0.3 x 115 / 128, the 4416 of the saved states. A volume
    // above 1 is that option given raw (0 .. 128), as our own option object may give it.
    const float given = volume > 1.0f ? volume / 128.0f : volume;
    const float level = given * library_->levels().level(kStereo, sound::VolumeTable::Stream);
    return streams_.play(path, repeat, level);
}

void MenuSound::stop_stream() {
    if (streams_.stop) streams_.stop();
}

void MenuSound::pause_stream(bool pause) {
    if (streams_.pause) streams_.pause(pause);
}

bool MenuSound::is_playing_stream() const { return streams_.playing && streams_.playing(); }

void MenuSound::set_music_option(float v) {
    music_option_ = std::clamp(v, 0.0f, 1.0f);
    driver_.set_volume(SoundDriver::Kind::Music, music_volume_ * music_option_ * library_->levels().level(kStereo, sound::VolumeTable::Sequence));
}

void MenuSound::set_effect_option(float v) { effect_option_ = std::clamp(v, 0.0f, 1.0f); }

} // namespace gt4r::audio
