#include "gt4r/audio/sound_library.h"

#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>

namespace gt4r::audio {

namespace {

const char* const kRegions[] = {"us", "jp", "uk", "eu", "kr", "tw", "cn"};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string sound_region(gt3r::Vfs& vfs) {
    for (const char* r : kRegions)
        for (const char* file : {"mid.inf", "bgmmid.inf"}) {
            const auto d = vfs.read(std::string("music/") + r + "/" + file);
            if (d && d->size() >= 0x14) return r;
        }
    return {};
}

SoundLibrary::SoundLibrary(gt3r::Vfs& vfs, std::string region) : vfs_(vfs), region_(std::move(region)) {
    if (region_.empty()) region_ = sound_region(vfs);
}

std::optional<Bytes> SoundLibrary::read(const std::string& path) const { return vfs_.read(path); }

const sound::EffectIndex& SoundLibrary::effects() {
    std::lock_guard lock(mutex_);
    if (!effects_) {
        effects_.emplace();
        try {
            if (const auto d = read("sound/gt4se.inf")) effects_ = sound::EffectIndex::parse(*d);
        } catch (const std::exception& e) {
            LOG_WARN("sound/gt4se.inf: %s", e.what());
        }
    }
    return *effects_;
}

std::shared_ptr<const sound::InstBank> SoundLibrary::effect_bank(int bank) {
    std::lock_guard lock(mutex_);
    if (bank < 0 || bank >= sound::kEffectBanks) return nullptr;
    const size_t i = size_t(bank);
    if (!bank_tried_[i]) {
        bank_tried_[i] = true;
        std::string path = sound::effect_bank_file(u8(bank));
        if (bank == 4 && !shutter_.empty()) path = "sound/gt4shutter" + shutter_ + ".ins";
        // Prologue's fourth bank is the pod's effects.
        if (bank == 3 && !vfs_.exists(path)) path = "sound/gt4pod.ins";
        try {
            if (const auto d = read(path)) banks_[i] = std::make_shared<const sound::InstBank>(sound::InstBank::parse(*d));
        } catch (const std::exception& e) {
            LOG_WARN("%s: %s", path.c_str(), e.what());
        }
    }
    return banks_[i];
}

void SoundLibrary::set_shutter(const std::string& type) {
    std::lock_guard lock(mutex_);
    const std::string t = lower(type);
    if (t == shutter_) return;
    shutter_ = t;
    bank_tried_[4] = false;
    banks_[4].reset();
}

const sound::VolumeTable& SoundLibrary::levels() {
    std::lock_guard lock(mutex_);
    if (!levels_) {
        levels_.emplace();
        try {
            if (const auto d = read("sound/sdvol.dat")) levels_ = sound::VolumeTable::parse(*d);
        } catch (const std::exception& e) {
            LOG_WARN("sound/sdvol.dat: %s", e.what());
        }
    }
    return *levels_;
}

void SoundLibrary::load_music_index() {
    if (music_tried_) return;
    music_tried_ = true;
    const std::string dir = "music/" + region_ + "/";
    try {
        if (const auto d = read(dir + "mid.inf"); d && d->size() >= 0x14) {
            // A group per bank: the group's name is its bank's file name.
            const sound::MusicIndex index = sound::MusicIndex::parse(*d);
            for (const auto& g : index.groups) {
                std::vector<MusicPiece> pieces;
                for (const sound::MusicRecord& r : g) pieces.push_back({r.name, dir + r.name, int(r.value), r.file, r.artist});
                groups_.push_back(g.empty() ? std::string() : g.front().name);
                pieces_.push_back(std::move(pieces));
            }
        } else if (const auto old = read(dir + "bgmmid.inf"); old && old->size() >= 0x14) {
            // The earlier index: every piece is a sequence of the one bank "bgmmid".
            const sound::MusicIndex index = sound::MusicIndex::parse(*old);
            for (const auto& g : index.groups)
                for (const sound::MusicRecord& r : g) {
                    groups_.push_back(r.name);
                    pieces_.push_back({MusicPiece{r.name, dir + "bgmmid", int(r.value), r.title, r.artist}});
                }
        }
    } catch (const std::exception& e) {
        LOG_WARN("%s: %s", dir.c_str(), e.what());
    }
}

const std::vector<std::string>& SoundLibrary::music_groups() {
    std::lock_guard lock(mutex_);
    load_music_index();
    return groups_;
}

int SoundLibrary::music_group(const std::string& name) {
    std::lock_guard lock(mutex_);
    load_music_index();
    const std::string n = lower(name);
    for (size_t i = 0; i < groups_.size(); ++i)
        if (!groups_[i].empty() && groups_[i] == n) return int(i);
    return -1;
}

std::vector<MusicPiece> SoundLibrary::music_pieces(int group) {
    std::lock_guard lock(mutex_);
    load_music_index();
    return group >= 0 && size_t(group) < pieces_.size() ? pieces_[size_t(group)] : std::vector<MusicPiece>{};
}

MusicBank SoundLibrary::music_bank(const std::string& file) {
    std::lock_guard lock(mutex_);
    if (file == bank_file_ && bank_) return bank_;
    MusicBank b;
    try {
        const auto ins = read(file + ".ins"), sqt = read(file + ".sqt");
        if (ins && sqt) {
            b.bank = std::make_shared<const sound::InstBank>(sound::InstBank::parse(*ins));
            b.sequences = std::make_shared<const sound::SqtBank>(sound::SqtBank::parse(*sqt));
        }
    } catch (const std::exception& e) {
        LOG_WARN("%s: %s", file.c_str(), e.what());
        return {};
    }
    if (b) {
        bank_file_ = file;
        bank_ = b;
    }
    return b;
}

MusicBank SoundLibrary::music_bank(int group) {
    const std::vector<MusicPiece> pieces = music_pieces(group);
    return pieces.empty() ? MusicBank{} : music_bank(pieces.front().file);
}

const sound::MusicIndex& SoundLibrary::streams() {
    std::lock_guard lock(mutex_);
    if (!streams_) {
        streams_.emplace();
        const std::string dir = "bgm/" + region_ + "/";
        try {
            auto d = read(dir + "ads.inf");
            if (!d || d->size() < 0x14) d = read(dir + "bgmads.inf");
            if (d && d->size() >= 0x14) streams_ = sound::MusicIndex::parse(*d);
        } catch (const std::exception& e) {
            LOG_WARN("%s: %s", dir.c_str(), e.what());
        }
    }
    return *streams_;
}

std::string SoundLibrary::stream_file(int group, int number, bool intro) {
    const sound::MusicIndex& index = streams();
    if (group < 0 || size_t(group) >= index.groups.size()) return {};
    const auto& g = index.groups[size_t(group)];
    if (g.empty() || number < 0) return {};
    return "bgm/" + region_ + "/" + (intro ? "i_" : "") + g[size_t(number) % g.size()].file;
}

std::string SoundLibrary::narration_file(const std::string& code) { return "narration/" + region_ + "/" + code + ".ads"; }

} // namespace gt4r::audio
