// The player's controls (src/gt4r/input): the controller settings and the frame the program's controller makes of a game
// port, against the user's own program (OptionRaceInputPortPS2: 0x43CCD0, 0x43CF90, 0x43D8A8, 0x43D520); our own layouts on
// the engine's pads, with simulated devices (SDL's virtual joysticks: no real device is opened); what a race sends back.

#include "menu_run_flow.h"
#include "test_framework.h"

#include "base/config.h"
#include "diff.h"
#include "gt4r/input/player_controls.h"
#include "gt4r/input/race_feedback.h"
#include "gt4r/menu/game_settings.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"
#include "orig.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>
#include <random>

using namespace gt4r;
using namespace gt4r::input;
using gt3r::PadAxis;
using gt3r::PadButton;
using gt3r::PadState;

namespace {

Disc* full_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d;
    return nullptr;
}

PadState pad_of(std::initializer_list<PadButton> buttons, float lx = 0) {
    PadState p;
    p.connected = true;
    for (PadButton b : buttons) p.buttons |= gt3r::u16(1u << unsigned(b)), p.pressure[size_t(b)] = 255;
    p.set_axis(PadAxis::LX, lx);
    return p;
}

} // namespace

TEST(input_key_config) {
    // The saved form and back; a preset; the scripts' calls.
    PortConfig c;
    u8 b[PortConfig::kSize];
    c.write(b);
    CHECK(b[0] == 1 && b[0x1D] == 1 && b[0x20] == 1 && b[0x2C] == 1 && b[0x44] == 1);
    PortConfig d;
    set_axis(d.of(Device::Cheetah), Device::Cheetah, "ACCEL", AxisAction::Brake);
    CHECK(!(d == c));
    d.read(b);
    CHECK(d == c);
    CHECK(port_from_text(port_text(c)) == c && port_from_text("") == c && port_from_text("zz") == c);
    CHECK(device_index("JOYSTICK") == 0 && device_index("DUALSHOCK 2") == 0 && device_index("COUGAR") == 1 && device_index("CHEETAH") == 3 && device_index("x") == -1);
    DeviceConfig& p = d.of(Device::DualShock2);
    CHECK(keys_with(p, Device::DualShock2, ButtonAction::Accel) == std::vector<std::string>{"CROSS"});
    CHECK(keys_with(p, Device::DualShock2, AxisAction::Steer) == std::vector<std::string>{"LX"});
    set_button(p, Device::DualShock2, "CROSS", ButtonAction::None);
    set_button(p, Device::DualShock2, "R2", ButtonAction::Accel);
    CHECK(keys_changed(p, Device::DualShock2));
    CHECK(p.preset == 0 && keys_with(p, Device::DualShock2, ButtonAction::Accel) == std::vector<std::string>{"R2"});
    set_preset(p, Device::DualShock2, 1);
    CHECK(p == c.of(Device::DualShock2));
    CHECK(button_action_name(button_action("BUTTON_LICENCE_BEST_LINE")) == "BUTTON_LICENCE_BEST_LINE" && axis_action("") == AxisAction::None);
    set_option(d, "VIBRATION", false);
    CHECK(!get_option(d, "VIBRATION") && get_option(d, "WHEEL FEEDBACK"));
    set_option(d, "WHEEL FEEDBACK", false);
    CHECK(!get_option(d, "WHEEL FEEDBACK") && get_feedback(d.of(Device::Cheetah), Device::Cheetah, "POWER") == 1);
    CHECK(!FeedbackOptions::from(d, 3).wheel && !FeedbackOptions::from(d, 3).vibration && FeedbackOptions::from(c, 3).wheel);
}

