// The home screen's Controls: the player's device, how the race reads it, its motors, triggers and force feedback, and what
// each race control is bound to. The game's own pages (Options: Key Configuration, Controllers, Steering) keep what the
// console's do; these are what a computer's pads and wheels need beside them. Kept in config.ini.

#include "gt4r/app/app.h"
#include "gt4r/input/player_controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace gt4r {

namespace {

using gt3r::PadTarget;

std::string percent(float v) { return std::to_string(int(std::lround(v * 100))) + " %"; }

class ControlsScreen : public Screen {
public:
    explicit ControlsScreen(App& app) : wheel_(input::WheelSettings::load(app.config())) {}

    void update(App& app, double dt) override {
        gt3r::InputManager& in = app.input();
        if (test_left_ > 0) { // the motors' test
            test_left_ -= dt;
            in.set_rumble(0, test_left_ > 0 ? gt3r::PadRumble{160, 160} : gt3r::PadRumble{});
        }
        if (capturing_ >= 0) {
            if (const auto got = in.poll_capture()) {
                const Binding& b = bindings(in)[size_t(capturing_)];
                if (b.menu) in.bind(0, b.target, got->source, true, got->rest);
                else in.bind_race(0, b.target, got->source, true, got->rest);
                capturing_ = -1, changed_ = true, settle_ = true;
            } else if (!in.capturing()) {
                capturing_ = -1, settle_ = true;
            }
            return;
        }
        const MenuInput& m = app.menu();
        build(app);
        const std::vector<Binding> keys = bindings(in);
        const int count = int(rows_.size() + keys.size());
        if (m.vertical()) sel_ = std::clamp(sel_ + m.vertical(), 0, count - 1);
        sel_ = std::clamp(sel_, 0, std::max(0, count - 1));
        if (settle_) { // (the key just bound is let go first)
            settle_ = m.held(PadButton::Cross) || m.held(PadButton::Triangle);
            return;
        }
        if (sel_ < int(rows_.size())) {
            const int d = m.horizontal() ? m.horizontal() : m.pressed(PadButton::Cross) ? 1 : 0;
            if (d) rows_[size_t(sel_)].change(d), changed_ = true;
        } else if (m.pressed(PadButton::Cross)) {
            in.begin_capture(0);
            capturing_ = sel_ - int(rows_.size());
        } else if (m.pressed(PadButton::Square)) {
            in.reset_bindings(0), in.set_race_layout(0, "modern");
            changed_ = true;
        }
        if (m.pressed(PadButton::Triangle)) {
            if (changed_) save(app);
            app.pop();
        }
    }

    void render(App& app, Ui& ui) override {
        gt3r::InputManager& in = app.input();
        ui.header("CONTROLS", in.device_name(0));
        const std::vector<Binding> keys = bindings(in);
        const int count = int(rows_.size() + keys.size());
        constexpr int kShown = 15;
        const int top = std::clamp(sel_ - kShown / 2, 0, std::max(0, count - kShown));
        for (int i = top; i < std::min(count, top + kShown); ++i) {
            const float y = 34 + float(i - top) * 17;
            const bool on = i == sel_;
            ui.fill_round(20, y - 3, 400, 15, 3, on ? colors::kHighlight : colors::kPanel);
            std::string label, value;
            if (i < int(rows_.size())) label = rows_[size_t(i)].label, value = rows_[size_t(i)].value;
            else {
                const Binding& b = keys[size_t(i) - rows_.size()];
                label = b.label, value = capturing_ == i - int(rows_.size()) ? std::string("press it now (Esc: leave it)") : bound(in, b);
            }
            ui.text(30, y, label, on ? colors::kText : colors::kDim);
            ui.text_right(412, y, Ui::elide(value, 220), on ? colors::kText : colors::kDim);
        }
        // What the race would be given now.
        const input::RaceInput now = input::read_player(in, 0, input::PortConfig{}, wheel_);
        const sim::rules::PadFrame& f = now.frame;
        const float x = 440, w = 180;
        ui.text(x, 34, "What the race reads", colors::kDim);
        const auto bar = [&](float y, const char* name, float v, bool center) {
            ui.text(x, y, name, colors::kDim);
            ui.fill(x, y + 12, w, 6, colors::kPanel);
            if (center) ui.fill(x + w / 2 + std::min(0.0f, v) * w / 2, y + 12, std::fabs(v) * w / 2, 6, colors::kAccent);
            else ui.fill(x, y + 12, v * w, 6, colors::kAccent);
        };
        const auto pedal = [](bool analog, int v) { return analog ? float(v) / 4096.0f : v ? 1.0f : 0.0f; };
        const float steer = f.flags & 1 ? -float(f.steer) / 4096.0f : f.steer > 0 ? -1.0f : f.steer < 0 ? 1.0f : 0.0f;
        bar(50, "Steering", steer, true);
        bar(76, "Accelerator", pedal(f.flags & 2, f.throttle), false);
        bar(102, "Brake", pedal(f.flags & 4, f.brake), false);
        bar(128, "Reverse", pedal(f.flags & 8, f.back), false);
        std::string more = f.kind == 6 ? "wheel, 900 degrees" : f.kind == 4 ? "wheel, 200 degrees" : f.kind == 2 ? "stick" : "buttons";
        if (f.shift) more += f.shift > 0 ? ", shift up" : ", shift down";
        if (f.third) more += ", handbrake";
        if (f.nitrous) more += ", nitrous";
        if (now.views & input::RaceInput::kPlayView) more += ", change view";
        if (now.views & (input::RaceInput::kBackView | input::RaceInput::kRearView)) more += ", look back";
        ui.text(x, 156, Ui::elide(more, w), colors::kText);
        float y = 182;
        for (const std::string& line : Ui::wrap("The game's own Options keep the rest: Key Configuration, vibration, the steering wheel's page. Once its "
                                                "Key Configuration for the DUALSHOCK 2 is changed, races read the pad through it.", w))
            ui.text(x, y, line, colors::kDim), y += 12;
        ui.footer(sel_ < int(rows_.size()) ? "{Left/Right}: change  {Triangle}: keep and go back" : "{Cross}: bind  {Square}: every binding as it was  {Triangle}: keep and go back");
    }

