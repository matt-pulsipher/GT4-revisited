// Gran Turismo 4's program tables as values; see program_tables.h. Each table area is a struct with named fields packed back
// into its bytes in field order (program_tables_values.inc, the field comments give each one's offset, its address in
// SCUS-97328 v1.01 and what reads it; a field named other_* is one GT4R does not read).

#include "gt4r/sim/program_tables.h"

#include "base/bytes.h"
#include "base/ee_float.h"
#include "gt4r/sim/program_code.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace gt4r::sim {

namespace {

using F = gt3r::ee::F;
using gt3r::s8;
using gt3r::s16;

// The tables' bytes in order (little-endian words, floats as their bits).
class Pack {
public:
    Pack& operator()(u8 v) {
        b.push_back(v);
        return *this;
    }
    Pack& operator()(s8 v) { return (*this)(u8(v)); }
    Pack& operator()(u16 v) { return (*this)(u8(v))(u8(v >> 8)); }
    Pack& operator()(s16 v) { return (*this)(u16(v)); }
    Pack& operator()(u32 v) { return (*this)(u16(v))(u16(v >> 16)); }
    Pack& operator()(F v) { return (*this)(u32(v.bits)); }
    template <class T, size_t N> Pack& operator()(const std::array<T, N>& a) {
        for (const T& x : a) (*this)(x);
        return *this;
    }
    std::vector<u8> b;
};

#include "gt4r/sim/program_tables_values.inc"

struct Store {
    std::vector<std::vector<u8>> bytes;
    std::vector<ProgramTable> tables;
};

constexpr size_t kReleases = 3;

// A release's own values over v1.01's (each at its v1.01 address; docs/specs/physics.md "The releases").
struct Own {
    Release release;
    u32 address;
    std::vector<u8> bytes;
};
const std::vector<Own>& own_values() {
    static const std::vector<Own> v = {
        // Japan: the engine condition's oil, old from 5,000 km (the static block +0xD7: 50 x 100,000 m; v1.01 60).
        {Release::Japan, 0x620DAF, {50}},
        // Japan: the static block has no +0x14E .. +0x155 (v1.01's 100 x 5, 0, 0, 4; Europe's spec builder reads +0x153 ..).
        {Release::Japan, 0x620E26, {0, 0, 0, 0, 0, 0, 0, 0}},
    };
    return v;
}
// Bytes of ours a release's table lacks (served as above, absent from that program's table).
struct Gap {
    Release release;
    u32 address, size;
};
constexpr Gap kGaps[] = {{Release::Japan, 0x620E26, 8}};
// Step counts (bytes) a release keeps at its own step rate: `count` of them `stride` bytes apart. Served at 60 steps a
// second (the same in seconds; 0 and 255 are no count and for ever); the program holds them x 50 / 60.
struct Rate {
    Release release;
    u32 address, count, stride;
};
constexpr Rate kRates[] = {
    {Release::Europe, 0x6207E4, 15, 8}, // the pit messages' times
    {Release::Europe, 0x620880, 16, 1}, // special car 1's moods' times
};
bool rated(Release r, u32 a) {
    for (const Rate& x : kRates)
        if (x.release == r && a >= x.address && (a - x.address) % x.stride == 0 && (a - x.address) / x.stride < x.count) return true;
    return false;
}
u32 gap_before(Release r, u32 table, u32 size, u32 a, bool* inside) {
    u32 n = 0;
    *inside = false;
    for (const Gap& g : kGaps) {
        if (g.release != r || g.address < table || g.address >= table + size) continue;
        if (a >= g.address + g.size) n += g.size;
        else if (a >= g.address) *inside = true;
    }
    return n;
}

template <class T> std::vector<u8> packed(const T& v, u32 size) {
    Pack p;
    pack(p, v);
    if (p.b.size() != size) throw std::logic_error("program_tables: a table packs to the wrong size");
    return std::move(p.b);
}

Store build(Release release) {
    Store s;
    struct Item {
        const char* what;
        u32 address;
        std::vector<u8> bytes;
    };
    std::vector<Item> items;
#define GT4R_TABLE_ITEM(name, what, address, size) items.push_back({what, address, packed(k##name, size)});
    GT4R_PROGRAM_TABLES(GT4R_TABLE_ITEM)
#undef GT4R_TABLE_ITEM
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.address < b.address; });
    for (const Own& o : own_values()) {
        if (o.release != release) continue;
        bool placed = false;
        for (Item& i : items)
            if (o.address >= i.address && o.address + o.bytes.size() <= i.address + i.bytes.size()) {
                std::copy(o.bytes.begin(), o.bytes.end(), i.bytes.begin() + (o.address - i.address));
                placed = true;
            }
        if (!placed) throw std::logic_error("program_tables: a release's value outside every table");
    }
    s.bytes.reserve(items.size());
    for (Item& i : items) s.bytes.push_back(std::move(i.bytes));
    for (size_t k = 0; k < items.size(); ++k) {
        s.tables.push_back({items[k].what, items[k].address, ByteSpan(s.bytes[k].data(), s.bytes[k].size())});
        if (k && s.tables[k - 1].address + s.tables[k - 1].bytes.size() > s.tables[k].address)
            throw std::logic_error("program_tables: two tables overlap");
    }
    return s;
}

