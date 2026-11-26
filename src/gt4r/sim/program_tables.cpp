// Gran Turismo 4's program tables as values; see program_tables.h. Each table area is a struct with named fields packed back
// into its bytes in field order (program_tables_values.inc, the field comments give each one's offset, its address in
// SCUS-97328 v1.01 and what reads it; a field named other_* is one GT4R does not read).

#include "gt4r/sim/program_tables.h"

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

template <class T> std::vector<u8> packed(const T& v, u32 size) {
    Pack p;
    pack(p, v);
    if (p.b.size() != size) throw std::logic_error("program_tables: a table packs to the wrong size");
    return std::move(p.b);
}

Store build() {
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
    s.bytes.reserve(items.size());
    for (Item& i : items) s.bytes.push_back(std::move(i.bytes));
    for (size_t k = 0; k < items.size(); ++k) {
        s.tables.push_back({items[k].what, items[k].address, ByteSpan(s.bytes[k].data(), s.bytes[k].size())});
        if (k && s.tables[k - 1].address + s.tables[k - 1].bytes.size() > s.tables[k].address)
            throw std::logic_error("program_tables: two tables overlap");
    }
    return s;
}

const Store& store() {
    static const Store s = build();
    return s;
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
    {0x69F4C8, {{{0x6d3c227bb0df1250ull, 85, 11, 0x0u}}}},
    {0x6A4028, {{{0xc07c7b9bd538c0aeull, 325, 144, 0x0u}}}},
    {0x6A6188, {{{0xfdcaf8d971050728ull, 46, 10, 0x0u}, {0xf894bcfbd6b8152dull, 122, 6, 0x0u}}}},
    {0x6A6910, {{{0x157af9341b107e38ull, 49, 9, 0x7Cu}, {0x8fab90df3dc002b3ull, 92, 41, 0x0u}}}},
    {0x6A6EF0, {{{0x94a37a4e94c1c969ull, 4, 3, 0x0u}}}},
};

} // namespace

std::span<const ProgramTable> gt4_tables() { return store().tables; }

const ProgramTable* gt4_table_at(u32 a) {
    const std::vector<ProgramTable>& t = store().tables;
    auto it = std::upper_bound(t.begin(), t.end(), a, [](u32 x, const ProgramTable& p) { return x < p.address; });
    if (it == t.begin()) return nullptr;
    --it;
    return it->contains(a) ? &*it : nullptr;
}

u64 gt4_tables_hash() {
    u64 h = 1469598103934665603ull;
    auto mix = [&](u8 c) { h = (h ^ c) * 1099511628211ull; };
    for (const ProgramTable& t : gt4_tables()) {
        for (int k = 0; k < 4; ++k) mix(u8(t.address >> (8 * k)));
        for (u8 c : t.bytes) mix(c);
    }
    return h;
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
    std::snprintf(buf, sizeof buf, "%s: %zu of %zu tables equal to ours, %zu differ, %zu not found",
                  reference ? "SCUS-97328 v1.01's program" : "another build's program", rows.size() - differ() - not_found(), rows.size(), differ(), not_found());
    out.push_back(buf);
    return out;
}

TableCheck check_tables(ByteSpan image, u32 load, bool locate) {
    TableCheck c;
    c.reference = reference_build(image, load);
    std::unique_ptr<ProgramCode> code;
    if (!c.reference || locate) code = std::make_unique<ProgramCode>(image, load);
    for (const ProgramTable& t : gt4_tables()) {
        TableCheck::Row r;
        r.table = &t;
        r.how = "not found";
        if (!code) r.found = t.address, r.how = "v1.01's place";
        else
            for (const Sites& s : kSites)
                if (s.table == t.address)
                    for (const Site& site : s.sites) {
                        if (!site.words || r.found) continue;
                        const std::vector<u32> at = code->find(site.hash, site.words);
                        const u32 a = at.size() == 1 ? code->address_in(at[0], site.index, site.offset) : 0;
                        if (a && a >= load && a - load + t.bytes.size() <= image.size()) r.found = a, r.routine = at[0], r.how = "its routine";
                    }
        if (r.found) {
            for (u32 k = 0; k < t.bytes.size(); ++k) {
                const u32 a = r.found + k;
                const u8 v = a >= load && a - load < image.size() ? image[a - load] : 0;
                if (v != t.bytes[k]) {
                    if (!r.differing) r.first_differing = k;
                    ++r.differing;
                }
            }
        }
        c.rows.push_back(r);
    }
    return c;
}

} // namespace gt4r::sim