TEST(input_program_settings_and_frames_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    const u32 port = ee.alloc(0xC0, 16), saved = ee.alloc(0x80, 16);
    ee.call(0x43CCD0, {diff::Arg::ptr(port)});
    // A new game's settings, saved by the program's own writer; each preset by its own routine.
    const auto program_saved = [&] {
        for (u32 k = 0; k < 0x80; ++k) ee.w8(saved + k, 0xCC);
        ee.call(0x43D8A8, {diff::Arg::ptr(port), diff::Arg::ptr(saved)});
        std::vector<u8> out(PortConfig::kSize);
        ee.read_bytes(saved, out.data(), u32(out.size()));
        return out;
    };
    const auto ours_saved = [](const PortConfig& c) {
        std::vector<u8> out(PortConfig::kSize);
        c.write(out.data());
        return out;
    };
    PortConfig ours;
    CHECK(program_saved() == ours_saved(ours));
    constexpr u32 kAt[4] = {0x18, 0x48, 0x64, 0x8C}, kPreset[4] = {0x43B808, 0x43B8D8, 0x43B928, 0x43B9D8};
    bool presets = true;
    for (u32 dev = 0; dev < 4; ++dev)
        for (int preset = 0; preset < 4; ++preset) {
            ee.call(kPreset[dev], {diff::Arg::ptr(port + kAt[dev]), diff::Arg::integer(preset)});
            set_preset(ours.device[dev], Device(dev), preset);
            presets = presets && program_saved() == ours_saved(ours);
        }
    CHECK(presets);

    // Frames: made-up ports (every kind of pad and wheel, sticks, pressures, buttons) through made-up settings, the program's
    // 0x43CF90 against ours; every byte of the frame and the words the race reads.
    const u32 data = ee.alloc(0x30, 16), frame = ee.alloc(0x40, 16), in = ee.alloc(0x200, 16);
    std::mt19937 rng(4);
    const auto below = [&](u32 n) { return int(rng() % n); };
    int equal = 0, runs = 0, shown = 0;
    int kinds[7] = {};
    for (int run = 0; run < 3000; ++run) {
        PortConfig c;
        if (run % 3) {
            for (size_t dev = 0; dev < kDeviceCount; ++dev) {
                for (s8& v : c.device[dev].axes)
                    if (below(3) == 0) v = s8(below(u32(AxisAction::Count) + 1) - 1);
                for (s8& v : c.device[dev].buttons)
                    if (below(3) == 0) v = s8(below(u32(ButtonAction::Count) + 1) - 1);
            }
        }
        u8 b[PortConfig::kSize];
        c.write(b);
        for (u32 k = 0; k < PortConfig::kSize; ++k) ee.w8(saved + k, b[k]);
        ee.call(0x43D8F0, {diff::Arg::ptr(port), diff::Arg::ptr(saved)});
        GamePort p;
        p.pad = GamePort::Pad(below(6));
        p.wheel = u8(below(3) ? 0 : 1 + below(3));
        for (int k = 0; k < 4; ++k) p.axis[size_t(k)] = gt3r::s16(below(3) ? below(256) : 128);
        for (int k = 4; k < 16; ++k) p.axis[size_t(k)] = gt3r::s16(below(2) ? 0 : below(256));
        p.make_halves();
        p.axis[24] = gt3r::s16(below(16384) - 8192), p.axis[25] = gt3r::s16(below(2) ? below(256) : 0), p.axis[26] = gt3r::s16(below(2) ? below(256) : 0);
        for (int bit = 0; bit < 64; ++bit)
            if (below(run % 5 == 0 ? 2 : 8) == 0) p.set(bit);
        const int mode = below(3);
        // The port's report: its kind, the axes (a size, a count, the values) and the buttons (a size, how many, the bytes).
        u32 at = in;
        ee.w16(at, 0x80), ee.w16(at + 2, gt3r::u16(u32(p.pad) << 4 | p.wheel));
        const u32 axes = at + 4;
        ee.w16(axes, 4 + 2 * 27), ee.w16(axes + 2, 27);
        for (u32 k = 0; k < 27; ++k) ee.w16(axes + 4 + 2 * k, gt3r::u16(p.axis[k]));
        const u32 buttons = axes + 4 + 2 * 27;
        ee.w16(buttons, 5 + 8), ee.w16(buttons + 2, 3), ee.w8(buttons + 4, 64);
        for (u32 k = 0; k < 8; ++k) ee.w8(buttons + 5 + k, u8(p.buttons >> (8 * k)));
        for (u32 k = 0; k < 0x24; ++k) ee.w8(data + k, 0x5A);
        for (u32 k = 0; k < 0x20; ++k) ee.w8(frame + k, 0x5A);
        ee.w32(data, frame);
        ee.call(0x43CF90, {diff::Arg::ptr(port), diff::Arg::ptr(data), diff::Arg::ptr(in), diff::Arg::integer(mode == 0 ? 0 : mode == 1 ? 1 : 2)});
        const RaceInput got = read_port(p, c, mode == 0 ? Mode::Drive : mode == 1 ? Mode::Watch : Mode::DriveNoDirections);
        u8 theirs[32], mine[32];
        ee.read_bytes(frame, theirs, 32);
        got.frame.to_bytes(mine);
        bool same = std::memcmp(theirs, mine, 32) == 0 && ee.r32(data + 0x18) == got.menu;
        for (u32 k = 0; k < 4; ++k) same = same && std::fabs(ee.rf(data + 8 + 4 * k) - got.stick[k]) < 1e-5f;
        if (mode != 1) same = same && ee.r32(data + 4) == got.views && ee.r32(data + 0x20) == got.special; // (a replay's own words: not ours)
        if (!same && shown++ < 5) {
            std::printf("  run %d mode %d pad %d wheel %d:\n   ", run, mode, int(p.pad), p.wheel);
            for (u8 v : theirs) std::printf(" %02x", v);
            std::printf("  %x %x %x\n   ", ee.r32(data + 4), ee.r32(data + 0x18), ee.r32(data + 0x20));
            for (u8 v : mine) std::printf(" %02x", v);
            std::printf("  %x %x %x\n", got.views, got.menu, got.special);
        }
        ++runs, equal += same;
        if (got.frame.kind < 7) ++kinds[got.frame.kind];
    }
    CHECK(runs == 3000 && equal == runs);
    CHECK(kinds[0] > 100 && kinds[2] > 100 && kinds[3] > 100 && kinds[4] > 100 && kinds[6] > 50);

    // What the port sends to the devices (0x43D520): the pad's two motors as the race's block has them with the vibration on,
    // nothing with it off; the force only with a wheel plugged in. And the stop (0x43D680).
    const u32 block = ee.alloc(0x10, 16);
    ee.call(0x43CCD0, {diff::Arg::ptr(port)});
    for (u32 k = 0; k < 0x10; ++k) ee.w8(block + k, u8(0x21 + k));
    ee.w8(block + 1, 90);
    ee.w32(port + 4, 0x50); // (a DualShock 2, no wheel)
    const u32 out = u32(ee.call(0x43D520, {diff::Arg::ptr(port), diff::Arg::ptr(block)}).v0.lo);
    CHECK(ee.r8(out + 2) == 0x21 && ee.r8(out + 3) == 90 && ee.r8(out + 5) == 0);
    ee.w32(port + 4, 0x53); // (and a Driving Force Pro)
    ee.call(0x43D520, {diff::Arg::ptr(port), diff::Arg::ptr(block)});
    CHECK(ee.r8(out + 4) == 0x23 && ee.r8(out + 5) == 0);
    ee.call(0x43C160, {diff::Arg::ptr(port + 0x18), diff::Arg::integer(0), diff::Arg::integer(0)}); // the vibration off
    ee.call(0x43D520, {diff::Arg::ptr(port), diff::Arg::ptr(block)});
    CHECK(ee.r8(out + 2) == 0 && ee.r8(out + 3) == 0);
    ee.call(0x43D680, {diff::Arg::ptr(port)});
    CHECK(ee.r8(out + 2) == 0 && ee.r8(out + 3) == 0 && ee.r8(out + 4) == 0 && ee.r8(out + 5) == 1);
}

