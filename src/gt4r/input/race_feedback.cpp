#include "gt4r/input/race_feedback.h"

#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>

namespace gt4r::input {

namespace ex = gt3r::sim::exact;
namespace rules = sim::rules;

namespace {

// The shared engine's shapes (VehicleTelemetry::traction_level and abs_level) on Gran Turismo 4's tire records.
float traction_at(const ex::Car& c, const sim::Extra& g, const rules::WheelLayout& l, int w) {
    const ex::Wheel& wh = c.w[size_t(w)];
    const rules::Tire& tire = rules::tire_record(g.tires, l, w, wh.surface);
    const float best = -tire.slip_drive.host(), peak = tire.peak_slip.host(), circle = wh.circle.host() * wh.peak_force.host();
    if (best <= 0 || peak <= 0) return 0;
    const float along = std::max(-wh.slip_ratio.host(), 0.0f) / best;
    const float angle = std::fabs(c.t[size_t(w)].slip_angle.host()) * wh.slip_fade.host() / peak;
    const float across = std::max(angle, circle > 0 ? std::hypot(wh.force_a.host(), wh.force_b.host()) / circle : 0.0f);
    const float grip = std::hypot(along, across);
    return grip < 1 ? 0.4f * std::clamp((grip - 0.85f) / 0.15f, 0.0f, 1.0f) : 0.4f + 0.6f * std::min(grip - 1, 1.0f);
}

// The wheels the TCS reads (0x3F79A0's: by the drive layout, the front's, the rear's or all).
bool driven(const sim::Extra& g, const rules::WheelLayout& l, int w) {
    const u8 layout = g.drive.d.layout;
    const s8 fa = l.fore_aft[size_t(w)];
    return layout == 1 ? fa > 0 : layout == 0 || (layout >= 5 && layout < 8) ? fa < 0 : true;
}

} // namespace

CarFeel car_feel(const sim::Session& s, size_t car) {
    CarFeel f;
    if (car >= s.cars().size()) return f;
    const ex::Car& c = s.record(car);
    const sim::Extra& g = s.gt4_record(car);
    const bool gt4 = s.gt4_rules();
    f.pad_output = c.control != 1 && c.control != 2 && c.driver_kind == 0;
    f.speed = std::fabs(c.speed.host());
    f.throttle = c.throttle_pedal.host(), f.brake = c.brake_pedal.host();
    f.rumble = gt4 || s.prologue_rules() ? g.feel.pad[1] : u8(std::clamp(int(c.rumble), 0, 255));
    f.small = gt4 || s.prologue_rules() ? g.feel.pad[0] : 0;
    f.steer_force = std::clamp(float(0x80 - int(s.steer_force(car))) / 127.0f, -1.0f, 1.0f);
    const rules::WheelLayout& l = s.wheel_layout(car);
    const int n = gt4 ? std::min<int>(l.count, 4) : 4;
    for (int w = 0; w < n; ++w) {
        const ex::Wheel& wh = c.w[size_t(w)];
        CarFeel::Wheel& o = f.wheels[size_t(w)];
        o.grounded = wh.load.host() > 0;
        o.slip_ratio = wh.slip_ratio.host();
        o.slide = wh.slip_intensity.host();
        const u8 surface = wh.surface;
        o.kerb = surface == 1 || surface == 10 || surface == 11 || surface == 21;
    }
    f.offroad = g.phases.soft_wheels != 0;
    if (gt4) {
        for (const sim::collide::Hit& h : s.hits()) {
            if (h.car != int(car) || h.kind == 2) continue;
            const float j = std::fabs(h.strength.host()) * (h.kind == 1 ? 15000.0f : 1.0f);
            f.impulse = std::max(f.impulse, j);
        }
        if (const float m = c.p.mass.host(); m > 0) f.impact = f.impulse / m;
        for (int w = 0; w < n; ++w) {
            const ex::Wheel& wh = c.w[size_t(w)];
            if (!(wh.load.host() > 0)) continue;
            if (f.throttle > 0.05f && driven(g, l, w)) f.traction = std::max(f.traction, traction_at(c, g, l, w));
            if (f.brake <= 0.05f) continue;
            const size_t axle = size_t(l.axle[size_t(w)] & 1);
            const float slip = wh.slip_ratio.host();
            const float target = rules::tire_record(g.tires, l, w, wh.surface).slip_brake.host() * g.aids.slip_scale[axle].host();
            if (target <= 0 || slip <= 0) continue;
            float level = 0.4f * std::clamp((slip / target - 0.85f) / 0.15f, 0.0f, 1.0f);
            if (const float over = slip - target; over > 0) {
                const float gain = g.aids.abs_gain[axle].host();
                level = 0.4f + 0.6f * std::min(1.0f, gain > 0 ? over * gain * wh.slip_gain.host() : over / 0.5f);
            }
            f.abs_level = std::max(f.abs_level, level);
        }
        // (the TCS holds the slip short of the tire's best: its cut counts by itself, but not a gear change's)
        if (g.aids.tcs_status == 2 && c.clutch != 3 && f.throttle > 0)
            f.traction = std::max(f.traction, 0.4f + 0.6f * std::clamp(1 - c.throttle.host() / f.throttle, 0.0f, 1.0f));
        f.traction *= std::min(f.throttle * 2, 1.0f);
        f.understeer = std::clamp(g.aids.understeer.host(), 0.0f, 1.0f);
        f.oversteer = std::clamp(g.aids.oversteer.host(), 0.0f, 1.0f);
    } else {
        f.impact = s.cars()[car].contact * 8.0f; // (Prologue's step: its contact's strength, 0 .. 1; ❓ judged)
    }
    return f;
}

FeedbackOptions FeedbackOptions::from(const PortConfig& port, u8 wheel) {
    FeedbackOptions o;
    o.vibration = get_feedback(port.of(Device::DualShock2), Device::DualShock2, "ON") != 0;
    const Device d = wheel == 1 ? Device::Cougar : wheel == 2 ? Device::Jaguar : Device::Cheetah;
    o.wheel = get_feedback(port.of(d), d, "ON") != 0;
    return o;
}

void RaceFeedback::step(const CarFeel& feel, float dt) {
    feel_ = feel;
    gt3r::sim::VehicleState s;
    gt3r::sim::VehicleTelemetry t;
    s.speed_kmh = feel.speed * 3.6f;
    gt3r::race::RumbleModel::Events e;
    for (size_t w = 0; w < 4; ++w) {
        s.wheels[w].grounded = feel.wheels[w].grounded;
        s.wheels[w].slip_ratio = feel.wheels[w].slip_ratio;
        t.smoke[w] = feel.wheels[w].slide;
        e.kerb = e.kerb || (feel.wheels[w].grounded && feel.wheels[w].kerb);
    }
    e.offroad = feel.offroad, e.impact = feel.impact;
    t.pad_output = feel.pad_output, t.pad_rumble = feel.rumble;
    gt3r::sim::ControlInputs in;
    in.throttle = feel.throttle, in.brake = feel.brake;
    model_.update(s, &t, e, in, dt);
    jolt_ = std::max(jolt_, std::min(1.0f, feel.impulse / 5000.0f));
    age_ = 0;
}

gt3r::PadRumble RaceFeedback::rumble(bool original, bool dualsense) const {
    if (!original) return model_.rumble(false, dualsense);
    gt3r::PadRumble r;
    if (!feel_.pad_output) return r;
    r.small = (feel_.small & 1) ? 255 : 0, r.large = feel_.rumble;
    return r;
}

gt3r::ForceFeedbackState RaceFeedback::force(const FeedbackOptions& options) const {
    gt3r::ForceFeedbackState f;
    f.speed_kmh = feel_.speed * 3.6f;
    f.brake = feel_.brake;
    if (feel_.pad_output) {
        f.abs_level = feel_.abs_level, f.traction = feel_.traction;
        f.steer_force = feel_.steer_force;
        f.jolt = jolt_;
        f.understeer = feel_.understeer, f.oversteer = feel_.oversteer;
    }
    f.triggers_on = options.vibration, f.wheel_on = options.wheel;
    return f;
}

void RaceFeedback::frame(gt3r::InputManager& input, int player, const FeedbackOptions& options, double dt) {
    if (!feeling() || !feel_.pad_output) return stop(input, player);
    age_ += dt;
    sent_ = true;
    input.set_rumble(player, options.vibration ? rumble(input.gt3_rumble(player), input.dualsense(player)) : gt3r::PadRumble{});
    if (options.vibration || options.wheel) input.set_force_feedback(player, force(options));
    else input.stop_force_feedback(player);
    jolt_ = 0;
}

void RaceFeedback::stop(gt3r::InputManager& input, int player) {
    age_ = 1e9, jolt_ = 0;
    model_.reset();
    if (!sent_) return;
    sent_ = false;
    input.set_rumble(player, {});
    input.stop_force_feedback(player);
}

} // namespace gt4r::input
