// The volume index readers and the image the shared engine reads through, on made-up volumes
// (no game data), then on the discs of GT4R_DISC.

#include "test_framework.h"

#include "formats/tex1.h"
#include "gt4r/disc/archive_image.h"
#include "gt4r/disc/archive_index.h"

#include <zlib.h>

#include <cstring>
#include <map>

using namespace gt4r;

namespace {

class MemoryReader : public gt3r::Reader {
public:
    explicit MemoryReader(Bytes data) : data_(std::move(data)) {}
    u64 size() const override { return data_.size(); }
    const std::string& path() const override { return path_; }
    void read_at(u64 offset, void* out, size_t len) override {
        if (offset > data_.size() || data_.size() - offset < len) throw FormatError("memory reader: past the end");
        std::memcpy(out, data_.data() + offset, len);
    }
    using gt3r::Reader::read_at;
    Bytes data_;

private:
    std::string path_ = "memory";
};

void put16(Bytes& b, size_t o, u16 v) { std::memcpy(b.data() + o, &v, 2); }
void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }

// A raw DEFLATE stream of `d`; `ended` false: without the end marker, as the game's index pages are.
Bytes deflate_raw(ByteSpan d, bool ended = true) {
    Bytes out(compressBound(uLong(d.size())) + 64);
    z_stream zs{};
    deflateInit2(&zs, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    zs.next_in = const_cast<Bytef*>(d.data()), zs.avail_in = uInt(d.size());
    zs.next_out = out.data(), zs.avail_out = uInt(out.size());
    deflate(&zs, ended ? Z_FINISH : Z_SYNC_FLUSH);
    out.resize(out.size() - zs.avail_out);
    deflateEnd(&zs);
    return out;
}

Bytes pack_file(ByteSpan d) {
    Bytes out(8);
    put32(out, 0, ArchiveIndex::kPackedMagic);
    put32(out, 4, u32(0) - u32(d.size()));
    const Bytes z = deflate_raw(d);
    out.insert(out.end(), z.begin(), z.end());
    return out;
}

Bytes pattern(size_t n, u8 seed) {
    Bytes b(n);
    for (size_t i = 0; i < n; ++i) b[i] = u8(seed + i * 7 + (i >> 8));
    return b;
}

// One index page: entries of {parent id, name, value bytes}.
struct Entry {
    u32 parent;
    std::string name;
    Bytes value;
};
Bytes index_page(s16 kind, const std::vector<Entry>& entries) {
    Bytes page(ArchiveIndex::kPage, 0);
    put16(page, 0, u16(kind));
    put16(page, 2, u16(entries.size() * 2));
    put32(page, 4, 0xFFFFFFFF), put32(page, 8, 0xFFFFFFFF);
    size_t at = 0x0C;
    for (size_t k = 0; k < entries.size(); ++k) {
        const Entry& e = entries[k];
        const size_t row = page.size() - 8 * (k + 1);
        put16(page, row, u16(at)), put16(page, row + 2, u16(4 + e.name.size()));
        page[at] = u8(e.parent >> 24), page[at + 1] = u8(e.parent >> 16), page[at + 2] = u8(e.parent >> 8), page[at + 3] = u8(e.parent);
        std::memcpy(page.data() + at + 4, e.name.data(), e.name.size());
        at += 4 + e.name.size();
        put16(page, row + 4, u16(at)), put16(page, row + 6, u16(e.value.size()));
        std::memcpy(page.data() + at, e.value.data(), e.value.size());
        at += e.value.size();
    }
    for (u8& b : page) b ^= 0x55;
    return deflate_raw(page, kind != 0); // (the disc's pages: one in 407 is ended)
}
Bytes folder_value(u32 id) {
    Bytes v(5, 0);
    put32(v, 1, id);
    return v;
}
Bytes file_value(u32 page, u32 size) {
    Bytes v(13, 0);
    v[0] = 1, put32(v, 1, page), put32(v, 5, 1100000000), put32(v, 9, size);
    return v;
}
Bytes packed_value(u32 page, u32 stored, u32 size) {
    Bytes v(17, 0);
    v[0] = 2, put32(v, 1, page), put32(v, 5, 1100000000), put32(v, 9, stored), put32(v, 13, size);
    return v;
}

} // namespace