const Store& store(Release r = Release::Us) {
    static const std::array<Store, kReleases> s = {build(Release::Us), build(Release::Europe), build(Release::Japan)};
    return s[size_t(r)];
}

// FNV-1a over a stretch of the image (raw bytes: SCUS-97328 v1.01's routines that address the tables, recognized as they
// are there: the spec builder, its wrapper with the fix-ups and the converter).
u64 fnv(ByteSpan image, u32 base, u32 from, u32 to) {
    if (from < base || to > base + image.size() || from >= to) return 0;
    u64 h = 0xcbf29ce484222325ull;
    for (u32 a = from; a < to; ++a) h = (h ^ image[a - base]) * 0x100000001b3ull;
    return h;
}
struct RawRoutine {
    u32 from, to;
    u64 hash;
};
constexpr RawRoutine kReference[] = {
    {0x4413C8, 0x44194C, 0x74b88b8726c105c9ull},
    {0x3F3B68, 0x3F4078, 0xca526b3daf3e7935ull},
    {0x361908, 0x362DF8, 0xddee4b2a031785e5ull},
};
// The same routines as the other releases have them (their places in each; raw bytes).
struct ReleaseCode {
    Release release;
    std::array<RawRoutine, 3> routines;
};
constexpr ReleaseCode kReleaseCode[] = {
    {Release::Us, {{{0x4413C8, 0x44194C, 0x74b88b8726c105c9ull}, {0x3F3B68, 0x3F4078, 0xca526b3daf3e7935ull}, {0x361908, 0x362DF8, 0xddee4b2a031785e5ull}}}},
    {Release::Europe, {{{0x441740, 0x441CC4, 0xbae17f389e21b0ccull}, {0x3F3E70, 0x3F43EC, 0x12aa5320befbb985ull}, {0x361B10, 0x3630A8, 0x4db9ea48a242704cull}}}},
    {Release::Japan, {{{0x439C50, 0x43A1D4, 0x1e413c8296cb8677ull}, {0x3EC878, 0x3ECD8C, 0xb561ef2fb5d26788ull}, {0x35BEC8, 0x35D340, 0x4a682d9056b07156ull}}}},
};