TEST(input_modern_layout) {
    // The modern layout on the race pad: the stick is the game's analog stick, the triggers proportional pedals.
    PadState race = pad_of({}, 0.5f);
    RaceInput in = modern_input({}, race);
    CHECK(in.frame.kind == 2 && (in.frame.flags & 1) && std::abs(in.frame.steer + 2048) <= 1);
    race = pad_of({}, -1);
    in = modern_input({}, race);
    CHECK(in.frame.steer == 4096 && in.frame.throttle == 0 && !(in.frame.flags & 2));
    in = modern_input({}, {});
    CHECK(in.frame.kind == 0 && in.frame.flags == 0 && in.frame.steer == 0);
    // A trigger at half and at full travel.
    race = pad_of({PadButton::Cross, PadButton::Square});
    race.pressure[size_t(PadButton::Cross)] = 128;
    in = modern_input({}, race);
    CHECK((in.frame.flags & 6) == 6 && std::abs(int(in.frame.throttle) - 2056) <= 2 && in.frame.brake == 4096);
    // The direction buttons steer digitally, before the stick; shifts, the handbrake, reverse, the views.
    race = pad_of({PadButton::Left, PadButton::R2, PadButton::Circle, PadButton::Triangle, PadButton::R1, PadButton::L1}, 0.7f);
    in = modern_input({}, race);
    CHECK(in.frame.kind == 0 && !(in.frame.flags & 1) && in.frame.steer == 2 && in.frame.shift == 1 && in.frame.third == 1 && in.frame.back == 1);
    CHECK(in.views == (RaceInput::kPlayView | RaceInput::kBackView));
    race = pad_of({PadButton::L2, PadButton::R2});
    CHECK(modern_input({}, race).frame.shift == 0);
    CHECK(modern_input({}, pad_of({PadButton::Down})).frame.nitrous == 1 && modern_input(pad_of({PadButton::L3}), {}).frame.nitrous == 1);
    CHECK(modern_input(pad_of({PadButton::R1}), {}).frame.nitrous == 1 && modern_input(pad_of({PadButton::R1}), pad_of({PadButton::R2})).frame.nitrous == 0);
    CHECK(modern_input(pad_of({PadButton::Select}), {}).views == RaceInput::kPlayView);
}

TEST(input_console_layout) {
    // The console's pad through the Key Configuration: a pad without pressures is a DualShock.
    PortConfig c;
    PadState menu = pad_of({PadButton::Cross, PadButton::Circle, PadButton::R2, PadButton::L1}, -0.25f);
    RaceInput in = read_port(pad_port(menu, menu), c);
    CHECK(in.frame.kind == 2 && std::abs(in.frame.steer - 1024) <= 1 && in.frame.throttle == 1 && !(in.frame.flags & 2) && in.frame.third == 1 && in.frame.shift == 1);
    CHECK(in.views == RaceInput::kBackView);
    // R2 accelerates and L2 brakes by the page: the triggers' travel does it.
    DeviceConfig& p = c.of(Device::DualShock2);
    set_button(p, Device::DualShock2, "R2", ButtonAction::Accel), set_button(p, Device::DualShock2, "L2", ButtonAction::Brake);
    set_button(p, Device::DualShock2, "CROSS", ButtonAction::ShiftUp);
    menu.pressure[size_t(PadButton::R2)] = 64;
    in = read_port(pad_port(menu, menu), c);
    CHECK((in.frame.flags & 2) && std::abs(int(in.frame.throttle) - 1028) <= 2 && in.frame.shift == 1 && in.frame.brake == 0);
    // The right stick's halves accelerate and brake as a new game sets them.
    PadState stick;
    stick.set_axis(PadAxis::RY, -1);
    in = read_port(pad_port(stick, stick), PortConfig());
    CHECK((in.frame.flags & 2) && in.frame.throttle > 1500 && in.frame.brake == 0);
    // The pad's directions taken by something else (the PIT STOP menu): they do not steer.
    menu = pad_of({PadButton::Left, PadButton::Cross});
    CHECK(read_port(pad_port(menu, menu), PortConfig()).frame.steer == 2 && read_port(pad_port(menu, menu), PortConfig(), Mode::DriveNoDirections).frame.steer == 0);
    CHECK(read_port(pad_port(menu, menu), PortConfig(), Mode::DriveNoDirections).frame.throttle == 1);
}

