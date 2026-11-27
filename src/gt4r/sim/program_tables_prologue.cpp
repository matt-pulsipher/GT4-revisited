// Gran Turismo 4 Prologue's program tables as values; see program_tables_prologue.h.

#include "gt4r/sim/program_tables_prologue.h"

#include "base/ee_float.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace gt4r::sim {

namespace {

using F = gt3r::ee::F;
using gt3r::s8;
using gt3r::s16;
using gt3r::s32;

// The areas' bytes in order (little-endian words, floats as their bits).
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
    Pack& operator()(s32 v) { return (*this)(u32(v)); }
    Pack& operator()(F v) { return (*this)(u32(v.bits)); }
    template <class T, size_t N> Pack& operator()(const std::array<T, N>& a) {
        for (const T& x : a) (*this)(x);
        return *this;
    }
    std::vector<u8> b;
};

#include "gt4r/sim/program_tables_prologue_values.inc"

struct Store {
    std::vector<std::vector<u8>> bytes;
    std::vector<ProgramTable> tables;
};

template <class T> std::vector<u8> packed(const T& v, u32 size) {
    Pack p;
    pack(p, v);
    if (p.b.size() != size) throw std::logic_error("program_tables_prologue: an area packs to the wrong size");
    return std::move(p.b);
}

const Store& store() {
    static const Store s = [] {
        Store s;
        std::vector<std::pair<ProgramTable, std::vector<u8>>> items;
#define GT4R_TABLE_ITEM(name, what, address, size) items.push_back({{what, address, {}}, packed(k##name, size)});
        GT4R_PROLOGUE_TABLES(GT4R_TABLE_ITEM)
#undef GT4R_TABLE_ITEM
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first.address < b.first.address; });
        s.bytes.reserve(items.size());
        for (auto& i : items) s.bytes.push_back(std::move(i.second));
        for (size_t k = 0; k < items.size(); ++k) {
            s.tables.push_back({items[k].first.what, items[k].first.address, ByteSpan(s.bytes[k].data(), s.bytes[k].size())});
            if (k && s.tables[k - 1].address + s.tables[k - 1].bytes.size() > s.tables[k].address)
                throw std::logic_error("program_tables_prologue: two areas overlap");
        }
        return s;
    }();
    return s;
}

} // namespace

std::span<const ProgramTable> prologue_tables() { return store().tables; }

const ProgramTable* prologue_table_at(u32 a) {
    const auto& t = store().tables;
    auto it = std::upper_bound(t.begin(), t.end(), a, [](u32 x, const ProgramTable& p) { return x < p.address; });
    if (it == t.begin()) return nullptr;
    --it;
    return it->contains(a) ? &*it : nullptr;
}

u64 prologue_tables_hash() {
    u64 h = 0xcbf29ce484222325ull;
    const auto mix = [&](u8 v) { h = (h ^ v) * 0x100000001b3ull; };
    for (const ProgramTable& t : prologue_tables()) {
        for (int k = 0; k < 4; ++k) mix(u8(t.address >> (8 * k)));
        for (const u8 v : t.bytes) mix(v);
    }
    return h;
}

TableCheck check_prologue_tables(ByteSpan image, u32 load, bool recognized) {
    TableCheck c;
    for (const ProgramTable& t : prologue_tables()) {
        TableCheck::Row r;
        r.table = &t;
        const bool inside = t.address >= load && t.address - load + t.bytes.size() <= image.size();
        if (recognized && inside) {
            r.found = t.address, r.how = "SCPS-15055 v1.01's place";
            for (u32 k = 0; k < t.bytes.size(); ++k)
                if (image[t.address - load + k] != t.bytes[k]) {
                    if (!r.differing) r.first_differing = k;
                    ++r.differing;
                }
        } else
            r.how = "not found";
        c.rows.push_back(r);
    }
    return c;
}

const std::array<PrologueLessonRule, 36>& prologue_lesson_rules() {
    static const std::array<PrologueLessonRule, 36> k = {{
        {50, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 1
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 2
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 3
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 4
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 5
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 6
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 7
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 8
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 9
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 10
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 11
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 12
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 13
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 14
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 15
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 16
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 17
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 18
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 19
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 20
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 21
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 22
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 23
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 24
        {0, 0, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 25
        {0, 0, {94, 0, 0, 0, 0}, {-37, 0, 0, 0, 0}}, // 26
        {0, 0, {94, 0, 0, 0, 0}, {-32, 0, 0, 0, 0}}, // 27
        {0, 0, {50, 0, 0, 0, 0}, {-26, 0, 0, 0, 0}}, // 28
        {0, 0, {100, 0, 0, 0, 0}, {200, 0, 0, 0, 0}}, // 29
        {0, 0, {170, 0, 0, 0, 0}, {400, 0, 0, 0, 0}}, // 30
        {0, 0, {80, 0, 0, 0, 0}, {400, 0, 0, 0, 0}}, // 31
        {0, 0, {80, 80, 80, 0, 0}, {50, 0, -35, 0, 0}}, // 32
        {0, 0, {150, 150, 150, 0, 0}, {160, 75, -65, 0, 0}}, // 33
        {0, 0, {80, 80, 80, 0, 0}, {450, 230, 190, 0, 0}}, // 34
        {0, 0, {80, 80, 80, 80, 80}, {1950, 1500, 1200, 980, 600}}, // 35
        {0, 100, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}, // 36
    }};
    return k;
}

} // namespace gt4r::sim