// Where another build has each table: the routines that address it in SCUS-97328 v1.01, by the hash of their masked code
// (program_code.h), their length in instructions, the instruction that addresses the table and the table's byte it
// addresses (the site; the first that is found once and decodes serves).
struct Site {
    u64 hash;
    u32 words, index, offset;
};
struct Sites {
    u32 table;
    std::array<Site, 3> sites;
};
constexpr Sites kSites[] = {
    {0x620218, {{{0x89a163708dcbff78ull, 16, 12, 0x24u}, {0xf86c976fdb6b2394ull, 19, 10, 0x24u}, {0x04941f9f4757e66bull, 19, 8, 0x24u}}}},
    {0x6203C8, {{{0x6c91c238b0020f8bull, 10, 7, 0xB0u}, {0x67bc4e2838a65664ull, 11, 8, 0xB0u}, {0xa30e627ef32d6ae8ull, 12, 6, 0x4C8u}}}},
    {0x620AD8, {{{0xe953ca0a463a179dull, 14, 8, 0x28u}, {0x546bce471a097708ull, 24, 1, 0x33Cu}, {0x15e86db0b91dbbd2ull, 27, 10, 0x28u}}}},
    {0x620E70, {{{0x6e116e9d864aa31bull, 16, 11, 0xFFFFFFFCu}}}},
    {0x622080, {{{0x6d148b24bf3ebc8dull, 33, 3, 0x90u}, {0x0f00ccb1988e2facull, 39, 1, 0x8Cu}, {0xda585797698cdab1ull, 40, 3, 0x90u}}}},
    {0x622420, {{{0x2eac7713bebabc13ull, 10, 1, 0x88u}, {0x6604842fb78ff438ull, 11, 1, 0x88u}, {0x37d20800687a610bull, 20, 11, 0x88u}}}},
    {0x622A20, {{{0x6172833cecf88c10ull, 36, 13, 0x28u}}}},
    {0x623DC0, {{{0x443ac74b12c4b72eull, 24, 8, 0x94u}, {0xf78606ee73beb123ull, 29, 25, 0x0u}, {0x61976201b9e49e19ull, 45, 12, 0x270u}}}},
    {0x655888, {{{0x465a56cf569c5da8ull, 28, 7, 0x400u}, {0x161dc2ab738c0e7eull, 72, 26, 0x408u}, {0xa4dac7259b5f8927ull, 77, 20, 0x408u}}}},
    {0x69F4C8, {{{0x6d3c227bb0df1250ull, 85, 11, 0x0u}, {0xb915084677ddc5e1ull, 85, 11, 0x0u}}}}, // (the second: Europe's, its step 1/50 s)
    {0x6A4028, {{{0xc07c7b9bd538c0aeull, 325, 144, 0x0u}, {0xaf7a0c1e51b43c71ull, 351, 170, 0x0u}}}}, // (Europe's: its inertia floors)
    {0x6A6188, {{{0xfdcaf8d971050728ull, 46, 10, 0x0u}, {0xf894bcfbd6b8152dull, 122, 6, 0x0u}}}},
    {0x6A6910, {{{0x157af9341b107e38ull, 49, 9, 0x7Cu}, {0x8fab90df3dc002b3ull, 92, 41, 0x0u}}}},
    {0x6A6EF0, {{{0x94a37a4e94c1c969ull, 4, 3, 0x0u}}}},
    {0x6A7C88, {{{0x6a95a99a900dd7d9ull, 8, 2, 0x0u}}}},
};

} // namespace

const char* release_name(Release r) {
    switch (r) {
    case Release::Europe: return "SCES-51719 v1.00";
    case Release::Japan: return "SCPS-17001 v1.05";
    default: return "SCUS-97328 v1.01";
    }
}

std::optional<Release> release_of(ByteSpan image, u32 load) {
    for (const ReleaseCode& c : kReleaseCode) {
        bool all = true;
        for (const RawRoutine& r : c.routines) all = all && fnv(image, load, r.from, r.to) == r.hash;
        if (all) return c.release;
    }
    return std::nullopt;
}

std::span<const ProgramTable> gt4_tables(Release r) { return store(r).tables; }

const ProgramTable* gt4_table_at(u32 a, Release r) {
    const std::vector<ProgramTable>& t = store(r).tables;
    auto it = std::upper_bound(t.begin(), t.end(), a, [](u32 x, const ProgramTable& p) { return x < p.address; });
    if (it == t.begin()) return nullptr;
    --it;
    return it->contains(a) ? &*it : nullptr;
}

u64 gt4_tables_hash(Release r) {
    u64 h = 1469598103934665603ull;
    auto mix = [&](u8 c) { h = (h ^ c) * 1099511628211ull; };
    for (const ProgramTable& t : gt4_tables(r)) {
        for (int k = 0; k < 4; ++k) mix(u8(t.address >> (8 * k)));
        for (u8 c : t.bytes) mix(c);
    }
    return h;
}

size_t gt4_release_bytes(Release r) {
    size_t n = 0;
    const std::span<const ProgramTable> a = gt4_tables(Release::Us), b = gt4_tables(r);
    for (size_t t = 0; t < a.size(); ++t)
        for (size_t k = 0; k < a[t].bytes.size(); ++k) n += a[t].bytes[k] != b[t].bytes[k];
    return n;
}

bool reference_build(ByteSpan image, u32 load) {
    for (const RawRoutine& r : kReference)
        if (fnv(image, load, r.from, r.to) != r.hash) return false;
    return true;
}

size_t TableCheck::not_found() const {
    return size_t(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.found == 0; }));
}
size_t TableCheck::differ() const {
    return size_t(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.found != 0 && r.differing != 0; }));
}