TEST(input_wheel) {
    // A 900 degree wheel as the Driving Force Pro: the frame's 4096 is 450 degrees; a wheel set to 450 degrees asks for half of
    // it at its lock; as a Driving Force (200 degrees) the same wheel is at full command from 100 degrees on.
    WheelSettings w;
    PortConfig c;
    PadState race = pad_of({}, 1);
    RaceInput in = read_port(wheel_port({}, race, w), c);
    CHECK(in.frame.kind == 6 && (in.frame.flags & 1) && in.frame.steer < -4090);
    w.rotation = 450;
    in = read_port(wheel_port({}, race, w), c);
    CHECK(std::abs(in.frame.steer + 2048) <= 1);
    w.wheel = 2, w.rotation = 900;
    race = pad_of({}, -100.0f / 450.0f);
    in = read_port(wheel_port({}, race, w), c);
    CHECK(in.frame.kind == 4 && in.frame.steer > 4085);
    // No dead zone: the least turn of the axis is in the frame; the center is a wheel's all the same.
    w = {};
    race = pad_of({}, 0.0005f);
    CHECK(read_port(wheel_port({}, race, w), c).frame.steer < 0);
    in = read_port(wheel_port({}, {}, w), c);
    CHECK(in.frame.kind == 6 && (in.frame.flags & 1) && in.frame.steer == 0 && in.frame.throttle == 0);
    // Pedals, paddles, buttons; a rebound button through its own page.
    race = pad_of({PadButton::Cross, PadButton::R1, PadButton::Triangle});
    race.pressure[size_t(PadButton::Cross)] = 100;
    in = read_port(wheel_port(pad_of({PadButton::Select}), race, w), c);
    CHECK((in.frame.flags & 2) && std::abs(int(in.frame.throttle) - 1606) <= 2 && in.frame.shift == 1 && in.frame.back == 1 && in.views == RaceInput::kPlayView);
    set_button(c.of(Device::Cheetah), Device::Cheetah, "R1", ButtonAction::BackView);
    CHECK(read_port(wheel_port({}, race, w), c).frame.shift == 0);
    gt3r::IniDocument ini = gt3r::IniDocument::parse("[wheel]\nkind = driving_force\nrotation = 540\n");
    CHECK(WheelSettings::load(ini).wheel == 2 && WheelSettings::load(ini).rotation == 540);
    CHECK(WheelSettings::load(gt3r::IniDocument::parse("[wheel]\nrotation = 5\n")).rotation == 90);
}

TEST(input_config_words) {
    gt3r::IniDocument ini = gt3r::IniDocument::parse("[player1]\nrace = original\nrumble = original\n[ffb]\nwheel_mode = modern\n");
    translate_config(ini);
    CHECK(ini.get("player1", "race") == "gt3" && ini.get("player1", "rumble") == "gt3" && ini.get("ffb", "wheel_mode") == "modern");
    CHECK(our_word("gt3") == "original" && engine_word("auto") == "auto");
}

TEST(input_feedback_models) {
    FeedbackOptions on;
    RaceFeedback f;
    CarFeel c;
    c.pad_output = true, c.speed = 30, c.rumble = 96, c.steer_force = 0.5f;
    f.step(c, 1 / 60.0f);
    // The original's: the level on the large motor, the small one never. Modern: a smooth road is still.
    CHECK(f.rumble(true, false).large == 96 && f.rumble(true, false).small == 0);
    CHECK(f.rumble(false, false).large == 0 && f.rumble(false, false).small == 0);
    CHECK(std::fabs(f.force(on).steer_force - 0.5f) < 1e-6f && f.feeling());
    // A hit of 6 m/s: both motors, the large leaning; a DualSense's closer together.
    c.impact = 6, c.impulse = 7000;
    f.step(c, 1 / 60.0f);
    const gt3r::PadRumble pad = f.rumble(false, false), sense = f.rumble(false, true);
    CHECK(pad.large > 150 && pad.small > 0 && pad.small < pad.large && sense.small > pad.small);
    CHECK(f.force(on).jolt == 1);
    // A quarter of a second later it is over.
    c.impact = c.impulse = 0;
    for (int i = 0; i < 20; ++i) f.step(c, 1 / 60.0f);
    CHECK(f.rumble(false, false).large == 0);
    // Off the road; a slide; locked wheels: the tires lean on the small motor.
    c.offroad = true;
    f.step(c, 1 / 60.0f);
    CHECK(f.rumble(false, false).large > f.rumble(false, false).small && f.rumble(false, false).small > 0);
    c.offroad = false;
    for (CarFeel::Wheel& w : c.wheels) w.grounded = true, w.slide = 1.2f;
    f.step(c, 1 / 60.0f);
    CHECK(f.rumble(false, false).small > f.rumble(false, false).large);
    // A kerb pulses: in half a second both on and off.
    for (CarFeel::Wheel& w : c.wheels) w.slide = 0, w.kerb = true;
    int felt = 0;
    for (int i = 0; i < 30; ++i) f.step(c, 1 / 60.0f), felt += f.rumble(false, false).small > 0;
    CHECK(felt > 5 && felt < 25);
    // A car the computer drives: nothing, whatever it meets.
    c.pad_output = false, c.impact = 10;
    f.step(c, 1 / 60.0f);
    CHECK(f.rumble(false, true).large == 0 && f.rumble(true, true).large == 0 && f.force(on).steer_force == 0 && f.force(on).jolt == 0);
}

