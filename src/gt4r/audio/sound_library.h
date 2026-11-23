#pragma once

// The disc's sound files by what the game calls them: the effect banks and their index
// (sound/gt4se.inf), the sequenced music's groups (music/<region>/mid.inf) and the streamed
// music's (bgm/<region>/ads.inf), read through the file layer when first asked for.

#include "gt4r/formats/sound/inst_bank.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/formats/sound/sqt.h"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt4r::audio {

// The folder of the disc's region under music/, bgm/ and narration/ ("us", "jp"): the one that
// holds a music index. Empty when the disc has none.
std::string sound_region(gt3r::Vfs& vfs);

// A bank of the sequenced music with its sequences.
struct MusicBank {
    std::shared_ptr<const sound::InstBank> bank;
    std::shared_ptr<const sound::SqtBank> sequences;
    explicit operator bool() const { return bank && sequences; }
};

// One piece of the sequenced music as the index names it.
struct MusicPiece {
    std::string group;  // "map"
    std::string file;   // the bank's path without its extension: "music/us/map"
    int number = 0;     // the sequence in the bank
    std::string title;  // the index's source name ("gt4mn-02.mid")
    std::string artist;
};

class SoundLibrary {
public:
    // `region`: sound_region()'s answer; empty to look for it.
    explicit SoundLibrary(gt3r::Vfs& vfs, std::string region = {});

    const std::string& region() const { return region_; }

    // --- effects ---
    const sound::EffectIndex& effects();
    // An effect bank by its number in the index; null when the disc has none. The shutter
    // bank (4) is the one chosen with set_shutter().
    std::shared_ptr<const sound::InstBank> effect_bank(int bank);
    // The camera whose shutter sound bank 4 holds: "" for sound/gt4shutter.ins, else the
    // type's letter or name as the game appends it ("typea": sound/gt4shuttertypea.ins).
    void set_shutter(const std::string& type);
    // The mix levels (sound/sdvol.dat); every level 1 without the file.
    const sound::VolumeTable& levels();

    // --- sequenced music ---
    // The groups' names in the index's order ("home", "powerspeed", "race", ...). A disc with
    // the earlier index (bgmmid.inf: one bank for every piece) has one group per name.
    const std::vector<std::string>& music_groups();
    int music_group(const std::string& name); // any case; -1 when there is none
    // The pieces of a group, in order.
    std::vector<MusicPiece> music_pieces(int group);
    // The bank a group's pieces are in (the last one asked for is kept).
    MusicBank music_bank(int group);
    MusicBank music_bank(const std::string& file);

    // --- streamed music ---
    // The streamed index's groups (sound_index.h stream_group_name) and their pieces.
    const sound::MusicIndex& streams();
    // The file of piece `number` of a group ("bgm/us/photo_01.ads"); `intro`: its short
    // version ("bgm/us/i_photo_01.ads"). Empty when the group has no such piece.
    std::string stream_file(int group, int number, bool intro = false);
    // A narration's file ("narration/us/<code>.ads").
    std::string narration_file(const std::string& code);

private:
    void load_music_index();
    std::optional<Bytes> read(const std::string& path) const;

    gt3r::Vfs& vfs_;
    std::string region_;
    std::mutex mutex_;
    std::optional<sound::EffectIndex> effects_;
    std::array<std::shared_ptr<const sound::InstBank>, sound::kEffectBanks> banks_{};
    std::array<bool, sound::kEffectBanks> bank_tried_{};
    std::string shutter_;
    std::optional<sound::VolumeTable> levels_;
    bool music_tried_ = false;
    std::vector<std::string> groups_;
    std::vector<std::vector<MusicPiece>> pieces_;
    std::string bank_file_;
    MusicBank bank_;
    std::optional<sound::MusicIndex> streams_;
};

} // namespace gt4r::audio
