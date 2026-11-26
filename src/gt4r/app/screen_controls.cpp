// A player's controls: the device, how the race reads it, its motors, triggers and force feedback, the game's own steering and
// vibration options, what each race control and each menu button is bound to, and the songs' controls. The game's Options pages
// for these (the button configurations, Vibration, Steering) show only a way here; what they set is shown and set here, in the
// game's own options (app/game_options.h). Ours are kept in config.ini.

#include "gt4r/app/app.h"
#include "gt4r/app/game_options.h"
#include "gt4r/app/page.h"
#include "gt4r/input/player_controls.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>

namespace gt4r {

namespace {

using gt3r::PadTarget;

std::string percent(float v) { return std::to_string(int(std::lround(v * 100))) + "%"; }

class ControlsScreen : public Screen {
public:
    ControlsScreen(App& app, int player, bool to_game, ControlsSection section, std::shared_ptr<GameOptions> options)
        : player_(player), to_game_(to_game), options_(options ? std::move(options) : saved_game_options(app)),
          wheel_(input::WheelSettings::load(app.config())) {
        first_row_ = section == ControlsSection::Wheel ? "Wheel driven as" : section == ControlsSection::Rumble ? "Rumble" : section == ControlsSection::Steering ? "Active steering" : "";
    }

    // (a wrong choice of device can be taken back with any other)
    bool menu_input_any_device() const override { return true; }

    void update(App& app, double dt) override {
        gt3r::InputManager& in = app.input();
        if (test_left_ > 0) { // the motors' test
            test_left_ -= dt;
            in.set_rumble(player_, test_left_ > 0 ? gt3r::PadRumble{160, 160} : gt3r::PadRumble{});
        }
        if (capturing_) return capture(in);
        const MenuInput& m = app.menu();
        if (settle_) { // (the key just bound is let go first)
            settle_ = m.held(PadButton::Cross) || m.held(PadButton::Triangle) || m.held(PadButton::Square);
            build(app);
            return;
        }
        build(app);
        // Opened from one of the game's pages: its part selected.
        if (const std::string first = std::exchange(first_row_, std::string()); !first.empty())
            for (size_t i = 0; i < rows_.rows.size(); ++i)
                if (rows_.rows[i].label == first) {
                    rows_.selected = int(i);
                    break;
                }
        if (m.pressed(PadButton::Triangle)) {
            if (changed_) save(app);
            if (const std::string error = options_->close(); !error.empty()) {
                message_ = "Your saved game could not be changed: " + error;
                return;
            }
            in.set_rumble(player_, {});
            app.pop();
            return;
        }
        if (m.pressed(PadButton::Select) && rows_.selected >= 0 && rows_.selected < int(bound_.size()) && bound_[size_t(rows_.selected)]) {
            clear(in, *bound_[size_t(rows_.selected)]);
            changed_ = true;
        } else if (rows_.update(app).changed) changed_ = true;
        build(app);
    }