// What a car feels on a real course, driven by the controller's own frames: the engine's and program's numbers behind the
// rumble, the triggers and the wheel.
TEST(input_feel_from_a_drive) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::string car = "nsx_r_02";
    const auto drive = [&](int steps, auto&& pad_at, auto&& each) {
        const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "tsukuba", std::span<const std::string>(&car, 1));
        for (int i = 0; i < steps; ++i) {
            race::PadRecord record;
            race::CarInput in;
            const RaceInput r = modern_input({}, pad_at(i));
            r.frame.to_bytes(record.bytes.data());
            in.pad = &record;
            s->step_once(std::span<const race::CarInput>(&in, 1));
            each(i, car_feel(*s, 0));
        }
        return s->cars()[0].speed;
    };
    const auto pedals = [](float gas, float brake, float steer) {
        PadState p = pad_of({}, steer);
        if (gas > 0) p.buttons |= gt3r::u16(1u << unsigned(PadButton::Cross)), p.pressure[size_t(PadButton::Cross)] = u8(gas * 255);
        if (brake > 0) p.buttons |= gt3r::u16(1u << unsigned(PadButton::Square)), p.pressure[size_t(PadButton::Square)] = u8(brake * 255);
        return p;
    };
    // Half and full throttle: proportional. A standing start at full throttle takes the driven tires a little past their best:
    // the throttle's buzz faint; at 40 % nothing.
    float half_traction = 0, full_traction = 0;
    const float half = drive(300, [&](int) { return pedals(0.4f, 0, 0); }, [&](int, const CarFeel& f) { half_traction = std::max(half_traction, f.traction); });
    const float full = drive(300, [&](int) { return pedals(1, 0, 0); }, [&](int, const CarFeel& f) { full_traction = std::max(full_traction, f.traction); });
    std::printf("  5 s at 40 %% and full throttle: %.1f and %.1f km/h; the edge of grip %.2f and %.2f\n", double(half) * 3.6, double(full) * 3.6, double(half_traction), double(full_traction));
    CHECK(half > 5 && full > half * 1.2f && half_traction == 0 && full_traction > 0 && full_traction < 0.3f);
    // Braking hard from speed: the ABS. Off the pedals it is still.
    float abs = 0, coast = 0;
    drive(500, [&](int i) { return i < 300 ? pedals(1, 0, 0) : i < 400 ? pedals(0, 1, 0) : pedals(0, 0, 0); },
          [&](int i, const CarFeel& f) { (i >= 300 && i < 400 ? abs : coast) = std::max(i >= 300 && i < 400 ? abs : coast, f.abs_level); });
    std::printf("  the ABS braking: %.2f, not braking %.2f\n", double(abs), double(coast));
    CHECK(abs > 0.2f && coast == 0);
    // Steering at speed: the wheel's force pushes back, the other way for the other side (the frame's left is positive, the
    // force's positive is clockwise).
    float left = 0, right = 0, straight = 0;
    drive(280, [&](int i) { return pedals(1, 0, i < 240 ? 0.0f : -0.3f); }, [&](int i, const CarFeel& f) { (i < 240 ? straight : left) = i < 240 ? std::max(straight, i > 120 ? std::fabs(f.steer_force) : 0) : f.steer_force; });
    drive(280, [&](int i) { return pedals(1, 0, i < 240 ? 0.0f : 0.3f); }, [&](int i, const CarFeel& f) { if (i >= 240) right = f.steer_force; });
    std::printf("  the wheel's force: straight %.2f at most, steering left %.2f, right %.2f\n", double(straight), double(left), double(right));
    CHECK(left > 0.1f && right < -0.1f && straight < 0.3f);
    // Off the road and into the wall: rough ground, then a hit, felt by both rumbles.
    RaceFeedback feedback;
    bool rough = false, kerb = false;
    float impact = 0;
    int original = 0, modern = 0;
    drive(900, [&](int i) { return pedals(1, 0, i < 200 ? 0.0f : 0.15f); }, [&](int, const CarFeel& f) {
        rough = rough || f.offroad, impact = std::max(impact, f.impact);
        for (const CarFeel::Wheel& w : f.wheels) kerb = kerb || w.kerb;
        feedback.step(f, 1 / 60.0f);
        original = std::max(original, int(feedback.rumble(true, false).large)), modern = std::max(modern, int(feedback.rumble(false, false).large));
        CHECK(feedback.rumble(true, false).small == 0);
    });
    std::printf("  off the road: %d, a kerb %d, the hit %.1f m/s; the motor at most %d (original), %d (Modern)\n", rough, kerb, double(impact), original, modern);
    CHECK(rough && impact > 1.5f && original > 20 && modern > 100);
}

