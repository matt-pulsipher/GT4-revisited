#include "gt4r/disc/disc.h"

#include "base/config.h"
#include "base/log.h"
#include "base/sha256.h"
#include "disc/chd_image.h"
#include "disc/disc.h"
#include "gt4r/disc/archive_index.h"

#include <algorithm>
#include <set>

namespace gt4r {

namespace {

struct KnownDisc {
    const char* serial;
    const char* version;
    Game game;
    const char* description;
    const char* digest; // DiscInfo::digest of the release's own files
};

// Discs this project has been tested against. Add entries as they are verified.
constexpr KnownDisc kKnownDiscs[] = {
    {"SCUS_973.28", "1.01", Game::GT4, "Gran Turismo 4 (NTSC-U/C)", "878407142121eb90"},
    {"SCPS_150.55", "1.01", Game::Prologue, "Gran Turismo 4 Prologue (NTSC-J)", "5f9c96f1934fd29e"},
};

} // namespace

const char* game_name(Game g) { return g == Game::Prologue ? "Gran Turismo 4 Prologue" : "Gran Turismo 4"; }

// An image whose files cannot be read because it is cut short says that.
std::unique_ptr<Disc> Disc::open(const std::string& path) {
    std::string problem;
    try {
        return open_image(path, problem);
    } catch (const std::exception& e) {
        if (problem.empty()) throw;
        LOG_WARN("%s: %s", path.c_str(), e.what());
        throw FormatError(problem);
    }
}

std::unique_ptr<Disc> Disc::open_image(const std::string& path, std::string& problem) {
    std::unique_ptr<Disc> disc(new Disc());
    auto file = gt3r::File::open(path);
    if (!file) throw FormatError("Could not open file: " + path);
    // By content: a picked file's path often has no extension.
    if (gt3r::ChdImage::is_chd(*file)) disc->file_ = gt3r::ChdImage::open(std::move(file));
    else disc->file_ = std::move(file);
    gt3r::Reader& image = *disc->file_;
    disc->layers_ = gt3r::iso_layers(image); // one file system, or one per layer

    DiscInfo& info = disc->info_;
    info.volume_id = disc->layers_[0]->volume_id();
    u64 volume_end = 0;
    for (const auto& l : disc->layers_) volume_end = std::max(volume_end, l->base_sector() * gt3r::kSectorSize + l->volume_bytes());
    problem = info.problem = gt3r::incomplete_image(image.size(), volume_end);
    if (!info.problem.empty()) LOG_WARN("%s (%s)", info.problem.c_str(), path.c_str());
    if (auto cnf = disc->find_file("SYSTEM.CNF")) {
        const Bytes b = cnf->first->read(cnf->second);
        const gt3r::IniDocument doc = gt3r::IniDocument::parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
        std::string boot = doc.get("", "BOOT2").value_or(""); // "cdrom0:\SCUS_973.28;1"
        if (auto bs = boot.find_last_of("\\/:"); bs != std::string::npos) boot = boot.substr(bs + 1);
        if (auto semi = boot.find(';'); semi != std::string::npos) boot.resize(semi);
        info.serial = boot;
        info.version = doc.get("", "VER").value_or("");
        info.video = doc.get("", "VMODE").value_or("");
    }

    // The volumes: every *.VOL in the layers' roots with the volume magic, in layer order.
    const auto core = disc->find_file("CORE.GT4");
    gt3r::Sha256 hash;
    if (core) hash.update(core->first->read(core->second));
    std::vector<ImageFile> files;
    std::set<std::string> seen;
    for (gt3r::Iso9660* fs : disc->file_systems())
        for (const gt3r::Iso9660::Entry& e : fs->list("")) {
            const std::string name = gt3r::to_lower(e.name);
            if (e.is_dir || name.size() < 4 || name.compare(name.size() - 4, 4, ".vol") != 0 || e.size < 0x14) continue;
            const u64 base = fs->offset(e);
            // A dual-layer disc's volume reaches to the end of its layer whatever size its entry gives.
            const u64 length = std::min<u64>(e.size, image.size() > base ? image.size() - base : 0);
            if (rd32(image.read_at(base, 4), 0) != ArchiveIndex::kMagic) continue;
            VolumeInfo v;
            v.name = e.name, v.offset = base, v.size = length;
            std::vector<ArchiveFile> listed;
            if (ArchiveIndex::find(image, base, length)) {
                ArchiveIndex index = ArchiveIndex::read(image, base, length);
                hash.update(image.read_at(base + index.offset, index.size));
                v.paged = true;
                if (front_table_size(image, base, length)) {
                    try {
                        v.unused_entries = read_front_table(image, base, length, false).size();
                    } catch (const FormatError&) { // a table the game never reads need not be whole
                    }
                }
                listed = std::move(index.files);
            } else {
                hash.update(image.read_at(base, front_table_size(image, base, length)));
                listed = read_front_table(image, base, length);
            }
            for (ArchiveFile& f : listed) {
                if (!seen.insert(f.path).second) continue; // a path an earlier volume has stays that volume's
                files.push_back({std::move(f.path), base + f.offset, f.size, f.stored_size, f.packed, u8(disc->volumes_.size())});
                ++v.files;
            }
            LOG_INFO("%s: %zu files (%s)", v.name.c_str(), v.files, v.paged ? "paged index" : "front table");
            disc->volumes_.push_back(std::move(v));
        }
    if (!core || disc->volumes_.empty())
        throw FormatError("This does not look like a Gran Turismo 4 disc image (CORE.GT4 / GT4.VOL missing).");
    {
        const std::array<u8, 32> d = hash.finish();
        static constexpr char kHex[] = "0123456789abcdef";
        for (size_t i = 0; i < 8; ++i) info.digest += kHex[d[i] >> 4], info.digest += kHex[d[i] & 15];
    }

    // The game: Gran Turismo 4's volumes have a paged index, Prologue's are read from their front table.
    info.game = disc->volumes_[0].paged ? Game::GT4 : Game::Prologue;
    info.description = std::string("Unverified ") + game_name(info.game) + " disc";
    for (const KnownDisc& k : kKnownDiscs)
        if (info.serial == k.serial && info.version == k.version) {
            info.game = k.game;
            info.description = k.description;
            info.modified = info.digest != k.digest;
            info.verified = !info.modified;
        }
    if (info.modified) LOG_WARN("Disc %s v%s: files differ from the release's (a modified copy)", info.serial.c_str(), info.version.c_str());
    else if (!info.verified) LOG_WARN("Disc %s v%s has not been verified; continuing", info.serial.c_str(), info.version.c_str());
    info.key = gt3r::short_serial(info.serial);
    if (info.key.empty()) info.key = "disc-" + info.digest.substr(0, 8);

    disc->image_ = std::make_unique<ArchiveImage>(image, std::move(files));
    for (size_t i = 0; i < disc->image_->files().size(); ++i) disc->by_path_[disc->image_->files()[i].path] = i;
    disc->volume_ = std::make_unique<gt3r::Volume>(*disc->image_, 0, disc->image_->size());
    Disc* d = disc.get();
    disc->volume_->set_program_reader([d] { return d->read_program(); });
    disc->volume_->set_identity(info.key, info.serial);
    LOG_INFO("Opened %s (%s, %s v%s, %s, files %s)", path.c_str(), info.description.c_str(), info.serial.c_str(),
             info.version.c_str(), info.video.c_str(), info.digest.c_str());
    return disc;
}

const ImageFile* Disc::file(const std::string& path) const {
    const auto it = by_path_.find(path);
    return it == by_path_.end() ? nullptr : &image_->files()[it->second];
}

Bytes Disc::read_program() {
    const auto core = find_file("CORE.GT4");
    if (!core) throw FormatError("CORE.GT4 missing");
    return core->first->read(core->second);
}

namespace {

std::shared_ptr<const gt3r::CoreImage> unpack(const Bytes& core) { return std::make_shared<const gt3r::CoreImage>(gt3r::CoreImage::decode(core)); }

} // namespace

void Disc::prepare_program() {
    std::lock_guard lock(program_mutex_);
    if (program_.valid()) return;
    try {
        program_ = std::async(std::launch::async, [core = read_program()] { return unpack(core); }).share();
    } catch (const std::exception&) {
        // (no program, or no thread: program_image() says so, or does it)
    }
}

std::shared_ptr<const gt3r::CoreImage> Disc::program_image() {
    std::shared_future<std::shared_ptr<const gt3r::CoreImage>> work;
    {
        std::lock_guard lock(program_mutex_);
        if (!program_.valid()) program_ = std::async(std::launch::deferred, [core = read_program()] { return unpack(core); }).share();
        work = program_;
    }
    return work.get();
}

std::vector<gt3r::Iso9660*> Disc::file_systems() const {
    std::vector<gt3r::Iso9660*> v;
    for (const auto& l : layers_) v.push_back(l.get());
    return v;
}

std::optional<std::pair<gt3r::Iso9660*, gt3r::Iso9660::Entry>> Disc::find_file(const std::string& path) const {
    for (gt3r::Iso9660* fs : file_systems())
        if (auto e = fs->find(path)) return std::make_pair(fs, *e);
    return std::nullopt;
}

} // namespace gt4r
