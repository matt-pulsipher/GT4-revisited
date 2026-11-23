#pragma once

#include "disc/iso9660.h"
#include "disc/volume.h"
#include "gt4r/disc/archive_image.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4r {

enum class Game : u8 {
    GT4,      // Gran Turismo 4
    Prologue, // Gran Turismo 4 Prologue
};
const char* game_name(Game g);

struct DiscInfo {
    std::string serial;   // "SCUS_973.28" from SYSTEM.CNF BOOT2
    std::string version;  // SYSTEM.CNF VER
    std::string video;    // SYSTEM.CNF VMODE
    std::string volume_id;
    Game game = Game::GT4;
    bool verified = false; // a release in the tested list with that release's files
    bool modified = false; // a release in the list whose files are not that release's
    std::string description;
    std::string problem; // a problem with the image for the player (an incomplete dump); empty when none
    // The files' fingerprint: 16 hex digits of SHA-256 over CORE.GT4 and the volumes' indexes.
    std::string digest;
    std::string key; // where the disc keeps its saves and mod choices: the serial as "SCUS-97328"
};

// One of the disc's volumes (GT4.VOL, GT4L1.VOL).
struct VolumeInfo {
    std::string name;
    u64 offset = 0, size = 0; // in the disc image
    bool paged = false;       // its files come from a paged index (archive_index.h), else from the front table
    size_t files = 0;
    size_t unused_entries = 0; // a paged volume's front table: entries the game does not read
};

// A user-supplied Gran Turismo 4 (or Prologue) disc image: the open file, its volumes' files
// and the shared engine's Volume over them (archive_image.h).
class Disc {
public:
    // Throws FormatError with a message for the player.
    static std::unique_ptr<Disc> open(const std::string& path);

    const DiscInfo& info() const { return info_; }
    gt3r::Volume& volume() { return *volume_; }
    const std::vector<ImageFile>& files() const { return image_->files(); }
    const std::vector<VolumeInfo>& volumes() const { return volumes_; }
    // A file by its path; null when the disc has none.
    const ImageFile* file(const std::string& path) const;
    // The disc's packed game program (CORE.GT4). Reference data: the engine never executes it.
    Bytes read_program();
    // A file's bytes as the disc stores them (still packed when ImageFile::packed).
    Bytes read_stored(const ImageFile& f) { return file_->read_at(f.disc_offset, f.stored_size); }

    std::vector<gt3r::Iso9660*> file_systems() const;
    std::optional<std::pair<gt3r::Iso9660*, gt3r::Iso9660::Entry>> find_file(const std::string& path) const;

private:
    Disc() = default;
    std::unique_ptr<gt3r::Reader> file_;
    std::vector<std::unique_ptr<gt3r::Iso9660>> layers_;
    std::unique_ptr<ArchiveImage> image_;
    std::unique_ptr<gt3r::Volume> volume_;
    std::vector<VolumeInfo> volumes_;
    std::unordered_map<std::string, size_t> by_path_;
    DiscInfo info_;
};

} // namespace gt4r
