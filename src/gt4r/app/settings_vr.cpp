// Settings > Display in a VR headset's build (settings_vr.h): the headset's own rows first (the engine's docs/dev/VR.md "Settings"),
// then the display's rows, those a headset decides itself shown as its own.

#include "gt4r/app/settings_vr.h"

#if GT3R_XR

#include "base/config.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace gt4r {

namespace {

using gt3r::xr::Settings;

std::string metres(float v, const char* format = "%.1f m") {
    char b[16];
    std::snprintf(b, sizeof b, format, double(v));
    return b;
}

} // namespace

void vr_display_rows(App& app, PageRows& rows) {
    App* a = &app;
    gt3r::xr::Session* x = app.xr();
    PageRows vr;
    vr.heading("VR headset");
    // [vr] runtime: a computer with more than one kind of VR software.
    const std::vector<std::pair<std::string, std::string>> runtimes = gt3r::xr::Session::runtime_choices();
    if (!runtimes.empty()) {
        std::vector<std::pair<std::string, std::string>> options{{"auto", "Automatic"}};
        options.insert(options.end(), runtimes.begin(), runtimes.end());
        vr.pick("Headset software", options, gt3r::to_lower(app.config().get("vr", "runtime").value_or("auto")), [a](const std::string& key) {
              a->config().set("vr", "runtime", key);
              a->save_config();
          }).tag = "from the next start";
    }
    if (!x) {
        vr.info("Headset", "None found at the start: the game is in a window");
    } else {
        if (!x->runtime_name().empty()) vr.info("Running in", x->runtime_name());
        const Settings s = x->settings();
        const auto set = [a, s](auto&& change) {
            Settings n = s;
            change(n);
            a->set_xr_settings(n);
        };
        vr.choice("Races", {"On the screen", "In 3D"}, s.stereo, [set](size_t i) { set([&](Settings& n) { n.stereo = i == 1; }); });
        vr.choice("In-car view", {"Turns with the car", "Level horizon"}, s.level_horizon, [set](size_t i) { set([&](Settings& n) { n.level_horizon = i == 1; }); });
        vr.choice("Your car in the in-car view", {"Not drawn", "Its body round you"}, s.show_car, [set](size_t i) { set([&](Settings& n) { n.show_car = i == 1; }); });
        vr.choice("Steering", {"Thumbstick", "Both grips: a wheel"}, s.wheel, [set](size_t i) { set([&](Settings& n) { n.wheel = i == 1; }); });
        static constexpr int kDegrees[] = {180, 270, 360, 540, 900};
        size_t di = 0;
        for (size_t i = 0; i < 5; ++i)
            if (s.wheel_degrees >= kDegrees[i]) di = i;
        vr.choice("Wheel rotation, lock to lock", {"180 degrees", "270 degrees", "360 degrees", "540 degrees", "900 degrees"}, di,
                  [set](size_t i) { set([&](Settings& n) { n.wheel_degrees = kDegrees[i]; }); })
            .enabled = s.wheel;
        vr.slider("Screen size", s.screen_width, 1.2f, 6.0f, 0.2f, [](float v) { return metres(v); }, [set](float v) { set([&](Settings& n) { n.screen_width = v; }); });
        vr.slider("Screen distance", s.screen_distance, 1.0f, 6.0f, 0.2f, [](float v) { return metres(v); }, [set](float v) { set([&](Settings& n) { n.screen_distance = v; }); });
        vr.slider("Race display size", s.hud_width, 0.6f, 3.0f, 0.1f, [](float v) { return metres(v); }, [set](float v) { set([&](Settings& n) { n.hud_width = v; }); });
        vr.slider("Race display distance", s.hud_distance, 0.6f, 3.0f, 0.1f, [](float v) { return metres(v); }, [set](float v) { set([&](Settings& n) { n.hud_distance = v; }); });
        // (The in-car view's camera is on the car's centre line.)
        vr.choice("In-car seat", {"In the middle", "On the left", "On the right", "The car's driver's"}, size_t(s.seat_side),
                  [set](size_t i) { set([&](Settings& n) { n.seat_side = gt3r::xr::SeatSide(i); }); });
        vr.slider("Seat moved aside", s.seat_offset, 0.1f, 0.6f, 0.02f, [](float v) { return metres(v, "%.2f m"); }, [set](float v) { set([&](Settings& n) { n.seat_offset = v; }); })
            .enabled = s.seat_side == gt3r::xr::SeatSide::Left || s.seat_side == gt3r::xr::SeatSide::Right;
        vr.slider("Seat height", s.seat_height, -0.6f, 0.6f, 0.02f, [](float v) { return metres(v, "%+.2f m"); }, [set](float v) { set([&](Settings& n) { n.seat_height = v; }); });
        vr.slider("Seat forward", s.seat_forward, -0.6f, 0.6f, 0.02f, [](float v) { return metres(v, "%+.2f m"); }, [set](float v) { set([&](Settings& n) { n.seat_forward = v; }); });
        const std::vector<float>& rates = x->refresh_rates();
        if (!rates.empty()) {
            std::vector<std::string> names;
            size_t at = 0;
            for (size_t i = 0; i < rates.size(); ++i) {
                names.push_back(std::to_string(int(std::lround(rates[i]))) + " Hz");
                if (std::fabs(rates[i] - float(s.refresh_hz)) < std::fabs(rates[at] - float(s.refresh_hz))) at = i;
            }
            vr.choice("Refresh rate", names, at, [set, rates](size_t i) { set([&](Settings& n) { n.refresh_hz = int(std::lround(rates[i])); }); });
        }
        static constexpr float kScales[] = {0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f};
        size_t si = 0;
        for (size_t i = 0; i < 7; ++i)
            if (std::fabs(kScales[i] - s.eye_scale) < std::fabs(kScales[si] - s.eye_scale)) si = i;
        vr.choice("Eyes' resolution", {"70 %", "80 %", "90 %", "100 %", "110 %", "120 %", "130 %"}, si, [set](size_t i) { set([&](Settings& n) { n.eye_scale = kScales[i]; }); })
            .tag = "of the headset's; after a restart";
        vr.action("Recenter view", std::to_string(x->eye(0).width) + " x " + std::to_string(x->eye(0).height) + " an eye", "here, where you look now (F9)",
                  [x] { x->recenter(); });
    }
    vr.heading("Display");
    // The rows of a screen the headset decides itself.
    if (x)
        for (PageRow& r : rows.rows)
            if (r.label == "Screen" || r.label == "VSync" || r.label == "Frame rate" || r.label == "Gamma" || r.label == "3D resolution" ||
                r.label == "Dynamic resolution")
                r.enabled = false, r.value = "The headset's", r.tag.clear();
    rows.rows.insert(rows.rows.begin(), vr.rows.begin(), vr.rows.end());
}

} // namespace gt4r

#endif // GT3R_XR
