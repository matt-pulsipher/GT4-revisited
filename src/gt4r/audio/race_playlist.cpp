#include "gt4r/audio/race_playlist.h"

#include <bit>
#include <cstring>

namespace gt4r::audio {

RacePlaylist RacePlaylist::in_order(size_t count, const std::vector<int>& pieces) {
    RacePlaylist p(count);
    int order = 0;
    for (int piece : pieces)
        if (piece >= 0 && size_t(piece) < count && !p.rows_[size_t(piece)].enabled) p.rows_[size_t(piece)].enabled = true, p.rows_[size_t(piece)].order = order++;
    p.reset_played();
    return p;
}

u32 RacePlaylist::random_step(u32& state) {
    state = state * 17u + 17u;
    return state ^ (state << 16 | state >> 16);
}

float RacePlaylist::random_float(u32 a) {
    if (a == 0) return 0.0f;
    // The exponent from the leading bits (the sign bit set: the top); the mantissa the bits two
    // places under the leading one, as the program shifts them.
    const int lead = (a & 0x80000000u) ? -1 : (std::countl_zero(a) - 1) & 0x1F;
    const u32 exponent = u32(0x7D - lead);
    const u32 mantissa = (a << ((0u - exponent) & 31u)) >> 9;
    const u32 bits = exponent << 23 | mantissa;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void RacePlaylist::reset_played() {
    bool none = true;
    for (Row& r : rows_) {
        r.played = !r.enabled;
        if (r.enabled) none = false;
    }
    none_ = none;
}

void RacePlaylist::build() {
    reset_played();
    position_ = 0;
    if (!shuffle_) {
        int order = 0;
        for (Row& r : rows_) r.order = r.enabled ? order++ : -1;
        return;
    }
    for (Row& r : rows_) r.order = -1;
    u32 state = seed_;
    const size_t count = rows_.size();
    int order = 0;
    for (size_t pick = 0; pick < count; ++pick) {
        // A random row, then the first enabled one without a place from there on (wrapping); when
        // there is none the row the search ends on takes the place if it has none.
        const float x = random_float(random_step(state)) * float(count);
        size_t at = size_t(x);
        if (at >= count) at = 0;
        bool found = false;
        for (size_t tries = 0; tries < count; ++tries) {
            if (rows_[at].enabled && rows_[at].order < 0) {
                found = true;
                break;
            }
            if (++at >= count) at = 0;
        }
        (void)found;
        if (rows_[at].order < 0) rows_[at].order = order++;
    }
    seed_ = u16(int(random_float(random_step(state)) * 65536.0f));
}

int RacePlaylist::next() {
    const size_t count = rows_.size();
    int enabled = 0;
    for (const Row& r : rows_) enabled += r.enabled;
    if (enabled > 0) position_ = int(u16(position_) % u16(enabled));
    else none_ = true;
    if (none_ || count == 0) return -1;
    int want = position_;
    // The row in that place; one played this round passes the turn to the next place (the
    // search then starts again from the second row, as the program's does).
    for (int guard = 0; guard < 4 * int(count) + 4; ++guard) {
        for (size_t i = 0; i < count; ++i) {
            Row& r = rows_[i];
            if (r.order != want) continue;
            if (r.played) {
                ++want;
                i = 0; // (the loop's ++ makes it 1)
                continue;
            }
            r.played = true;
            want = (want + 1) % enabled;
            if (want == 0) reset_played();
            position_ = want;
            return int(i);
        }
    }
    position_ = want;
    return -1;
}

} // namespace gt4r::audio
