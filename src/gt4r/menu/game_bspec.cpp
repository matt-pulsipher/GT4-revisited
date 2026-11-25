// The B-spec driver's book (game_bspec.h). Integer arithmetic as the program's (32-bit products and
// quotients truncated toward zero; the total's sum through a 64-bit product).

#include "gt4r/menu/game_bspec.h"

#include <algorithm>

namespace gt4r::menu::bspec {

namespace {

u32 cell(const Book& b, int t, int r, int c) { return b[size_t(t * 64 + r * 8 + c)]; }
u32 cap(u32 v, u32 most) { return v <= most ? v : most; }
// A course's sum: the cell [7][0] of the second and third tables, scaled by 59160.
s32 course_sum(const Book& b, int t, s32 scale) { return s32(cell(b, t, 7, 0)) * scale / 0xE718; }

} // namespace

void class_range(int k, int& lo, int& hi) {
    lo = k < 5 ? 0 : k - 4;
    hi = k < 3 ? k + 5 : 8;
}

int car_experience(const Book& b, const Weights& w) {
    s32 sum = 0;
    for (int k = 0; k < 8; ++k) {
        int lo, hi;
        class_range(k, lo, hi);
        s32 got = 0, most = 0;
        for (int r = lo; r < hi; ++r) {
            const s32 wt = w[size_t(r)];
            got += s32(cap(cell(b, 0, r, k), 1024)) * wt + s32(cap(cell(b, 1, r, k), 1024)) * wt;
            most += 2 * (wt << 10);
        }
        sum += most ? 125 * got / most : 0;
    }
    return std::min(sum, 1000) / 10;
}

int course_experience(const Book& b, const Weights& w) {
    s32 sum = 0;
    for (int r = 0; r < 8; ++r) {
        int lo, hi;
        class_range(r, lo, hi);
        const s32 wt = w[size_t(r)];
        s32 got = 0, most = 0;
        for (int c = lo; c < hi; ++c) {
            got += s32(cap(cell(b, 0, r, c), 1024)) * wt + s32(cap(cell(b, 1, r, c), 1024)) * wt;
            most += 2 * (wt << 10);
        }
        sum += most ? 125 * got / most : 0;
    }
    const s32 v = (70 * sum + 20 * course_sum(b, 1, 1000) + 10 * course_sum(b, 2, 1000)) / 100;
    return std::min(v, 1000) / 10;
}

int battle_experience(const Book& b, const Weights& w) {
    s32 got = 0, most = 0;
    for (int r = 0; r < 8; ++r) {
        int lo, hi;
        class_range(r, lo, hi);
        const s32 wt = w[size_t(r)];
        for (int c = lo; c < hi; ++c) {
            got += s32(cap(cell(b, 2, r, c), 256)) * wt;
            most += wt << 8;
        }
    }
    const s32 own = most ? 1000 * got / most : 0;
    const s32 v = (70 * own + 30 * course_sum(b, 2, 1000)) / 100;
    return std::min(v, 1000) / 10;
}

int total_experience(const Book& b, const Weights& w) {
    s64 got = 0, most = 0;
    for (int r = 0; r < 8; ++r) {
        int lo, hi;
        class_range(r, lo, hi);
        const s32 wt = w[size_t(r)];
        for (int c = lo; c < hi; ++c) {
            const u32 battle = (cap(cell(b, 2, r, c), 256) * 4) & 0xFFFF;
            got += s32(cap(cell(b, 0, r, c), 1024)) * wt + s32(cap(cell(b, 1, r, c), 1024)) * wt + s32(cap(battle, 1024)) * wt;
            most += 3 * (wt << 10);
        }
    }
    // The program's sums are 32-bit; the product with 10000 is taken in 64 bits (0x5BA0D8).
    const s32 own = most ? s32((s64(s32(got)) * 10000) / s32(most)) : 0;
    const s32 v = (70 * own + 20 * course_sum(b, 1, 10000) + 10 * course_sum(b, 2, 10000)) / 100;
    return v < 10001 ? v : 10000;
}

std::array<u8, 0x40> race_record(const Book& b, int k, const u8* course_marks) {
    std::array<u8, 0x40> out{};
    for (int t = 0; t < 3; ++t)
        for (int r = 0; r < 8; ++r) {
            if (k == 0 && r == 7) continue;
            const u32 v = cell(b, t, r, k);
            out[size_t(t * 16 + r * 2)] = u8(v), out[size_t(t * 16 + r * 2 + 1)] = u8(v >> 8);
        }
    if (course_marks)
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < 4; ++i) out[size_t(0x30 + s * 8 + i * 2 + 1)] = course_marks[s * 4 + i];
    return out;
}

void write_back(Book& b, int k, const std::array<u8, 0x40>& record, bool doubled, int courses, u8* course_marks) {
    for (int t = 0; t < 3; ++t)
        for (int r = 0; r < 8; ++r) {
            if (k == 0 && r == 7) continue;
            u16& at = b[size_t(t * 64 + r * 8 + k)];
            s32 v = s32(record[size_t(t * 16 + r * 2)] | record[size_t(t * 16 + r * 2 + 1)] << 8);
            if (doubled) {
                v = s32(at) + 2 * (v - s32(at));
                if (0xFFFF < v) v = 0xFFFF;
            }
            at = u16(v);
        }
    if (!course_marks) return;
    for (int s = 0; s < courses && s < 2; ++s)
        for (int i = 0; i < 4; ++i) {
            const s32 old = course_marks[s * 4 + i];
            s32 v = record[size_t(0x30 + s * 8 + i * 2 + 1)];
            if (doubled) {
                v = old + 2 * (v - old);
                if (!(v < 0x100)) v = 0xFF;
            }
            u16& total = b[size_t((s + 1) * 64 + 7 * 8)];
            total = u16(total + (v - old));
            course_marks[s * 4 + i] = u8(v);
        }
}

} // namespace gt4r::menu::bspec