    void render(App& app, Ui& ui) override {
        gt3r::InputManager& in = app.input();
        page_header(app, ui, "PLAYER " + std::to_string(player_ + 1) + "'S CONTROLS", in.device_name(player_), to_game_ ? "< Back to game" : "< Back");
        rows_.render(ui, {20, 28, 410, 17 * 18 + 1});
        // What the race would be given now.
        const input::RaceInput now = input::read_player(in, player_, options_->state() ? options_->port(player_) : input::PortConfig{}, wheel_);
        const sim::rules::PadFrame& f = now.frame;
        const float x = 446, w = 180;
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
        std::string more = std::string("Steered by ") + (f.kind == 6 ? "a wheel, 900 degrees" : f.kind == 4 ? "a wheel, 200 degrees" : f.kind == 2 ? "a stick" : "buttons");
        if (f.shift) more += f.shift > 0 ? ", shift up" : ", shift down";
        if (f.third) more += ", handbrake";
        if (f.nitrous) more += ", nitrous";
        if (now.views & input::RaceInput::kPlayView) more += ", change view";
        if (now.views & (input::RaceInput::kBackView | input::RaceInput::kRearView)) more += ", look back";
        ui.text(x, 156, Ui::elide(more, w), colors::kText);
        float y = 182;
        if (!in.wheel_steering(player_) && in.race_uses_menu_pad(player_))
            for (const std::string& line : Ui::wrap("Gran Turismo 4 original reads your pad through the game's own button layout, listed here as your saved game has it.", w))
                ui.text(x, y, line, colors::kDim), y += 12;
        if (!message_.empty())
            for (const std::string& line : Ui::wrap(message_, w)) ui.text(x, y + 6, line, colors::kBad), y += 12;
        std::string hint = rows_.hint();
        if (rows_.selected >= 0 && rows_.selected < int(bound_.size()) && bound_[size_t(rows_.selected)]) hint += "  {Select|Delete}: clear";
        ui.footer(capturing_ ? std::string("Press it now (Esc: leave it as it is)") : hint + (hint.empty() ? "" : "  ") + (to_game_ ? "{Triangle}: keep and go back to the game" : "{Triangle}: keep and go back"));
        if (capturing_ && chord_ >= 0) {
            std::string held;
            for (const gt3r::InputSource& s : in.chord_so_far()) held += (held.empty() ? "" : " + ") + in.describe(player_, s);
            ui.text_right(626, 330, held, colors::kAccent);
        }
    }

private:
    struct Binding {
        std::string label;
        PadTarget target;
        bool menu = false;  // the menus' binding, not the race's
        int action = -1;    // a song's control
    };

    void capture(gt3r::InputManager& in) {
        if (chord_ >= 0) {
            if (const auto got = in.poll_chord()) {
                in.bind_action(player_, gt3r::PadAction(chord_), *got, replace_);
                changed_ = true;
            } else if (in.capturing()) return;
        } else if (const auto got = in.poll_capture()) {
            if (target_.menu) in.bind(player_, target_.target, got->source, replace_, got->rest);
            else in.bind_race(player_, target_.target, got->source, replace_, got->rest);
            changed_ = true;
        } else if (in.capturing()) return;
        capturing_ = false, chord_ = -1, settle_ = true;
    }

    void begin(gt3r::InputManager& in, const Binding& b, bool replace) {
        target_ = b, replace_ = replace, capturing_ = true, chord_ = b.action;
        if (chord_ >= 0) in.begin_chord_capture(player_);
        else in.begin_capture(player_);
    }

    void clear(gt3r::InputManager& in, const Binding& b) {
        for (const gt3r::BindingSet s : {gt3r::BindingSet::Controller, gt3r::BindingSet::Keyboard}) {
            if (b.action >= 0) in.clear_action(player_, gt3r::PadAction(b.action), s);
            else if (b.menu) in.clear_target(player_, b.target, s);
            else in.clear_race_target(player_, b.target, s);
        }
    }

