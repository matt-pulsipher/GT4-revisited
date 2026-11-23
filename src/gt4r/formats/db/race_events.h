#pragma once

// The events of the car database (docs/formats/database.md "Events: RACE and ENEMY_CARS"): a RACE row's
// course, laps, grid, entry rules, prizes, target times and the computer drivers' settings; its pool of
// opponents (the file specdb/<set>/RACES/<table, row> of ENEMY_CARS rows), and an opponent's car, parts
// and settings. The columns are the program's (its MRaceData methods 0x1A0F80 .. 0x1A2E30, the
// RaceSpec accessors 0x447400 .. 0x4477D0 and the race's copy 0x35E108).

#include "gt4r/formats/db/specdb.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::db {

struct RaceEvent {
    u32 id = 0;
    std::string label;          // "s_sunday_b_0001"
    u32 course_id = 0;          // +0x00 the COURSE row (+0x04: its table, 34)
    std::string course;         // its label: crs/<course>
    int laps = 0;               // +0x43
    int grid = 0;               // +0x3E the cars on the grid, the player's included
    u8 x44 = 0;                 // +0x44 (the race keeps it with the laps) ❓
    int time_limit = 0;         // +0x18 u16 (getLimitTime) ❓ unit
    // +0x76 / +0x78 / +0x7A minutes, +0x77 / +0x79 / +0x7B seconds, +0x1A / +0x1C / +0x1E u16 milliseconds: the gold,
    // silver and bronze times (licenses and missions); 0: none.
    std::array<u32, 3> target_ms{};
    std::array<u16, 6> prize{}; // +0x20 u16 x 6 the prize by place (getPrize)
    s16 prize_x2c = 0;          // +0x2C s16 (getPrizeGC: x 100)
    // The entry rules (getNeed...; 0: none): +0x45 license, +0x46 drivetrain, +0x48 aspiration, +0x49 the car's
    // category, +0x4A tires, +0x92 power (x 10 PS), +0x93 price, +0x94 weight, +0x95 length, +0x96 country;
    // +0x38 / +0x3A s16 the model years.
    u8 need_license = 0, need_drivetrain = 0, need_aspiration = 0, need_car_type = 0, need_tire = 0;
    int need_ps = 0;
    u8 need_price = 0, limit_weight = 0, limit_length = 0, need_country = 0;
    s16 year_from = 0, year_to = 0;
    // The computer drivers (the race's copy 0x35E108, read by 0x467148 and 0x365480): per driveline class the
    // throttle (+0x5F), braking (+0x5B) and arcs' speed (+0x57) percents; the rubber band: +0x63 its overall
    // percent, +0x64 its mode, +0x65 .. +0x67 the slowing of the cars ahead (percent, from metres, full at
    // x 10 metres), +0x68 .. +0x6A the hurrying of the cars behind.
    std::array<u8, 4> ai_throttle{}, ai_brake{}, ai_corner{};
    u8 band_global = 0, band_mode = 0, slow_pct = 0, slow_start = 0, slow_full = 0, fast_pct = 0, fast_start = 0, fast_full = 0;
    std::vector<u8> bytes;      // the whole row (151 bytes)
};

// An opponent of an event's pool: an ENEMY_CARS row.
struct Opponent {
    u32 enemy_id = 0;           // the ENEMY_CARS row
    std::string enemy;          // its label ("gt4r_jp80_celica_gtr_4ws_91")
    u32 car_id = 0;             // +0x00 the GENERIC_CAR row
    std::string car;            // its label
    u32 parts_id = 0;           // +0x08 the DEFAULT_PARTS row (table 36): the car's parts
    std::string parts;
    u32 param_id = 0;           // +0x10 the DEFAULT_PARAM row (table 37): its settings
    std::string param;
    int paint = -1;             // the pool's paint for it (its second word less one; -1: the race chooses)
};

class RaceEvents {
public:
    // Reads RACE and ENEMY_CARS of `db`; `read` reads the database's other files (the pools).
    RaceEvents(SpecDb& db, SpecDb::Reader read);

    std::optional<RaceEvent> event(const std::string& label) const;
    std::optional<RaceEvent> event(u32 id) const;
    std::vector<std::string> labels() const;
    std::optional<Opponent> opponent(u32 enemy_id) const;
    // An event's pool of opponents (specdb/<set>/RACES/<35, row>), in the file's order; empty without one.
    std::vector<Opponent> pool(const RaceEvent& e) const;
    // The opponents the race draws for `count` places (RaceInformation::vf15, 0x3BB5A8): each place a random
    // entry of the pool not drawn yet (the next free one on from a random place), `random` giving numbers in
    // [0, 1). Once every entry is drawn, the random entry as it falls (repeats).
    template <class Random> std::vector<Opponent> draw(const RaceEvent& e, int count, Random&& random) const {
        const std::vector<Opponent> p = pool(e);
        std::vector<Opponent> out;
        if (p.empty()) return out;
        std::vector<bool> used(p.size(), false);
        size_t free = p.size();
        for (int k = 0; k < count; ++k) {
            size_t i = size_t(random() * float(p.size())) % p.size();
            if (free > 0) {
                while (used[i]) i = (i + 1) % p.size();
                used[i] = true, --free;
            }
            out.push_back(p[i]);
        }
        return out;
    }

private:
    SpecDb* db_;
    SpecDb::Reader read_;
};

} // namespace gt4r::db