TEST(paged_index_lists_files) {
    const Bytes plain = pattern(3000, 1), big = pattern(70000, 9);
    const Bytes packed = pack_file(big);
    // A volume: an unused front table's header, the index on page 3, then the files' pages.
    const u64 index_at = 3 * ArchiveIndex::kPage;
    const std::vector<Bytes> pages = {
        index_page(1, {}), // a search page: skipped
        index_page(0, {{0, ".", folder_value(1)}, {1, "crs", folder_value(2)}, {2, "bestline", folder_value(3)},
                       {1, "Top.TXT", file_value(0, u32(plain.size()))}}),
        index_page(0, {{3, "line0001", packed_value(2, u32(packed.size()), u32(big.size()))}, {2, "empty", file_value(0, 0)}}),
    };
    Bytes index(0x40 + 4 * (pages.size() + 1), 0);
    std::vector<u32> ends = {u32(index.size())};
    for (const Bytes& p : pages) index.insert(index.end(), p.begin(), p.end()), ends.push_back(u32(index.size()));
    put32(index, 0, ArchiveIndex::kMagic), put16(index, 4, 1), put16(index, 6, 3);
    put32(index, 8, u32(index.size()));
    const u32 index_pages = u32((index.size() + ArchiveIndex::kPage - 1) / ArchiveIndex::kPage);
    put32(index, 0x0C, index_pages);
    put16(index, 0x10, u16(ArchiveIndex::kPage)), put16(index, 0x12, u16(pages.size()));
    for (size_t i = 0; i < ends.size(); ++i) put32(index, 0x40 + i * 4, ends[i] ^ (0x14AC327Au * u32(i + 1)));
    const u64 data_at = index_at + u64(index_pages) * ArchiveIndex::kPage;
    Bytes vol(data_at + 2 * ArchiveIndex::kPage + packed.size(), 0);
    put32(vol, 0, ArchiveIndex::kMagic), put16(vol, 4, 2), put16(vol, 6, 2); // (an unused front table: no size)
    std::memcpy(vol.data() + index_at, index.data(), index.size());
    std::memcpy(vol.data() + data_at, plain.data(), plain.size());
    std::memcpy(vol.data() + data_at + 2 * ArchiveIndex::kPage, packed.data(), packed.size());

    // The volume sits at an offset of the disc image.
    const u64 base = 0x1800;
    Bytes disc(base, 0xEE);
    disc.insert(disc.end(), vol.begin(), vol.end());
    MemoryReader r(disc);
    CHECK(ArchiveIndex::find(r, base, vol.size()) == index_at);
    const ArchiveIndex ix = ArchiveIndex::read(r, base, vol.size());
    CHECK(ix.offset == index_at && ix.size == index.size() && ix.data_offset == data_at);
    CHECK(ix.folders == 3 && ix.files.size() == 3);
    std::map<std::string, ArchiveFile> by;
    for (const ArchiveFile& f : ix.files) by[f.path] = f;
    CHECK(by.count("Top.TXT") && by["Top.TXT"].size == plain.size() && !by["Top.TXT"].packed && by["Top.TXT"].offset == data_at);
    CHECK(by.count("crs/empty") && by["crs/empty"].size == 0);
    const ArchiveFile& line = by["crs/bestline/line0001"];
    CHECK(line.packed && line.size == big.size() && line.stored_size == packed.size() && line.modified == 1100000000);
    CHECK(unpack_file(r.read_at(base + line.offset, line.stored_size), line.size) == big);

    // A volume without one, and a file past the volume's end.
    CHECK(!ArchiveIndex::find(r, base, index_at));
    bool threw = false;
    try {
        ArchiveIndex::read(r, base, vol.size() - 16);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(image_presents_files_plain) {
    // A disc with three files' stored bytes; the image presents them as one plain volume.
    const Bytes a = pattern(5000, 3), b = pattern(100000, 5), c = pattern(1, 7);
    const Bytes b_packed = pack_file(b);
    Bytes disc(0x300, 0xAA);
    std::vector<ImageFile> files;
    auto add = [&](const std::string& path, const Bytes& stored, u32 size, bool packed) {
        files.push_back({path, disc.size(), size, u32(stored.size()), packed, 0});
        disc.insert(disc.end(), stored.begin(), stored.end());
        disc.resize(disc.size() + 37, 0xBB);
    };
    add("projects/arcade/US/CarRoot.gpb", a, u32(a.size()), false);
    add("crs/akasaka", b_packed, u32(b.size()), true);
    add("crs/none", {}, 0, false);
    add("photo/000.JPG", c, 1, false);
    MemoryReader r(disc);
    ArchiveImage image(r, files);
    CHECK(image.size() % ArchiveImage::kSector == 0);

    // Its table reads back as the same files, every one plain.
    const std::vector<ArchiveFile> listed = read_front_table(image, 0, image.size());
    CHECK(listed.size() == files.size());
    std::map<std::string, ArchiveFile> by;
    for (const ArchiveFile& f : listed) by[f.path] = f;
    for (const ImageFile& f : files) {
        CHECK(by.count(f.path) && by[f.path].size == f.size && !by[f.path].packed);
        if (f.path == "crs/akasaka") CHECK(image.read_at(by[f.path].offset, f.size) == b);
        if (f.path == "projects/arcade/US/CarRoot.gpb") CHECK(image.read_at(by[f.path].offset, f.size) == a);
    }
    // Reads across a file's end give the padding as zeros, then the next file.
    const u64 at = by["crs/akasaka"].offset;
    const Bytes tail = image.read_at(at + b.size() - 4, 4 + 16);
    CHECK(std::memcmp(tail.data(), b.data() + b.size() - 4, 4) == 0);
    for (size_t i = 4; i < tail.size(); ++i) CHECK(tail[i] == 0);
    const Bytes whole = image.read_at(0, size_t(image.size()));
    CHECK(std::memcmp(whole.data() + by["photo/000.JPG"].offset, c.data(), 1) == 0);
    CHECK(std::memcmp(whole.data() + at, b.data(), b.size()) == 0);

    // The shared engine's volume over it: paths as stored, contents, ranges, folders.
    gt3r::Volume vol(image, 0, image.size());
    CHECK(vol.entries().size() == files.size());
    const gt3r::Volume::Entry* e = vol.find("crs/akasaka");
    CHECK(e && !e->compressed && vol.read(*e) == b);
    Bytes part(1000);
    vol.read_range(*e, 65000, part.data(), part.size());
    CHECK(std::memcmp(part.data(), b.data() + 65000, part.size()) == 0);
    CHECK(vol.find("projects/arcade/US/CarRoot.gpb") && vol.read(*vol.find("projects/arcade/US/CarRoot.gpb")) == a);
    CHECK(vol.find("crs/none") && vol.read(*vol.find("crs/none")).empty());
    const std::vector<std::string> top = vol.list_dir("");
    CHECK(top == (std::vector<std::string>{"crs/", "photo/", "projects/"}));
    CHECK(vol.list_dir("crs") == (std::vector<std::string>{"akasaka", "none"}));
}

TEST(disc_files_read_whole) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        const DiscInfo& info = disc->info();
        std::printf("  %s %s: %zu files, files %s\n", info.serial.c_str(), game_name(info.game), disc->files().size(), info.digest.c_str());
        CHECK(info.problem.empty());
        CHECK(!disc->files().empty() && disc->volume().entries().size() == disc->files().size());
        // Every packed file starts as a packed file does and says its size.
        size_t packed = 0, bad = 0;
        for (const ImageFile& f : disc->files()) {
            if (!f.packed) continue;
            ++packed;
            ImageFile head = f;
            head.stored_size = 8;
            const Bytes h = disc->read_stored(head);
            if (rd32(h, 0) != ArchiveIndex::kPackedMagic || u32(0) - rd32(h, 4) != f.size) ++bad;
        }
        CHECK(packed > 0 && bad == 0);
        // Every standalone texture set under advertise/ and race/ decodes through the volume.
        size_t sets = 0;
        for (const gt3r::Volume::Entry& e : disc->volume().entries()) {
            if (e.path.compare(0, 10, "advertise/") != 0 && e.path.compare(0, 5, "race/") != 0) continue;
            const Bytes data = disc->volume().read(e);
            CHECK(data.size() == e.size);
            for (size_t off : gt3r::find_tex1_sets(data)) {
                gt3r::Tex1::parse(ByteSpan(data).subspan(off));
                ++sets;
            }
        }
        CHECK(sets > 0);
        // The program unpacks.
        CHECK(disc->read_program().size() > 1000000);
    }
}

TEST(known_discs_are_recognised) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    for (const auto& disc : test::discs()) {
        const DiscInfo& info = disc->info();
        if (info.serial == "SCUS_973.28") {
            CHECK(info.game == Game::GT4 && info.verified);
            CHECK(disc->volumes().size() == 2 && disc->volumes()[0].paged && disc->volumes()[1].paged);
            CHECK(disc->files().size() == 18419); // 18,352 in GT4.VOL and the 67 movies of GT4L1.VOL
            CHECK(disc->volume().find("mpeg/main/start_game.pss") && disc->volume().find("projects/GT4Application.adc"));
        } else if (info.serial == "SCPS_150.55") {
            CHECK(info.game == Game::Prologue && info.verified);
            CHECK(disc->volumes().size() == 1 && !disc->volumes()[0].paged);
            CHECK(disc->files().size() == 2592);
        }
    }
}