    void build(App& app) {
        gt3r::InputManager& in = app.input();
        const int p = player_;
        rows_.rows.clear();
        bound_.clear();
        std::vector<std::pair<std::string, std::string>> devices{{"auto", "Automatic"}};
        for (const std::string& n : in.device_names())
            if (std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.first == n; })) devices.emplace_back(n, n);
        if (in.device_filter(p) != "auto" && !in.keyboard_only(p) && std::none_of(devices.begin(), devices.end(), [&](const auto& d) { return d.first == in.device_filter(p); }))
            devices.emplace_back(in.device_filter(p), in.device_filter(p) + " (not connected)");
        devices.emplace_back(gt3r::InputManager::kKeyboardOnly, "Keyboard only");
        rows_.pick("Controller", devices, in.device_filter(p), [&in, p](const std::string& v) { in.set_device_filter(p, v); });
        const bool wheel = in.wheel_steering(p);
        if (!wheel) {
            std::vector<std::pair<std::string, std::string>> layouts{{"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}};
            const bool custom = in.is_race_custom(p);
            if (custom || in.has_custom(p, true)) layouts.emplace_back("custom", "Custom");
            rows_.pick("Race controls", layouts, custom ? "custom" : in.race_layout(p), [&in, p](const std::string& v) {
                if (v == "custom") in.use_custom(p, true);
                else in.set_race_layout(p, v);
            });
        }
        rows_.slider("Steering dead zone", in.steering_deadzone(p), 0, gt3r::InputManager::kSteeringDeadzoneMax, 0.01f, percent, [&in, p](float v) { in.set_steering_deadzone(p, v); });
        rows_.slider("Steering response", in.steering_curve(p), gt3r::InputManager::kSteeringCurveMin, gt3r::InputManager::kSteeringCurveMax, 0.05f,
                     [](float v) {
                         char b[48];
                         std::snprintf(b, sizeof b, v > 1.001f ? "%.2f, finer near the center" : v < 0.999f ? "%.2f, quicker off the center" : "%.2f, as you steer", double(v));
                         return std::string(b);
                     },
                     [&in, p](float v) { in.set_steering_curve(p, v); });
        gt3r::ForceFeedbackConfig ffb = in.ffb_config();
        const auto set_ffb = [&in, ffb](auto&& change) {
            gt3r::ForceFeedbackConfig c = ffb;
            change(c);
            in.set_ffb_config(c);
        };
        if (wheel) {
            rows_.pick("Wheel driven as", {{"3", "Driving Force Pro (900 degrees)"}, {"2", "Driving Force (200 degrees)"}}, std::to_string(int(wheel_.wheel)),
                       [this](const std::string& v) { wheel_.wheel = u8(std::atoi(v.c_str())); });
            rows_.slider("Wheel's rotation, as its driver has it", wheel_.rotation, 180, 1440, 30, [](float v) { return std::to_string(int(v)) + " degrees"; }, [this](float v) { wheel_.rotation = v; });
            // One setting with the game's own ("WHEEL FEEDBACK", per player): the force is sent when both are on.
            GameOptions* o = options_->state() ? options_.get() : nullptr;
            const bool forces = ffb.wheel && (!o || o->wheel_feedback(p));
            rows_.pick("Force feedback", {{"off", "None"}, {"on", "From the road"}}, forces ? "on" : "off", [set_ffb, o, p](const std::string& v) {
                const bool on = v == "on";
                if (o) o->set_wheel_feedback(p, on);
                if (on || !o) set_ffb([&](auto& c) { c.wheel = on; }); // (ours stays on while the game's is per player)
            });
            rows_.pick("Force model", {{"auto", "Automatic (Modern)"}, {"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}}, ffb.wheel_mode,
                       [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.wheel_mode = v; }); });
            rows_.slider("Strength", ffb.strength, 0, 2, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.strength = v; }); });
            rows_.slider("Steering force", ffb.steering, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.steering = v; }); });
            rows_.slider("Centering spring", ffb.spring, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.spring = v; }); });
            if (!ffb.gt3_wheel()) rows_.slider("Lighter in understeer by", ffb.understeer, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.understeer = v; }); });
            rows_.pick("Force's direction", {{"off", "As the wheel gives it"}, {"on", "The other way"}}, ffb.invert ? "on" : "off",
                       [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.invert = v == "on"; }); });
        }
        {
            // Off is the game's own vibration option (the DualShock 2's "ON", per player), which the race honors whatever the kind.
            GameOptions* o = options_->state() ? options_.get() : nullptr;
            std::vector<std::pair<std::string, std::string>> kinds{{"auto", "Automatic (Modern)"}, {"modern", "Modern"}, {"gt3", "Gran Turismo 4 original"}};
            if (o) kinds.insert(kinds.begin(), {"off", "Off"});
            rows_.pick("Rumble", kinds, o && !o->vibration(p) ? "off" : in.rumble_mode(p), [&in, p, o](const std::string& v) {
                if (o) o->set_vibration(p, v != "off");
                if (v != "off") in.set_rumble_mode(p, v);
            });
        }
        if (!wheel) {
            rows_.pick("Triggers", {{"off", "Free"}, {"on", "Brake resistance, the edge of grip"}}, ffb.triggers ? "on" : "off",
                       [set_ffb](const std::string& v) { set_ffb([&](auto& c) { c.triggers = v == "on"; }); });
            if (ffb.triggers) rows_.slider("Brake trigger's resistance", ffb.trigger_force, 0, 1, 0.05f, percent, [set_ffb](float v) { set_ffb([&](auto& c) { c.trigger_force = v; }); });
            rows_.action("Try the motors", "half a second", "try", [this] { test_left_ = 0.5; });
            const Binding steer = axis("", gt3r::InputSource::Half::Full);
            const bool tilts = tilt_bound(in, steer.target);
            if (in.kind(p) == gt3r::DeviceKind::Pad)
                rows_.pick("Tilting the controller", {{"off", "Does nothing"}, {"on", "Steers"}}, tilts ? "on" : "off", [&in, p, steer](const std::string& v) {
                    gt3r::InputSource tilt;
                    tilt.kind = gt3r::InputSource::Kind::Tilt;
                    if (v == "on") in.bind_race(p, steer.target, tilt, false);
                    else in.clear_race_target(p, steer.target, gt3r::BindingSet::Keyboard);
                });
        }
        rows_.pick("Keyboard", {{"off", "Does nothing while a controller is there"}, {"on", "Also drives"}}, in.keyboard_enabled(p) ? "on" : "off", [&in, p](const std::string& v) { in.set_keyboard(p, v == "on"); });

        steering_rows(p);

        rows_.heading("Race controls");
        const auto add = [&](const Binding& b) {
            bound_.resize(rows_.rows.size());
            PageRow& r = rows_.action(b.label, bound(in, b), "bind", [this, &in, b] { begin(in, b, true); });
            r.other = [this, &in, b] { begin(in, b, false); }, r.other_hint = "bind one more";
            if (capturing_ && target_.label == b.label && target_.menu == b.menu && target_.action == b.action) r.value = "press it now";
            bound_.push_back(b);
        };
        if (!wheel && in.race_uses_menu_pad(p)) game_layout_rows(p);
        for (const Binding& b : bindings(in)) add(b);
        rows_.action("Every race control", in.is_race_custom(p) ? "as bound here" : "as it comes", "as it comes", [&in, p] { in.set_race_layout(p, in.race_layout(p)); });
        rows_.heading("Songs (any buttons or keys, held together)");
        for (const gt3r::PadAction a : {gt3r::PadAction::NextSong, gt3r::PadAction::PreviousSong}) {
            Binding b;
            b.label = a == gt3r::PadAction::NextSong ? "Next song" : "Song before", b.action = int(a);
            add(b);
        }
        rows_.heading("Menu buttons");
        for (const auto& [name, button] : {std::pair<const char*, PadButton>{"Cross (confirm)", PadButton::Cross}, {"Triangle (back)", PadButton::Triangle}, {"Circle", PadButton::Circle},
                                           {"Square", PadButton::Square}, {"Up", PadButton::Up}, {"Down", PadButton::Down}, {"Left", PadButton::Left}, {"Right", PadButton::Right},
                                           {"L1", PadButton::L1}, {"R1", PadButton::R1}, {"L2", PadButton::L2}, {"R2", PadButton::R2}, {"Start", PadButton::Start}, {"Select", PadButton::Select}})
            add(this->button(name, button, true));
        rows_.action("Every menu button", in.is_custom(p) ? "as bound here" : "as it comes", "as it comes", [&in, p] { in.reset_bindings(p); });
        bound_.resize(rows_.rows.size());
    }

    // The game's steering options (its Options' Steering page), this player's.
    void steering_rows(int p) {
        rows_.heading("Steering (the game's own options)");
        GameOptions* o = options_->state() ? options_.get() : nullptr;
        if (!o) {
            rows_.info(options_->why(), "");
            return;
        }
        rows_.pick("Active steering", {{"off", "Not used"}, {"on", "Helps against spins"}}, o->active_steering(p) ? "on" : "off",
                   [o, p](const std::string& v) { o->set_active_steering(p, v == "on"); });
        rows_.pick("Steering type (GT FORCE, Driving Force)", {{"AMATEUR", "Amateur"}, {"PROFESSIONAL", "Professional"}, {"SIMULATION", "Simulation"}},
                   o->steering_type(p), [o, p](const std::string& v) { o->set_steering_type(p, v); });
        rows_.pick("Power assist (Driving Force Pro)", {{"off", "None"}, {"on", "Assisted"}}, o->power_assist(p) ? "on" : "off",
                   [o, p](const std::string& v) { o->set_power_assist(p, v == "on"); });
        rows_.pick("Force strength (Driving Force Pro)", {{"0", "Weak"}, {"1", "Strong"}}, o->force_strength(p) > 0 ? "1" : "0",
                   [o, p](const std::string& v) { o->set_force_strength(p, v == "1" ? 1 : 0); });
    }

    // Gran Turismo 4 original: the race reads the pad through the game's own button configuration (the saved game's), listed as
    // it is; Restore puts the game's layout back on every device of this player.
    void game_layout_rows(int p) {
        GameOptions* o = options_->state() ? options_.get() : nullptr;
        if (!o) {
            rows_.info("As your saved game has them", "");
            if (!options_->why().empty()) rows_.info(options_->why(), "");
            return;
        }
        const input::DeviceConfig c = o->port(p).of(input::Device::DualShock2);
        // The pad's keys by the settings' names: LX the left stick, LYF its upward half (F: left or up, L: right or down), CROSS.
        const auto name = [](const std::string& k) -> std::string {
            if (k.size() >= 2 && (k[0] == 'L' || k[0] == 'R') && (k[1] == 'X' || k[1] == 'Y') && (k.size() == 2 || k.size() == 3)) {
                std::string s = std::string(k[0] == 'L' ? "Left" : "Right") + " stick";
                if (k.size() == 3) s += k[1] == 'X' ? (k[2] == 'F' ? " left" : " right") : (k[2] == 'F' ? " up" : " down");
                return s;
            }
            std::string s = k;
            for (size_t i = 1; i < s.size(); ++i) s[i] = char(std::tolower(static_cast<unsigned char>(s[i])));
            return s;
        };
        const auto keys = [&name](const std::vector<std::string>& names) {
            std::string out;
            for (const std::string& n : names) out += (out.empty() ? "" : ", ") + name(n);
            return out.empty() ? std::string("none") : out;
        };
        using A = input::AxisAction;
        using B = input::ButtonAction;
        const auto both = [&](A a, B b) {
            std::vector<std::string> n = input::keys_with(c, input::Device::DualShock2, a);
            for (std::string& k : input::keys_with(c, input::Device::DualShock2, b)) n.push_back(k);
            return keys(n);
        };
        const auto one = [&](B b) { return keys(input::keys_with(c, input::Device::DualShock2, b)); };
        const std::pair<const char*, std::string> list[] = {
            {"Steer", keys(input::keys_with(c, input::Device::DualShock2, A::Steer))}, {"Steer left", one(B::SteerLeft)}, {"Steer right", one(B::SteerRight)},
            {"Accelerate", both(A::Accel, B::Accel)}, {"Brake", both(A::Brake, B::Brake)}, {"Reverse", both(A::ReverseGear, B::ReverseGear)},
            {"Handbrake", both(A::SideBrake, B::SideBrake)}, {"Shift up", both(A::ShiftUp, B::ShiftUp)}, {"Shift down", both(A::ShiftDown, B::ShiftDown)},
            {"Look back", one(B::BackView)}, {"Change view", one(B::PlayView)}, {"Nitrous", one(B::Special)}, {"Ghost", one(B::Ghost)}};
        for (const auto& [label, value] : list) rows_.info(label, value);
        rows_.action("Restore the game's layout", o->keys_changed(p) ? "changed in your saved game" : "as it comes", "restore", [o, p] { o->restore_keys(p); }).enabled =
            o->keys_changed(p);
    }

    static Binding button(const char* label, PadButton b, bool menu = false) {
        Binding x;
        x.label = label, x.menu = menu;
        x.target.kind = PadTarget::Kind::Button, x.target.button = b;
        return x;
    }
    static Binding axis(const char* label, gt3r::InputSource::Half half) {
        Binding x;
        x.label = label;
        x.target.kind = PadTarget::Kind::Axis, x.target.axis = gt3r::PadAxis::LX, x.target.half = half;
        return x;
    }

    std::vector<Binding> bindings(const gt3r::InputManager& in) const {
        // (the "original" layout's buttons are the game's own Key Configuration page's)
        const bool wheel = in.wheel_steering(player_);
        if (!wheel && in.race_uses_menu_pad(player_)) return {};
        std::vector<Binding> out{axis("Steer (an axis)", gt3r::InputSource::Half::Full), axis("Steer left", gt3r::InputSource::Half::Negative), axis("Steer right", gt3r::InputSource::Half::Positive),
                                 button("Accelerate", PadButton::Cross), button("Brake", PadButton::Square), button("Handbrake", PadButton::Circle), button("Reverse", PadButton::Triangle)};
        if (wheel) {
            for (const Binding& b : {button("Right paddle", PadButton::R1), button("Left paddle", PadButton::L1), button("Lever: shift up", PadButton::R2), button("Lever: shift down", PadButton::L2),
                                     button("Change view", PadButton::Select, true), button("Look back (wheel's Down)", PadButton::Down), button("Ghost (wheel's Up)", PadButton::Up),
                                     button("Pause", PadButton::Start)})
                out.push_back(b);
        } else {
            for (const Binding& b : {button("Shift up", PadButton::R2), button("Shift down", PadButton::L2), button("Change view", PadButton::R1), button("Look back", PadButton::L1),
                                     button("Nitrous", PadButton::Down), button("Ghost", PadButton::Up), button("Pause", PadButton::Start)})
                out.push_back(b);
        }
        return out;
    }

    bool tilt_bound(const gt3r::InputManager& in, const PadTarget& target) const {
        for (const gt3r::Binding& x : in.race_profile(player_).bindings)
            if (x.target == target && x.source.kind == gt3r::InputSource::Kind::Tilt) return true;
        return false;
    }

    std::string bound(const gt3r::InputManager& in, const Binding& b) const {
        std::string out;
        if (b.action >= 0) {
            for (const gt3r::Chord& c : in.actions(player_).of(gt3r::PadAction(b.action))) out += (out.empty() ? "" : ", ") + in.describe(player_, c);
            return out.empty() ? "-" : out;
        }
        const gt3r::Profile& p = b.menu ? in.profile(player_) : in.race_profile(player_);
        for (const gt3r::Binding& x : p.bindings)
            if (x.target == b.target) out += (out.empty() ? "" : ", ") + in.describe(player_, x.source);
        return out.empty() ? "-" : out;
    }

    void save(App& app) {
        input::save_config(app.input(), app.config());
        wheel_.save(app.config());
        app.save_config();
        changed_ = false;
    }

    int player_ = 0;
    bool to_game_ = false;                  // opened from the game's pages (the Options' controller pages): back goes to them
    std::string first_row_;                 // the row of the section the page was opened at, selected once the rows are there
    std::shared_ptr<GameOptions> options_;  // the game's own options shown here
    std::string message_;                   // a write of the saved game that failed
    input::WheelSettings wheel_;
    PageRows rows_;
    std::vector<std::optional<Binding>> bound_; // per row: what it binds
    Binding target_;                            // being captured
    int chord_ = -1;
    bool replace_ = true, capturing_ = false, changed_ = false, settle_ = false;
    double test_left_ = 0;
};

} // namespace

std::unique_ptr<Screen> make_controls_screen(App& app, int player, bool to_game, ControlsSection section, std::shared_ptr<GameOptions> options) {
    return std::make_unique<ControlsScreen>(app, std::clamp(player, 0, gt3r::InputManager::kMaxPlayers - 1), to_game, section, std::move(options));
}

} // namespace gt4r
