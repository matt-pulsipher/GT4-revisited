#pragma once

// The races' play list (docs/formats/sound.md "The music"): which piece of the streamed "racing"
// group plays next. The program's MPlayList (0x437E50 .. 0x4382B0): a row per piece of the group,
// each enabled or not, with its place in the order and whether it has played this round.
//
//   audio::RacePlaylist list = audio::RacePlaylist::in_order(65, {15, 16, 47, ...});
//   int piece = list.next();   // the group's piece to play, -1: none enabled
//
// The order a new game has comes from the configuration script (its init_raceplaylist: a list of
// the group's pieces in playing order for the region, applied to the rows); a shuffled list is
// ordered by the program's own random numbers from a seed the list keeps.

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::audio {

class RacePlaylist {
public:
    struct Row {
        bool enabled = false;
        bool played = false;
        int order = -1; // the place in the playing order, -1: none
    };

    RacePlaylist() = default;
    explicit RacePlaylist(size_t count) : rows_(count) {}
    // The rows of `pieces` enabled and ordered as listed; the others disabled.
    static RacePlaylist in_order(size_t count, const std::vector<int>& pieces);

    std::vector<Row>& rows() { return rows_; }
    const std::vector<Row>& rows() const { return rows_; }
    int position() const { return position_; }
    void set_position(int p) { position_ = p; }
    bool shuffle() const { return shuffle_; }
    void set_shuffle(bool s, u16 seed) { shuffle_ = s, seed_ = seed; }
    u16 seed() const { return seed_; }

    // The order made again (0x437EB8): in the rows' own order (0x437EF0), or shuffled (0x437F58).
    void build();
    // Every enabled row not yet played this round (0x437E50).
    void reset_played();
    // The next piece (0x438170): the row whose place is the position, skipping rows played this
    // round; when the round is through, a new one starts. -1 when no row is enabled.
    int next();

    // The program's random numbers (0x579330, 0x578598): a state stepped as s = 17 s + 17, the
    // result s ^ (s rotated by 16), made a float of 0..1 by its own normalisation.
    static u32 random_step(u32& state);
    static float random_float(u32 bits);

private:
    std::vector<Row> rows_;
    int position_ = 0;
    bool shuffle_ = false;
    bool none_ = false; // no row enabled
    u16 seed_ = 0;
};

} // namespace gt4r::audio