    ~ControlsScreen() override = default;

private:
    struct Row {
        std::string label, value;
        std::function<void(int)> change;
    };
    struct Binding {
        std::string label;
        PadTarget target;
        bool menu = false; // the menus' binding, not the race's
    };

    void choice(std::string label, const std::vector<std::pair<std::string, std::string>>& options, const std::string& now, std::function<void(const std::string&)> set) {
        int at = 0;
        for (size_t i = 0; i < options.size(); ++i)
            if (options[i].first == now) at = int(i);
        rows_.push_back({std::move(label), options[size_t(at)].second, [options, at, set = std::move(set)](int d) {
                             set(options[size_t((at + d + int(options.size())) % int(options.size()))].first);
                         }});
    }
    void slider(std::string label, float now, float lo, float hi, float step, std::function<std::string(float)> text, std::function<void(float)> set) {
        rows_.push_back({std::move(label), text(now), [=, set = std::move(set)](int d) { set(std::clamp(std::round((now + float(d) * step) / step) * step, lo, hi)); }});
    }

    void build(App& app) {
        gt3r::InputManager& in = app.input();
        rows_.clear();
        std::vector<std::pair<std::string, std::string>> devices{{"auto", "Automatic"}};
        for (const std::string& n : in.device_names())
            if (std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.first == n; })) devices.emplace_back(n, n);
        if (in.device_filter(0) != "auto" && !in.keyboard_only(0) && std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.first == in.device_filter(0); }))
            devices.emplace_back(in.device_filter(0), in.device_filter(0) + " (not connected)");
        devices.emplace_back(gt3r::InputManager::kKeyboardOnly, "Keyboard only");
        choice("Controller", devices, in.device_filter(0), [&in](const std::string& v) { in.set_device_filter(0, v); });
        const bool wheel = in.wheel_steering(0);
        if (!wheel)
            choice("Race controls", {{"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}}, in.race_layout(0), [&in](const std::string& v) { in.set_race_layout(0, v); });
        slider("Steering dead zone", in.steering_deadzone(0), 0, gt3r::InputManager::kSteeringDeadzoneMax, 0.01f, percent, [&in](float v) { in.set_steering_deadzone(0, v); });
        slider("Steering response", in.steering_curve(0), gt3r::InputManager::kSteeringCurveMin, gt3r::InputManager::kSteeringCurveMax, 0.05f,
               [](float v) {
                   char b[48];
                   std::snprintf(b, sizeof b, v > 1.001f ? "%.2f, finer near the center" : v < 0.999f ? "%.2f, quicker off the center" : "%.2f, as you steer", double(v));
                   return std::string(b);
               },
               [&in](float v) { in.set_steering_curve(0, v); });
        gt3r::ForceFeedbackConfig ffb = in.ffb_config();
        const auto set_ffb = [&in, ffb](auto&& change) {
            gt3r::ForceFeedbackConfig c = ffb;
            change(c);
            in.set_ffb_config(c);
        };
        if (wheel) {
            choice("Wheel driven as", {{"3", "Driving Force Pro (900 degrees)"}, {"2", "Driving Force (200 degrees)"}}, std::to_string(int(wheel_.wheel)),
                   [this](const std::string& v) { wheel_.wheel = u8(std::atoi(v.c_str())); });
            slider("Wheel's rotation, as its driver has it", wheel_.rotation, 180, 1440, 30, [](float v) { return std::to_string(int(v)) + " degrees"; }, [this](float v) { wheel_.rotation = v; });
            choice("Force feedback", {{"on", "From the road"}, {"off", "None"}}, ffb.wheel ? "on" : "off", [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.wheel = v == "on"; }); });
            choice("Force model", {{"auto", "Automatic (Modern)"}, {"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}}, ffb.wheel_mode,
                   [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.wheel_mode = v; }); });
            slider("Strength", ffb.strength, 0, 2, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.strength = v; }); });
            slider("Steering force", ffb.steering, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.steering = v; }); });
            slider("Centering spring", ffb.spring, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.spring = v; }); });
            if (!ffb.gt3_wheel()) slider("Lighter in understeer by", ffb.understeer, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.understeer = v; }); });
            choice("Force's direction", {{"off", "As the wheel gives it"}, {"on", "The other way"}}, ffb.invert ? "on" : "off",
                   [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.invert = v == "on"; }); });
        }
        choice("Rumble", {{"auto", "Automatic (Modern)"}, {"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}}, in.rumble_mode(0),
               [&in](const std::string& v) { in.set_rumble_mode(0, v); });
        if (!wheel) {
            choice("Triggers", {{"on", "Brake resistance, the edge of grip"}, {"off", "Free"}}, ffb.triggers ? "on" : "off",
                   [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.triggers = v == "on"; }); });
            if (ffb.triggers) slider("Brake trigger's resistance", ffb.trigger_force, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.trigger_force = v; }); });
            rows_.push_back({"Try the motors", "half a second", [this](int) { test_left_ = 0.5; }});
        }
        choice("Another window in front", {{"off", "The race pauses"}, {"on", "The controller keeps driving"}}, in.background() ? "on" : "off",
               [&in](const std::string& v) { in.set_background(v == "on"); });
        choice("Keyboard", {{"on", "Also drives"}, {"off", "Does nothing while a controller is there"}}, in.keyboard_enabled(0) ? "on" : "off", [&in](const std::string& v) { in.set_keyboard(0, v == "on"); });
    }

    std::vector<Binding> bindings(const gt3r::InputManager& in) const {
        // (the "original" layout's buttons are the game's own Key Configuration page's)
        if (!in.wheel_steering(0) && in.race_uses_menu_pad(0)) return {};
        const auto button = [](const char* label, PadButton b, bool menu = false) {
            Binding x;
            x.label = label, x.menu = menu;
            x.target.kind = PadTarget::Kind::Button, x.target.button = b;
            return x;
        };
        const auto axis = [](const char* label, gt3r::InputSource::Half half) {
            Binding x;
            x.label = label;
            x.target.kind = PadTarget::Kind::Axis, x.target.axis = gt3r::PadAxis::LX, x.target.half = half;
            return x;
        };
        std::vector<Binding> out{axis("Steer (an axis)", gt3r::InputSource::Half::Full), axis("Steer left", gt3r::InputSource::Half::Negative), axis("Steer right", gt3r::InputSource::Half::Positive),
                                 button("Accelerate", PadButton::Cross), button("Brake", PadButton::Square), button("Handbrake", PadButton::Circle), button("Reverse", PadButton::Triangle)};
        if (in.wheel_steering(0)) {
            for (Binding b : {button("Right paddle", PadButton::R1), button("Left paddle", PadButton::L1), button("Lever: shift up", PadButton::R2), button("Lever: shift down", PadButton::L2),
                              button("Change view", PadButton::Select, true), button("Look back (wheel's Down)", PadButton::Down), button("Pause", PadButton::Start)})
                out.push_back(b);
        } else {
            for (Binding b : {button("Shift up", PadButton::R2), button("Shift down", PadButton::L2), button("Change view", PadButton::R1), button("Look back", PadButton::L1),
                              button("Nitrous", PadButton::Down), button("Pause", PadButton::Start)})
                out.push_back(b);
        }
        return out;
    }

    static std::string bound(const gt3r::InputManager& in, const Binding& b) {
        const gt3r::Profile& p = b.menu ? in.profile(0) : in.race_profile(0);
        std::string out;
        for (const gt3r::Binding& x : p.bindings)
            if (x.target == b.target) out += (out.empty() ? "" : ", ") + in.describe(0, x.source);
        return out.empty() ? "-" : out;
    }

    void save(App& app) {
        gt3r::IniDocument& c = app.config();
        app.input().save(c);
        wheel_.save(c);
        for (const auto& [section, key] : {std::pair<const char*, const char*>{"player1", "race"}, {"player1", "rumble"}, {"player2", "race"}, {"player2", "rumble"}, {"ffb", "wheel_mode"}})
            if (const auto v = c.get(section, key)) c.set(section, key, input::our_word(*v));
        app.save_config();
        changed_ = false;
    }

    input::WheelSettings wheel_;
    std::vector<Row> rows_;
    int sel_ = 0, capturing_ = -1;
    bool changed_ = false, settle_ = false;
    double test_left_ = 0;
};

} // namespace

std::unique_ptr<Screen> make_controls_screen(App& app) { return std::make_unique<ControlsScreen>(app); }

} // namespace gt4r