std::vector<std::string> TableCheck::lines(bool every) const {
    std::vector<std::string> out;
    char buf[320];
    for (const Row& r : rows) {
        if (r.found && !r.differing && !every) continue;
        char where[64] = "";
        if (r.routine) std::snprintf(where, sizeof where, "its routine 0x%06X", r.routine);
        else std::snprintf(where, sizeof where, "%s", r.how);
        if (!r.found) std::snprintf(buf, sizeof buf, "%s (0x%06X in SCUS-97328 v1.01): not found in this program", r.table->what, r.table->address);
        else if (r.differing)
            std::snprintf(buf, sizeof buf, "%s (0x%06X in SCUS-97328 v1.01): at 0x%06X (%s), %u of %zu bytes unlike ours, the first at +0x%X", r.table->what,
                          r.table->address, r.found, where, r.differing, r.table->bytes.size(), r.first_differing);
        else std::snprintf(buf, sizeof buf, "%s (0x%06X in SCUS-97328 v1.01): at 0x%06X (%s), equal", r.table->what, r.table->address, r.found, where);
        out.push_back(buf);
    }
    u32 relocated = 0, scaled = 0, gap = 0;
    for (const Row& r : rows) relocated += r.relocated, scaled += r.scaled, gap += r.gap;
    std::string who = release ? std::string(release_name(*release)) + "'s program" : std::string("another build's program (SCUS-97328 v1.01's tables)");
    std::snprintf(buf, sizeof buf, "%s: %zu of %zu tables equal to its own, %zu differ, %zu not found (%u address words its own, %u step counts at its step rate, %u bytes it lacks)",
                  who.c_str(), rows.size() - differ() - not_found(), rows.size(), differ(), not_found(), relocated, scaled, gap);
    out.push_back(buf);
    return out;
}

TableCheck check_tables(ByteSpan image, u32 load, bool locate) {
    TableCheck c;
    c.reference = reference_build(image, load);
    c.release = release_of(image, load);
    const Release rel = c.release.value_or(Release::Us);
    std::unique_ptr<ProgramCode> code;
    if (!c.reference || locate) code = std::make_unique<ProgramCode>(image, load);
    auto at = [&](u32 a) -> u32 { return a >= load && a - load + 4 <= image.size() ? gt3r::rd32(image, a - load) : 0; };
    auto address_word = [](u32 v) { return v % 4 == 0 && v >= 0x100000 && v < 0x1000000; };
    for (const ProgramTable& t : gt4_tables(rel)) {
        TableCheck::Row r;
        r.table = &t;
        r.how = "not found";
        if (!code) r.found = t.address, r.how = "v1.01's place";
        else
            for (const Sites& s : kSites)
                if (s.table == t.address)
                    for (const Site& site : s.sites) {
                        if (!site.words || r.found) continue;
                        const std::vector<u32> places = code->find(site.hash, site.words);
                        const u32 a = places.size() == 1 ? code->address_in(places[0], site.index, site.offset) : 0;
                        if (a && a >= load && a - load + t.bytes.size() <= image.size()) r.found = a, r.routine = places[0], r.how = "its routine";
                    }
        if (r.found) {
            for (u32 k = 0; k < t.bytes.size(); ++k) {
                bool inside = false;
                const u32 skip = gap_before(rel, t.address, u32(t.bytes.size()), t.address + k, &inside);
                if (inside) {
                    ++r.gap;
                    continue;
                }
                const u32 a = r.found + k - skip;
                const u8 v = a >= load && a - load < image.size() ? image[a - load] : 0;
                u8 want = t.bytes[k];
                if (rated(rel, t.address + k) && want != 0 && want != 255) {
                    if (want * 50 % 60 == 0 && v == want * 50 / 60) {
                        ++r.scaled;
                        continue;
                    }
                }
                if (v == want) continue;
                // An address word: the program's own address where ours holds v1.01's (any build's data moved).
                const u32 w0 = (t.address + k) & ~3u;
                if (w0 >= t.address && w0 + 4 <= t.address + t.bytes.size()) {
                    const u32 ours = u32(t.bytes[w0 - t.address]) | u32(t.bytes[w0 - t.address + 1]) << 8 | u32(t.bytes[w0 - t.address + 2]) << 16 |
                                     u32(t.bytes[w0 - t.address + 3]) << 24;
                    const u32 theirs = at(r.found + (w0 - t.address) - skip);
                    if (address_word(ours) && address_word(theirs)) {
                        if (k % 4 == 0) ++r.relocated;
                        continue;
                    }
                }
                if (!r.differing) r.first_differing = k;
                ++r.differing;
            }
        }
        c.rows.push_back(r);
    }
    return c;
}

} // namespace gt4r::sim