TEST(input_full_presses_as_the_console_pad) {
    // A DualShock 2's Cross and Square pressed fully (its pressures 255), the modern layout's triggers fully down and the
    // console layout's buttons on a pad without pressures: the same frame, or the same pedals in the step.
    GamePort ds2;
    ds2.pad = GamePort::Pad::DualShock2;
    ds2.set(kPadCross), ds2.set(kPadSquare);
    ds2.axis[10] = 255, ds2.axis[11] = 255; // (the pressures of Cross and Square)
    ds2.make_halves();
    const RaceInput console = read_port(ds2, PortConfig());
    const RaceInput modern = modern_input({}, pad_of({PadButton::Cross, PadButton::Square}));
    CHECK(console.frame.throttle == 4096 && console.frame.brake == 4096 && (console.frame.flags & 6) == 6);
    std::array<u8, 32> a{}, b{};
    console.frame.to_bytes(a.data()), modern.frame.to_bytes(b.data());
    CHECK(a == b);
    // The console's pressure reaches the full pedal early (the throttle at 45 of 255, the brake at 84); the triggers over
    // their whole travel.
    ds2.axis[10] = 45, ds2.axis[11] = 84;
    CHECK(read_port(ds2, PortConfig()).frame.throttle == 4096 && read_port(ds2, PortConfig()).frame.brake == 4096);
    ds2.axis[10] = 44, ds2.axis[11] = 83;
    CHECK(read_port(ds2, PortConfig()).frame.throttle < 4096 && read_port(ds2, PortConfig()).frame.brake < 4096);

    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // B-1's car and course: 6 s at full throttle, then full brake. The digital pedals ramp (0x620270) as the analog ones slew
    // (0x622121 set), so the three runs are equal in every bit.
    const std::string car = "demio_glx_99";
    const auto drive = [&](auto&& frame_at) {
        const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "testcourse_100m", std::span<const std::string>(&car, 1));
        std::vector<float> speeds;
        for (int i = 0; i < 560; ++i) {
            race::PadRecord record;
            race::CarInput in;
            frame_at(i).to_bytes(record.bytes.data());
            in.pad = &record;
            s->step_once(std::span<const race::CarInput>(&in, 1));
            speeds.push_back(s->cars()[0].speed);
        }
        return speeds;
    };
    const auto gas = [](int i) { return i < 360; };
    const std::vector<float> pressures = drive([&](int i) {
        GamePort p;
        p.pad = GamePort::Pad::DualShock2;
        p.set(gas(i) ? kPadCross : kPadSquare);
        p.axis[gas(i) ? 10 : 11] = 255;
        p.make_halves();
        return read_port(p, PortConfig()).frame;
    });
    const std::vector<float> triggers = drive([&](int i) { return modern_input({}, pad_of({gas(i) ? PadButton::Cross : PadButton::Square})).frame; });
    const std::vector<float> buttons = drive([&](int i) {
        const PadState p = pad_of({gas(i) ? PadButton::Cross : PadButton::Square});
        return read_port(pad_port(p, p), PortConfig()).frame;
    });
    std::printf("  full throttle 6 s: %.2f km/h, then 200 steps of full brake: %.2f km/h\n", double(pressures[359]) * 3.6, double(pressures.back()) * 3.6);
    CHECK(pressures[359] * 3.6f > 50 && pressures == triggers && pressures == buttons);
}

namespace {

// A made-up pad, no real device opened (the real drivers are off; only SDL's virtual one runs).
struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* joystick = nullptr;
    int rumbles = 0;
    Uint16 low = 0, high = 0;
    bool ok = false;
    static bool SDLCALL rumble(void* user, Uint16 l, Uint16 h) {
        VirtualPad* p = static_cast<VirtualPad*>(user);
        ++p->rumbles, p->low = l, p->high = h;
        return true;
    }
    bool plug() {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT, d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name = "GT4R test pad";
        d.userdata = this, d.Rumble = &VirtualPad::rumble;
        id = SDL_AttachVirtualJoystick(&d);
        joystick = id ? SDL_OpenJoystick(id) : nullptr;
        return joystick != nullptr;
    }
    void unplug() {
        if (joystick) SDL_CloseJoystick(joystick);
        if (id) SDL_DetachVirtualJoystick(id);
        joystick = nullptr, id = 0;
    }
    void axis(SDL_GamepadAxis a, float v) { SDL_SetJoystickVirtualAxis(joystick, int(a), Sint16(std::lround(v * 32767))); }
    void button(SDL_GamepadButton b, bool down) { SDL_SetJoystickVirtualButton(joystick, int(b), down); }
};

void pump(gt3r::InputManager& in) {
    SDL_UpdateJoysticks();
    SDL_Event e;
    while (SDL_PollEvent(&e)) in.handle_event(e);
    in.update(1 / 60.0);
}

} // namespace

