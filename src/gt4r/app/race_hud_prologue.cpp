#include "gt4r/app/race_hud_prologue.h"

#include "base/log.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>

namespace gt4r {

namespace hr = gt3r::race;

PrologueHud::PrologueHud(App& app, ByteSpan gtcm) {
    gt3r::Vfs* vfs = app.vfs();
    if (!vfs) return;
    const std::optional<Bytes> modes = vfs->read("database/racemode_e3.db"), rows = vfs->read("database/racedetail_e3.db");
    // The pictures are in a folder of their own under the region's: race/JP/display/display.imgs
    // (Gran Turismo 3's race/JP/display.imgs). The engine's display reads race/<language>/display.imgs.
    std::string region;
    for (const char* r : {"JP", "US", "BR"})
        if (vfs->exists(std::string("race/") + r + "/display/display.imgs")) {
            region = r;
            break;
        }
    if (!modes || !rows || region.empty()) return;
    if (!hr::set_hud_tables(*modes, *rows)) {
        LOG_WARN("Prologue's race display: its layout tables do not read");
        return;
    }
    hud_ = std::make_unique<hr::Hud>();
    hud_->load(*vfs, app.renderer(), region + "/display");
    hud_->set_course_map(app.renderer(), gtcm);
    hud_->reset_needles();
    ready_ = true;
}

PrologueHud::~PrologueHud() {
    if (ready_) hr::clear_hud_tables();
}

hr::HudData PrologueHud::data(const HudState& s, const Extra& e) {
    hr::HudData d;
    hr::HudModeSelect m;
    m.run = e.run, m.event_laps = s.laps, m.play = e.live ? hr::HudPlay::Live : hr::HudPlay::Replay;
    d.mode = hr::hud_mode(m);
    d.live = e.live;
    if (!e.live) d.items = hr::kLiveItems;
    d.speed_kmh = std::fabs(s.speed) * 3.6f;
    d.rpm = s.rpm;
    d.rev_limit_rpm = s.rev_limit > 0 ? s.rev_limit : 7000;
    d.shift_rpm = s.red_line;
    d.top_speed_kmh = s.top_speed * 3.6f;
    d.gear = s.gear;
    d.automatic = e.automatic;
    d.onboard = s.driver_view;
    d.throttle = s.throttle, d.brake = s.brake;
    d.lap = s.lap;
    d.total_laps = s.laps;
    auto seconds = [](s64 ms) { return ms < 0 ? -1.0 : double(ms) / 1000.0; };
    d.lap_time = std::max(0.0, seconds(s.lap_ms));
    d.last_lap = seconds(s.last_ms), d.best_lap = seconds(s.best_ms);
    d.lap_list_lines = hr::lap_list_lines(s.laps);
    d.position = s.place;
    // Prologue shows the place in a run without opponents too ("1st" on its arcade's solo race,
    // the original's pictures); the engine's display shows it from two cars. ❓ The rule is not
    // read from the program.
    d.cars = std::max(2, int(s.cars.size()));
    d.countdown = e.countdown;
    d.finished = e.finish_t >= 0, d.finish_t = e.finish_t;
    d.car_name = e.car_name;
    d.license_targets = e.license_targets;
    if (!s.cars.empty()) {
        d.player_x = s.cars[0].x, d.player_z = s.cars[0].z;
        for (size_t i = 1; i < s.cars.size(); ++i) d.others.push_back({s.cars[i].x, s.cars[i].z});
    }
    d.trip_m = s.trip_m;
    return d;
}

void PrologueHud::draw(Ui& ui, const HudState& s, const Extra& e) {
    if (!ready_ || s.opacity <= 0) return;
    hud_->set_screen({0, 0, float(Ui::kWidth), float(Ui::kHeight)});
    hud_->draw(ui.renderer(), data(s, e));
}

} // namespace gt4r