// The two-player battle's two pads: each its own player's, read into its own frame and driving its own car on one session; the
// feeling of a player's car goes to his pad alone.
TEST(input_two_simulated_devices) {
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_MFI, SDL_HINT_JOYSTICK_IOKIT, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_XINPUT_ENABLED}) SDL_SetHint(hint, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) SKIP("SDL's joysticks do not start here");
    {
        gt3r::InputManager in;
        in.load(gt3r::IniDocument{});
        PortConfig config[2];
        const WheelSettings wheel;
        pump(in);
        VirtualPad pad[2];
        if (!pad[0].plug() || !pad[1].plug()) {
            std::printf("  no virtual joysticks: %s\n", SDL_GetError());
        } else {
            for (VirtualPad& p : pad) p.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -1), p.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -1);
            pump(in), pump(in);
            CHECK(in.gamepad(0) && in.gamepad(1) && in.gamepad(0) != in.gamepad(1));
            // The first pad on the throttle, the second's stick over: each in its own player's frame.
            pad[0].axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), pad[1].axis(SDL_GAMEPAD_AXIS_LEFTX, -1);
            pump(in), pump(in);
            const RaceInput a = read_player(in, 0, config[0], wheel), b = read_player(in, 1, config[1], wheel);
            CHECK(a.frame.throttle == 4096 && a.frame.steer == 0 && b.frame.throttle == 0 && b.frame.steer == 4096);
            // The second's own Key Configuration: only his layout changes.
            set_button(config[1].of(Device::DualShock2), Device::DualShock2, "SQUARE", ButtonAction::None);
            CHECK(!console_layout(in, 0, config[0]) && console_layout(in, 1, config[1]));
            config[1] = {};
            // The second player's menus do not move by his pad (the pause and the results are the first player's).
            pad[1].button(SDL_GAMEPAD_BUTTON_START, true);
            pump(in), pump(in);
            CHECK(!(in.menu_pad().buttons & (1u << unsigned(PadButton::Start))));
            pad[1].button(SDL_GAMEPAD_BUTTON_START, false);
            pump(in), pump(in);
            // A step's feeling of the second player's car: his pad's motors alone.
            RaceFeedback feedback[2];
            CarFeel feel;
            feel.pad_output = true, feel.rumble = 100, feel.impact = 8;
            feedback[1].step(feel, 1 / 60.0f);
            feedback[0].frame(in, 0, {}, 1 / 60.0), feedback[1].frame(in, 1, {}, 1 / 60.0);
            pump(in);
            CHECK(pad[1].low > 0 && pad[0].low == 0 && pad[0].high == 0);
            for (size_t i = 0; i < 2; ++i) feedback[i].stop(in, int(i));
            pump(in);
            CHECK(pad[1].low == 0 && pad[1].high == 0);
            // Both cars on one session by their frames.
            if (Disc* disc = full_disc()) {
                const std::string cars[2] = {"ka_01", "ka_01"};
                const std::unique_ptr<sim::Session> s = sim::Session::open(*disc, "tsukuba", cars);
                pad[1].axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
                const auto run = [&](int steps) {
                    for (int k = 0; k < steps; ++k) {
                        pump(in);
                        race::PadRecord record[2];
                        race::CarInput inputs[2];
                        for (size_t i = 0; i < 2; ++i) {
                            read_player(in, int(i), config[i], wheel).frame.to_bytes(record[i].bytes.data());
                            inputs[i].pad = &record[i];
                        }
                        s->step_once(inputs);
                    }
                };
                run(240);
                std::printf("  the first pad's throttle: %.1f and %.1f km/h\n", double(s->cars()[0].speed) * 3.6, double(s->cars()[1].speed) * 3.6);
                CHECK(s->cars()[0].speed > 5 && s->cars()[1].speed < 1);
                pad[0].axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -1), pad[0].axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), pad[1].axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1);
                run(240);
                std::printf("  its brake, and the second's throttle: %.1f and %.1f km/h\n", double(s->cars()[0].speed) * 3.6, double(s->cars()[1].speed) * 3.6);
                CHECK(s->cars()[0].speed < 2 && s->cars()[1].speed > 5);
            }
            // The second pad unplugged: no device of his own; the keyboard's second set of keys can drive him.
            pad[1].unplug();
            pump(in), pump(in);
            CHECK(in.gamepad(0) && !in.gamepad(1) && in.device_name(1) == "No device" && in.device_filter(1) == "auto");
            in.set_keyboard(1, true);
            CHECK(in.device_name(1) == "Keyboard" && in.kind(1) == gt3r::DeviceKind::Keyboard);
        }
        for (VirtualPad& p : pad) p.unplug();
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
}

TEST(input_simulated_devices) {
    for (const char* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_MFI, SDL_HINT_JOYSTICK_IOKIT, SDL_HINT_JOYSTICK_GAMEINPUT, SDL_HINT_XINPUT_ENABLED}) SDL_SetHint(hint, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) SKIP("SDL's joysticks do not start here");
    {
        gt3r::InputManager in;
        in.load(gt3r::IniDocument{});
        PortConfig config;
        const WheelSettings wheel;
        pump(in);
        VirtualPad pad;
        if (!pad.plug()) {
            std::printf("  no virtual joystick: %s\n", SDL_GetError());
        } else {
            // Plugged in while the game runs.
            pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -1), pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -1);
            pump(in), pump(in);
            CHECK(in.gamepad(0) != nullptr && in.kind(0) == gt3r::DeviceKind::Pad);
            CHECK(!console_layout(in, 0, config));
            // The stick and the triggers in the modern layout.
            pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 1), pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
            pump(in), pump(in);
            RaceInput r = read_player(in, 0, config, wheel);
            CHECK(r.frame.kind == 2 && r.frame.steer == -4096 && r.frame.throttle == 4096 && r.frame.brake > 1800 && r.frame.brake < 2300);
            // Within the stick's dead zone nothing.
            pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0.05f);
            pump(in), pump(in);
            CHECK(read_player(in, 0, config, wheel).frame.steer == 0);
            pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
            pump(in), pump(in);
            CHECK(read_player(in, 0, config, wheel).frame.shift == 1);
            // The Key Configuration changed by hand: the pad is the console's, so Cross (the south button) accelerates.
            set_button(config.of(Device::DualShock2), Device::DualShock2, "SQUARE", ButtonAction::None);
            CHECK(console_layout(in, 0, config));
            pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -1);
            pump(in), pump(in);
            r = read_player(in, 0, config, wheel);
            CHECK(r.frame.throttle == 1 && r.frame.shift == 1); // (R2, the trigger held, shifts up)
            config = {};
            // The motors get the race's feeling while steps come, and stop when they do not.
            RaceFeedback feedback;
            CarFeel feel;
            feel.pad_output = true, feel.rumble = 100, feel.impact = 8;
            FeedbackOptions on;
            in.set_rumble_mode(0, "gt3");
            feedback.step(feel, 1 / 60.0f), feedback.frame(in, 0, on, 1 / 60.0);
            CHECK(pad.low == 100 * 257 && pad.high == 0);
            in.set_rumble_mode(0, "auto");
            feedback.step(feel, 1 / 60.0f), feedback.frame(in, 0, on, 1 / 60.0);
            CHECK(pad.low > 40000 && pad.high > 10000);
            for (int i = 0; i < 12; ++i) feedback.frame(in, 0, on, 1 / 60.0);
            CHECK(pad.low == 0 && pad.high == 0 && !feedback.feeling());
            feedback.step(feel, 1 / 60.0f), feedback.frame(in, 0, on, 1 / 60.0);
            CHECK(pad.low > 0);
            feedback.stop(in, 0); // (a pause)
            CHECK(pad.low == 0 && pad.high == 0);
            on.vibration = false; // (the game's option)
            feedback.step(feel, 1 / 60.0f), feedback.frame(in, 0, on, 1 / 60.0);
            CHECK(pad.low == 0 && pad.high == 0);
            // Unplugged: nothing held stays held.
            pad.unplug();
            pump(in), pump(in);
            r = read_player(in, 0, config, wheel);
            CHECK(in.gamepad(0) == nullptr && r.frame.throttle == 0 && r.frame.steer == 0 && r.frame.shift == 0);
        }
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
}
TEST(input_option_pages) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    using menu::Key;
    // Each page from the title's Options, changed and left by its OK button; what the races read then.
    const auto open = [&](Flow& f, std::initializer_list<Key> to) {
        bool ok = f.boot_to_title();
        f.press({Key::Down, Key::Down, Key::Down, Key::Accept});
        ok = ok && f.rt->finished() && f.rt->advance();
        f.rt->settle();
        f.press(to);
        return ok;
    };
    const auto focus_on = [](Flow& f, const std::string& part) {
        for (int i = 0; i < 24 && (!f.rt->focused() || f.rt->focused()->self()->path().find(part) == std::string::npos); ++i) f.press({Key::Down});
        return f.rt->focused() && f.rt->focused()->self()->path().find(part) != std::string::npos;
    };
    const auto settings = [](Flow& f) { return menu::settings_of(f.rt->game()); };
    { // Key Configuration, DUALSHOCK 2 1P: the accelerator from Cross to R2 (three rows down its list).
        Flow f(*disc);
        CHECK(open(f, {Key::Down, Key::Down, Key::Down, Key::Right, Key::Accept}) && f.page() == "KeyconfigRoot");
        const auto keys = [&](ButtonAction a) { return keys_with(settings(f).controllers.port[0].of(Device::DualShock2), Device::DualShock2, a); };
        CHECK(keys(ButtonAction::Accel) == std::vector<std::string>{"CROSS"});
        // The next key of its list until one that nothing else has (the page refuses a key taken twice).
        int tries = 0, refused = 0;
        for (; tries < 14 && f.page() == "KeyconfigRoot"; ++tries) {
            CHECK(focus_on(f, "Accel"));
            f.press({Key::Accept, Key::Down, Key::Accept});
            CHECK(focus_on(f, "Buttons::OK"));
            f.press({Key::Accept});
            if (f.page() == "ConfirmDialog") f.press({Key::Accept}), ++refused;
        }
        std::printf("  %d keys tried, %d refused\n", tries, refused);
        const std::vector<std::string> accel = keys(ButtonAction::Accel);
        std::printf("  the accelerator: %s\n", accel.empty() ? "none" : accel[0].c_str());
        CHECK(accel.size() == 1 && accel[0] != "CROSS");
        CHECK(settings(f).controllers.port[0].of(Device::DualShock2).preset == 0 && settings(f).controllers.port[1] == PortConfig() && f.clean());
    }
    { // The page left by OK as it was.
        Flow f(*disc);
        CHECK(open(f, {Key::Down, Key::Down, Key::Down, Key::Right, Key::Accept}) && f.page() == "KeyconfigRoot");
        CHECK(focus_on(f, "Buttons::OK"));
        f.press({Key::Accept});
        const PortConfig c = settings(f).controllers.port[0];
        CHECK(f.page() == "OptionRoot" && c.of(Device::DualShock2).preset == 0 && !keys_changed(c.of(Device::DualShock2), Device::DualShock2));
    }
    { // Vibration 1P off.
        Flow f(*disc);
        CHECK(open(f, {Key::Down, Key::Down, Key::Down, Key::Accept}) && f.page() == "ControllerRoot");
        f.press({Key::Accept, Key::Down, Key::Accept});
        CHECK(focus_on(f, "OK"));
        f.press({Key::Accept});
        CHECK(!FeedbackOptions::from(settings(f).controllers.port[0], 3).vibration && FeedbackOptions::from(settings(f).controllers.port[1], 3).vibration && f.clean());
    }
    { // The steering wheel's page: active steering on, the force's strength.
        Flow f(*disc);
        CHECK(open(f, {Key::Down, Key::Down, Key::Down, Key::Right, Key::Right, Key::Right, Key::Accept}) && f.page() == "SteeringRoot");
        const bool was = settings(f).active_steering;
        const int power = get_feedback(settings(f).controllers.port[0].of(Device::Cheetah), Device::Cheetah, "POWER");
        f.press({Key::Accept, was ? Key::Down : Key::Up, Key::Accept});
        CHECK(focus_on(f, "Feedback1"));
        f.press({Key::Accept});
        f.press({Key::Up, Key::Accept});
        CHECK(focus_on(f, "OK"));
        f.press({Key::Accept});
        const int now = get_feedback(settings(f).controllers.port[0].of(Device::Cheetah), Device::Cheetah, "POWER");
        std::printf("  active steering %d -> %d, the force's strength %d -> %d\n", was, settings(f).active_steering, power, now);
        CHECK(settings(f).active_steering != was && now != power && f.clean());
    }
}
