// A race's sound: the panner, the levels by the throttle, the course's ambience on made-up data,
// then the program's tables and rendered cars and races on the discs of GT4R_DISC. Every check
// that needs sound renders it to memory and measures it; nothing opens an audio device.

#include "test_framework.h"

#include "disc/core_image.h"
#include "gt4r/audio/analysis.h"
#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/course_sound.h"
#include "gt4r/audio/race_playlist.h"
#include "gt4r/audio/race_sound.h"
#include "gt4r/audio/race_tables.h"
#include "gt4r/audio/render.h"
#include "gt4r/audio/sim_sound_state.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>

using namespace gt4r;

namespace {

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// The energy of each side over a stretch of frames.
void side_energy(const std::vector<s16>& pcm, size_t from, size_t frames, double& left, double& right) {
    left = right = 0;
    for (size_t f = from; f < from + frames && 2 * f + 1 < pcm.size(); ++f) {
        left += double(pcm[2 * f]) * pcm[2 * f];
        right += double(pcm[2 * f + 1]) * pcm[2 * f + 1];
    }
}
double rms(const std::vector<s16>& pcm, size_t from, size_t frames) {
    double l, r;
    side_energy(pcm, from, frames, l, r);
    return std::sqrt((l + r) / double(2 * frames));
}

// The Gran Turismo 4 disc of GT4R_DISC whose program's tables are mapped (sessions); null without one.
Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

std::shared_ptr<const course::Runway> open_road(Disc& disc, const std::string& name, Bytes& data, course::CourseFile& file) {
    const gt3r::Volume::Entry* e = disc.volume().find("crs/" + name);
    if (!e) return nullptr;
    data = disc.volume().read(*e);
    file = course::CourseFile::parse(data);
    return std::make_shared<const course::Runway>(course::Runway::parse(file.section(data, course::kSlotRunway)));
}

} // namespace

// The races' play list (0x438170): the enabled rows in their order, round after round; a row
// played this round passes its turn on; a shuffled order holds every enabled row once.
TEST(sound_race_playlist) {
    audio::RacePlaylist list = audio::RacePlaylist::in_order(8, {5, 2, 7});
    std::vector<int> got;
    for (int i = 0; i < 7; ++i) got.push_back(list.next());
    CHECK((got == std::vector<int>{5, 2, 7, 5, 2, 7, 5}));
    CHECK(list.position() == 1);
    // A row already played this round: the next place's row instead.
    list.reset_played();
    list.set_position(0);
    list.rows()[2].played = true;
    CHECK(list.next() == 5 && list.next() == 7);
    // Nothing enabled: nothing to play.
    audio::RacePlaylist empty(4);
    CHECK(empty.next() == -1);
    // The program's random numbers: 2^31 is 0.5, 2^30 a quarter, and the state steps by 17 s + 17.
    CHECK(audio::RacePlaylist::random_float(0x80000000u) == 0.5f && audio::RacePlaylist::random_float(0x40000000u) == 0.25f);
    u32 state = 1;
    const u32 r = audio::RacePlaylist::random_step(state);
    CHECK(state == 34 && r == (34u ^ (34u << 16)));
    for (int i = 0; i < 1000; ++i) {
        const float f = audio::RacePlaylist::random_float(audio::RacePlaylist::random_step(state));
        CHECK(f >= 0.0f && f < 1.0f);
    }
    // Shuffled: each of the enabled rows has one place of 0..n-1, and the same seed the same order.
    audio::RacePlaylist a = audio::RacePlaylist::in_order(20, {1, 3, 4, 8, 9, 10, 15, 19}), b = a;
    a.set_shuffle(true, 0x1234), b.set_shuffle(true, 0x1234);
    a.build(), b.build();
    std::vector<int> places;
    for (size_t i = 0; i < a.rows().size(); ++i) {
        CHECK(a.rows()[i].order == b.rows()[i].order);
        if (a.rows()[i].enabled) places.push_back(a.rows()[i].order);
    }
    std::sort(places.begin(), places.end());
    CHECK((places == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    CHECK(a.seed() == b.seed() && a.seed() != 0x1234);
}

// The game record's options as the race takes them (the option page's names for the output).
TEST(sound_race_options_from_record) {
    std::map<std::string, std::variant<s32, float, std::string>> o;
    audio::RaceSoundOptions r = audio::race_sound_options(o);
    CHECK(r.effects_on && r.effects == 115 && r.music == 115 && r.replay_music == 115 && r.mode == audio::OutputMode::Stereo);
    o["race_se"] = 0, o["race_bgm_volume_raw"] = 64, o["replay_se_volume_raw"] = 100, o["sound_type"] = std::string("PROLOGIC2");
    r = audio::race_sound_options(o);
    CHECK(!r.effects_on && r.music == 64 && r.replay_effects == 100 && r.mode == audio::OutputMode::Surround);
    o["sound_type"] = std::string("MONOPHONIC");
    CHECK(audio::race_sound_options(o).mode == audio::OutputMode::Mono);
}

// The seam's contacts as the program records them (0x350328), and a landing.
TEST(sound_race_contacts_from_seam) {
    CHECK(near(audio::contact_strength(0, 25000), 0.5, 1e-6) && audio::contact_strength(0, 5000) == 0); // a wall: N / 50,000; under 0.2 none
    CHECK(near(audio::contact_strength(1, 0.3f), 0.6, 1e-6) && audio::contact_strength(1, 2) == 1);   // a car: twice the speed, at most 1
    CHECK(near(audio::contact_strength(2, 1), 0.2, 1e-6));                                             // the third kind: at least 0.2
    race::CarState car;
    car.contact = 30000, car.contact_kind = 1;
    audio::CarSoundState s = audio::sound_state(car);
    CHECK(s.contact_kind == 0 && near(s.contact, 0.6, 1e-6));
    car.contact = 0.25f, car.contact_kind = 2;
    s = audio::sound_state(car);
    CHECK(s.contact_kind == 1 && near(s.contact, 0.5, 1e-6));
    car.contact_kind = 0, car.landed = true, car.velocity = {0, -4, 10};
    s = audio::sound_state(car);
    CHECK(s.contact == 0 && near(s.wheels[0].landing, 0.8, 1e-6));
}

// The panner (0x391AF0): full level inside the reach, 1 / (1 + slope (d - reach)) past it, a
// quarter-power pan by the sound's side, the quarter metre at the camera.
TEST(sound_race_place_sound) {
    const audio::RaceSoundTables::Mic mic{0.5f, 9.0f};
    audio::Placement p = audio::place_sound(mic, 0, 5);
    CHECK(near(p.gain, 1, 1e-6) && near(p.left, p.right, 1e-6) && near(p.left, std::sqrt(0.5), 1e-6));
    p = audio::place_sound(mic, 0, 29);
    CHECK(near(p.gain, 1.0 / 11.0, 1e-6));
    p = audio::place_sound(mic, 10, 0); // to the right: the right side only
    CHECK(near(p.gain, 1.0 / 1.5, 1e-6) && near(p.left, 0, 1e-6) && near(p.right, 1, 1e-6));
    p = audio::place_sound(mic, -3, 3); // half way to the left
    CHECK(p.left > p.right && near(p.left * p.left + p.right * p.right, 1, 1e-5));
    p = audio::place_sound(mic, 0, 0); // at the camera: a quarter metre ahead
    CHECK(near(p.z, 1, 1e-6) && near(p.gain, 1, 1e-6));
    p = audio::place_sound(audio::RaceSoundTables::Mic{0, 9}, 0, 64); // a microphone switched off: 32 / d
    CHECK(near(p.gain, 0.5, 1e-6));
}

// The two banks' levels (0x350080): quietest at the throttle that just holds the revs, louder
// either way, halved with the clutch open; each step half way to the target.
TEST(sound_race_levels_by_throttle) {
    gt3r::Vfs vfs(nullptr);
    gt3r::VoiceMixer mixer;
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables.params.engine_level = mix->tables.params.exhaust_level = 1.0f;
    audio::CarSound car(vfs, mixer);
    car.set_mix(mix);
    const auto settle = [&](float throttle, int clutch) {
        audio::CarSoundState s;
        s.rpm = 4000, s.throttle = throttle, s.clutch = clutch;
        s.torque_full = 3, s.torque_friction = 1; // quietest at a quarter
        for (int i = 0; i < 20; ++i) car.update(s, audio::SoundListener{}, true);
        return car.report();
    };
    const auto quiet = settle(0.25f, 1), open = settle(1.0f, 1), shut = settle(0.0f, 1), declutched = settle(1.0f, 0);
    std::printf("  engine level: shut %d, quietest %d, open %d, open declutched %d; exhaust %d %d %d\n", shut.engine_level, quiet.engine_level,
                open.engine_level, declutched.engine_level, shut.exhaust_level, quiet.exhaust_level, open.exhaust_level);
    CHECK(quiet.engine_level < shut.engine_level && shut.engine_level < open.engine_level);
    CHECK(near(open.engine_level, 126, 1) && near(quiet.engine_level, 31, 1) && near(shut.engine_level, 62, 1));
    CHECK(near(declutched.engine_level, 63, 1));
    // The exhaust: 0.4 off the load, 1 with it all: (1 - (1 - load)^16) x 0.6 + 0.4.
    CHECK(near(shut.exhaust_level, 50, 1) && near(open.exhaust_level, 126, 1));
    // Half way each step.
    audio::CarSoundState s;
    s.rpm = 4000, s.throttle = 1, s.torque_full = 3, s.torque_friction = 1;
    settle(0.25f, 1);
    car.update(s, audio::SoundListener{}, true);
    CHECK(near(car.report().engine_level, (31 + 127) / 2, 1));
}

// The views: what view each camera gives its car and the others.
TEST(sound_race_views) {
    using K = audio::SoundListener::Kind;
    CHECK(audio::sound_view(K::InCar, true) == audio::SoundView::InCarTarget);
    CHECK(audio::sound_view(K::InCar, false) == audio::SoundView::InCarOther);
    CHECK(audio::sound_view(K::Chase, true) == audio::SoundView::ChaseTarget);
    CHECK(audio::sound_view(K::Chase, false) == audio::SoundView::ChaseOther);
    CHECK(audio::sound_view(K::Other, true) == audio::SoundView::Other);
}

// The course's ambience on made-up emitters.
TEST(sound_race_course_ambience) {
    gt3r::sound::CrssData data;
    gt3r::sound::CrssEmitter air;
    air.points.push_back({0, 0, 500, 0.6f});
    data.channels[0].push_back(air);
    gt3r::sound::CrssEmitter crowd; // gated between 100 m and 200 m along a straight course
    crowd.gate_a = {0, 0, 100}, crowd.gate_b = {0, 0, 200};
    crowd.points.push_back({64, 0, 0, 1.0f});
    data.channels[2].push_back(crowd);
    gt3r::sound::CrssEmitter sea;
    sea.points.push_back({-10, 0, 0, 0.5f});
    data.channels[4].push_back(sea);
    audio::CourseSound c;
    c.load(data, [](const race::Vec3& p, float* d) { return *d = p.z, true; }, 1000.0f);
    std::vector<audio::CourseSound::Listener> at(1);
    // The air: not placed, both sides at the first point's weight, at once.
    c.update(at, {50.0f});
    CHECK(near(c.channel(audio::CourseSound::Air).left, 0.6, 1e-6) && near(c.channel(audio::CourseSound::Air).right, 0.6, 1e-6));
    // The crowd: no car between the gates yet; then one: rises by 1/120 a step.
    CHECK(c.emitter_gain(audio::CourseSound::Crowds, 0) == 0);
    for (int i = 0; i < 60; ++i) c.update(at, {150.0f, 20.0f});
    CHECK(near(c.emitter_gain(audio::CourseSound::Crowds, 0), 0.5, 1e-4));
    // At 64 m to the right: 32 / 64 of its weight, on the right side only.
    const audio::CourseSound::Out& o = c.channel(audio::CourseSound::Crowds);
    CHECK(near(o.right, 0.5 * 0.5, 1e-4) && near(o.left, 0, 1e-4));
    // Falls by 1/180 a step; the second car alone weighs 0.8.
    for (int i = 0; i < 18; ++i) c.update(at, {20.0f, 20.0f});
    CHECK(near(c.emitter_gain(audio::CourseSound::Crowds, 0), 0.4, 1e-4));
    for (int i = 0; i < 200; ++i) c.update(at, {20.0f, 150.0f});
    CHECK(near(c.emitter_gain(audio::CourseSound::Crowds, 0), 0.8, 1e-4));
    // The sea: placed, to the left.
    CHECK(c.channel(audio::CourseSound::Sea).left > 0.49f && c.channel(audio::CourseSound::Sea).right < 0.01f);
    // The tunnels and the reverb: 1/36 a step toward the road's target.
    CHECK(near(c.reverb(), 0.125, 1e-6));
    at[0].road_flags = 0x08;
    c.update(at, {});
    CHECK(c.tunnel1() == 1 && c.tunnel2() == 0 && near(c.reverb_target(), 0.5, 1e-6) && near(c.reverb(), 0.125 + 1.0 / 36, 1e-6));
    for (int i = 0; i < 20; ++i) c.update(at, {});
    CHECK(near(c.reverb(), 0.5, 1e-6));
    at[0].road_flags = 0x10;
    c.update(at, {});
    CHECK(c.tunnel1() == 0 && c.tunnel2() == 1 && near(c.reverb_target(), 0.25, 1e-6));
    at[0].road_flags = 0x18;
    c.update(at, {});
    CHECK(c.tunnel1() == 0 && c.tunnel2() == 0 && near(c.reverb_target(), 0.25, 1e-6));
    // Two cameras: both sides the larger, the reverb 0.125.
    at.push_back({});
    at[1].road_flags = 0x08;
    c.update(at, {});
    CHECK(near(c.reverb_target(), 0.125, 1e-6) && near(c.channel(audio::CourseSound::Sea).left, c.channel(audio::CourseSound::Sea).right, 1e-6));
}

// The program's tables, found in each disc's program.
TEST(sound_race_tables_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        const Bytes core = disc->read_program();
        const audio::RaceSoundTables t = audio::RaceSoundTables::from_core(core);
        std::printf("  %s: gains %s, microphones %s, numbers %s\n", disc->info().game == Game::GT4 ? "Gran Turismo 4" : "Prologue",
                    t.found_gains ? "found" : "not found", t.found_mics ? "found" : "not found", t.found_params ? "found" : "not found");
        if (disc->info().game != Game::GT4) continue;
        CHECK(t.found());
        using V = audio::SoundView;
        using C = audio::SoundChannel;
        std::printf("  engine / exhaust by view:");
        for (int v = 0; v < 5; ++v) std::printf(" %.2f/%.2f", double(t.view_gain(V(v), C::Engine)), double(t.view_gain(V(v), C::Exhaust)));
        std::printf("\n  wind %.2f..%.2f m/s -> %.0f..%.0f rpm; levels %.2f %.2f, spool %.2f, blow-off %.2f\n", double(t.params.wind_speed_lo),
                    double(t.params.wind_speed_hi), double(t.params.wind_rpm_lo), double(t.params.wind_rpm_hi), double(t.params.engine_level),
                    double(t.params.exhaust_level), double(t.params.turbine_level), double(t.params.blowoff_scale));
        CHECK(near(t.view_gain(V::InCarTarget, C::Engine), 6.5, 1e-4) && near(t.view_gain(V::ChaseTarget, C::Exhaust), 6.5, 1e-4));
        CHECK(near(t.view_gain(V::Other, C::Wind), 60.0, 1e-3) && t.view_gain(V::InCarOther, C::InCar) == 0);
        for (int m = 0; m < 3; ++m) CHECK(near(t.mic(audio::OutputMode(m), 0).slope, 0.5, 1e-6) && near(t.mic(audio::OutputMode(m), 1).reach, 9, 1e-6));
        CHECK(near(t.mic(audio::OutputMode::Stereo, 1).slope, 0.1, 1e-6) && near(t.mic(audio::OutputMode::Surround, 2).slope, 0.16, 1e-6));
        CHECK(near(t.params.engine_level, 0.1, 1e-6) && near(t.params.exhaust_level, 0.1, 1e-6));
        CHECK(near(t.params.turbine_level, 0.2, 1e-6) && near(t.params.blowoff_scale, 2.5, 1e-6));
    }
}

// A car rendered as the race mixes it: level against distance, the pan, the Doppler factor.
TEST(sound_race_car_mix_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue;
        gt3r::Vfs vfs(&disc->volume());
        auto mix = std::make_shared<audio::RaceMix>();
        mix->tables = audio::RaceSoundTables::from_core(disc->read_program());
        const auto render = [&](race::Vec3 at, race::Vec3 velocity, audio::CarSound::Report* report) {
            audio::OfflineSound s(false);
            audio::CarSound car(vfs, s.mixer());
            car.set_mix(mix);
            CHECK(car.load("_117_coupe_68"));
            s.advance(120, [&](int) {
                audio::CarSoundState in;
                in.rpm = 4000, in.throttle = 1, in.torque_full = 3, in.torque_friction = 1;
                in.position = at, in.velocity = velocity;
                car.update(in, audio::SoundListener{}, false);
            });
            if (report) *report = car.report();
            return s.pcm;
        };
        audio::CarSound::Report near5, far29, right10, coming;
        const std::vector<s16> a = render({0, 0, 5}, {}, &near5), b = render({0, 0, 29}, {}, &far29);
        const std::vector<s16> c = render({10, 0, 0}, {}, &right10), d = render({0, 0, 5}, {0, 0, -30}, &coming);
        const size_t from = 60 * 800, frames = 48000;
        const double ra = rms(a, from, frames), rb = rms(b, from, frames);
        std::printf("  at 5 m: engine %.3f exhaust %.3f, rms %.0f; at 29 m: engine %.3f exhaust %.3f, rms %.0f (%.2f times quieter)\n",
                    double(near5.engine_gain), double(near5.exhaust_gain), ra, double(far29.engine_gain), double(far29.exhaust_gain), rb, ra / rb);
        // The engine's microphone falls to 1/11 at 29 m, the exhaust's to 1/3 (stereo).
        CHECK(near(far29.engine_gain / near5.engine_gain, 1.0 / 11, 1e-3) && near(far29.exhaust_gain / near5.exhaust_gain, 1.0 / 3, 1e-3));
        CHECK(ra / rb > 2.5 && ra / rb < 11.5);
        double l, r;
        side_energy(c, from, frames, l, r);
        std::printf("  10 m to the right: left / right energy %.4f\n", l / r);
        CHECK(r > 0 && l / r < 0.05);
        // Coming at 30 m/s: (c') / (c' - 0.5), c' = 5.5241666 m a step.
        const double expect = 5.5241666 / (5.5241666 - 0.5);
        const double f0 = audio::dominant_frequency(a, from, 24000, 20, 400), f1 = audio::dominant_frequency(d, from, 24000, 20, 400);
        std::printf("  Doppler: factor %.4f (expected %.4f), %.1f Hz -> %.1f Hz (%.4f)\n", double(coming.doppler), expect, f0, f1, f1 / f0);
        CHECK(near(coming.doppler, expect, 1e-4));
        CHECK(f0 > 0 && near(f1 / f0, expect, 0.03));
    }
}

// The Ford Ka '01 flat out in the original's Tsukuba race (docs/formats/sound.md "Checked against
// the original"): its engine's strongest note at 5,650 rpm from the chase view is at 181 Hz and at
// 5,076 rpm from the nose view at 164 Hz, and the players' volumes the program held at those two
// moments (engine / exhaust levels 11 / 12, the new game's options, stereo) were 2146 / 5073 and
// 4651 / 5464 of 16383 on each side.
TEST(sound_race_engine_against_original) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && d->volume().find("sound/engine/37701")) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables = audio::RaceSoundTables::from_core(disc->read_program());
    mix->master = library->levels().level(int(audio::OutputMode::Stereo), sound::VolumeTable::Race) * 115.0f / 128.0f;
    const auto render = [&](float rpm, audio::SoundListener::Kind kind, audio::CarSound::Report& report) {
        audio::OfflineSound s(false);
        audio::CarSound car(vfs, s.mixer());
        car.set_mix(mix);
        CHECK(car.load("ka_01"));
        audio::SoundListener at;
        at.kind = kind;
        s.advance(150, [&](int) {
            audio::CarSoundState in;
            in.rpm = rpm, in.throttle = 1, in.torque_full = 3, in.torque_friction = 1;
            in.position = {0, 0, kind == audio::SoundListener::Kind::Chase ? 5.0f : 0.0f};
            car.update(in, at, true);
        });
        report = car.report();
        return audio::slice(audio::signal_from(s.pcm), 1.0, 2.5);
    };
    audio::CarSound::Report chase, nose;
    const audio::LogSpectrum a = audio::log_spectrum(render(5650, audio::SoundListener::Kind::Chase, chase), 48, 60, 2500);
    const audio::LogSpectrum b = audio::log_spectrum(render(5076, audio::SoundListener::Kind::InCar, nose), 48, 60, 2500);
    const double fa = a.peak(120, 260), fb = b.peak(120, 260);
    std::printf("  strongest note: %.1f Hz at 5650 rpm (chase), %.1f Hz at 5076 rpm (in the car)\n", fa, fb);
    CHECK(near(fa, 181, 181 * 0.02) && near(fb, 164, 164 * 0.02));
    // The players' volumes: gain x the side's share (centred: 0.7071) x 16383. The original's
    // exhaust level was 12 where a level rising from 0 settles at 11 (each step half way, rounded
    // down): the exhaust is compared at its level.
    const auto side = [](float gain) { return gain * 0.70710678f * 16383.0f; };
    std::printf("  chase: engine %.0f exhaust %.0f; in the car: engine %.0f exhaust %.0f (levels %d / %d)\n", double(side(chase.engine_gain)),
                double(side(chase.exhaust_gain)), double(side(nose.engine_gain)), double(side(nose.exhaust_gain)), nose.engine_level, nose.exhaust_level);
    CHECK(chase.engine_level == 11 && nose.engine_level == 11);
    CHECK(near(side(chase.engine_gain), 2146, 2146 * 0.005) && near(side(nose.engine_gain), 4651, 4651 * 0.005));
    CHECK(near(side(chase.exhaust_gain) * 12 / chase.exhaust_level, 5073, 5073 * 0.005));
    CHECK(near(side(nose.exhaust_gain) * 12 / nose.exhaust_level, 5464, 5464 * 0.005));
}

// The surround cue as the original plays it (its stereo output, the nose view: the exhaust behind
// the camera with its left side inverted): with the mixer's option on, the sides of an in-car
// rendering correlate far less than without it, and the chase view's (the car ahead) not at all less.
TEST(sound_race_surround_cue_on_disc) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && d->volume().find("sound/engine/37701")) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables = audio::RaceSoundTables::from_core(disc->read_program());
    const auto render = [&](bool cue, audio::SoundListener::Kind kind) {
        audio::OfflineSound s(false);
        s.mixer().set_surround_cue(cue);
        audio::CarSound car(vfs, s.mixer(), &s.driver(), library);
        car.set_mix(mix);
        CHECK(car.load("ka_01"));
        audio::SoundListener at;
        at.kind = kind;
        s.advance(120, [&](int) {
            audio::CarSoundState in;
            in.rpm = 5000, in.throttle = 1, in.torque_full = 3, in.torque_friction = 1;
            in.position = {0, 0, kind == audio::SoundListener::Kind::Chase ? 5.0f : 0.0f};
            car.update(in, at, true);
        });
        return audio::loudness(audio::slice(audio::signal_from(s.pcm), 0.5, 2)).side_correlation;
    };
    const double off = render(false, audio::SoundListener::Kind::InCar), on = render(true, audio::SoundListener::Kind::InCar);
    const double chase_off = render(false, audio::SoundListener::Kind::Chase), chase_on = render(true, audio::SoundListener::Kind::Chase);
    std::printf("  sides correlate: in the car %.2f, with the cue %.2f; chase %.2f, with the cue %.2f\n", off, on, chase_off, chase_on);
    CHECK(off > 0.9 && on < 0.5 && near(chase_on, chase_off, 0.02));
}

// The gearbox's whine: a car whose gearbox is not the road one (its GEAR row's byte +0x22, the
// gearbox's grade: 1 close, 2 super close, 3 racing) whines with the clutch in and torque through it.
TEST(sound_race_gear_whine_on_disc) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && d->volume().find("sound/engine/37701")) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    CHECK(!audio::car_sound_files(vfs, "ka_01").whine && audio::car_sound_files(vfs, "impreza_wrc_03").whine);
    audio::OfflineSound s(false);
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    auto mix = std::make_shared<audio::RaceMix>();
    mix->tables = audio::RaceSoundTables::from_core(disc->read_program());
    audio::CarSound car(vfs, s.mixer(), &s.driver(), library);
    car.set_mix(mix);
    CHECK(car.load("impreza_wrc_03"));
    audio::CarSoundState in;
    in.rpm = 6000, in.throttle = 1, in.drive_torque = 300, in.gear_ratio = 1.5f, in.low_ratio = 3, in.shaft_speed = 100;
    in.position = {0, 0, 5};
    audio::SoundListener chase;
    car.update(in, chase, true);
    const float lo = car.report().whine;
    in.gear_ratio = 3;
    car.update(in, chase, true);
    std::printf("  whine: %.3f in a high gear, %.3f in the first\n", double(lo), double(car.report().whine));
    CHECK(lo > 0 && car.report().whine > lo);
    in.clutch = 0;
    car.update(in, chase, true);
    CHECK(car.report().whine == 0);
}

// A race: a session on Tsukuba with its course sound, the count's beeps at 3, 2 and 1 seconds,
// the start, then the cars driving.
TEST(sound_race_session_on_disc) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::string labels[] = {"nsx_r_02", "civic_r_01"};
    const std::unique_ptr<sim::Session> session = sim::Session::open(*disc, "tsukuba", labels);
    Bytes data;
    course::CourseFile file;
    const auto road = open_road(*disc, "tsukuba", data, file);
    CHECK(road != nullptr);
    gt3r::Vfs vfs(&disc->volume());
    audio::OfflineSound s(false);
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    std::vector<std::string> streams;
    audio::StreamPlayer music;
    music.play = [&](const std::string& path, bool, float level) { return streams.push_back(path), level > 0; };
    audio::RaceSound sound(vfs, s.mixer(), s.driver(), library, audio::RaceSoundTables::from_core(disc->read_program()), music);
    for (const std::string& l : labels) CHECK(sound.add_car(l));
    sound.set_course(file.section(data, course::kSlotSound), road);
    // (each car's state from the record: plausible numbers)
    const audio::CarSoundState st = audio::sound_state(*session, 0);
    std::printf("  car 0: engine place %d, drag %.3f, tire widths %.3f / %.3f m, surface %d\n", st.engine_place, double(st.drag),
                double(st.wheels[0].width), double(st.wheels[3].width), st.wheels[0].surface);
    CHECK(st.drag > 0.1f && st.drag < 1.0f && st.wheels[0].width > 0.1f && st.wheels[0].width < 0.5f && st.engine_place == 2); // (the NSX's engine is in the middle)
    std::vector<race::CarInput> in(2);
    for (race::CarInput& i : in) i.brake = 1;
    const auto camera = [&] { return audio::listener_at(session->cars()[0], audio::SoundListener::Kind::Chase); };
    std::vector<size_t> beep_frames;
    int beeps = 0;
    for (int left = 180; left >= 0; --left) {
        session->hold_once(in);
        sound.count(left);
        if (sound.report().beeps != beeps) beeps = sound.report().beeps, beep_frames.push_back(s.pcm.size() / 2);
        sound.update(*session, camera(), 0);
        s.advance(1);
    }
    CHECK(sound.report().beeps == 3 && sound.report().starts == 1);
    CHECK(streams.size() == 1 && streams[0].find("bgm/") == 0);
    // The beeps a second apart in the rendering: the sound starts within a step of each.
    const size_t step = audio::SoundDriver::kSamplesPerUpdate;
    for (size_t i = 0; i < beep_frames.size(); ++i) {
        size_t onset = 0;
        for (size_t f = beep_frames[i]; f < beep_frames[i] + 4 * step && 2 * f + 1 < s.pcm.size(); ++f)
            if (std::abs(s.pcm[2 * f]) > 200) {
                onset = f;
                break;
            }
        std::printf("  beep %zu: asked at %.3f s, heard from %.3f s\n", i + 1, double(beep_frames[i]) / 48000, double(onset) / 48000);
        if (i > 0) CHECK(near(double(beep_frames[i] - beep_frames[i - 1]) / 48000, 1.0, 1e-6));
    }
    // Driving: the master level has faded in, the engines and the course sound.
    for (race::CarInput& i : in) i = race::CarInput{}, i.throttle = 1;
    const size_t from = s.pcm.size() / 2;
    for (int f = 0; f < 240; ++f) {
        session->step_once(in);
        sound.update(*session, camera(), 0);
        s.advance(1);
    }
    const double level = rms(s.pcm, from + 48000, 2 * 48000);
    std::printf("  master %.3f; driving rms %.0f, %d voices at most; ambience", double(sound.master()), level, s.max_voices);
    for (float a : sound.report().ambience) std::printf(" %.3f", double(a));
    std::printf("; reverb %.3f; emitters %zu %zu %zu %zu %zu\n", double(sound.report().reverb), sound.course().emitters(0), sound.course().emitters(1),
                sound.course().emitters(2), sound.course().emitters(3), sound.course().emitters(4));
    // The master: the option (115 of 128) x the mix table's race level for stereo, faded in.
    const float table = library->levels().level(int(audio::OutputMode::Stereo), sound::VolumeTable::Race);
    std::printf("  the mix table's race level (stereo) %.3f\n", double(table));
    CHECK(near(sound.master(), 115.0 / 128.0 * table, 1e-5) && level > 300);
    sound.pause(true);
    s.advance(60);
    CHECK(s.mixer().active_voices() == 0);
}

// A rolling start (the original's arcade races): the racing music as the race begins to run, the
// start's sound when the player's car reaches the line; the play list's pieces in turn; the replay
// plays the race's first piece again at the replay's levels.
TEST(sound_race_rolling_start_and_replay) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && d->volume().find("bgm/us/ads.inf")) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Vfs vfs(&disc->volume());
    audio::OfflineSound s(false);
    auto library = std::make_shared<audio::SoundLibrary>(vfs);
    struct Played {
        std::string file;
        float level;
    };
    std::vector<Played> streams;
    bool playing = true;
    audio::StreamPlayer music;
    music.play = [&](const std::string& path, bool, float level) { return streams.push_back({path, level}), playing = true; };
    music.stop = [&] { playing = false; };
    music.playing = [&] { return playing; };
    audio::RaceSound sound(vfs, s.mixer(), s.driver(), library, audio::RaceSoundTables::from_core(disc->read_program()), music);
    audio::RaceSoundOptions o;
    o.music = 64, o.replay_music = 128;
    sound.set_options(o);
    const int racing = sound::stream_group("racing");
    const size_t pieces = library->streams().groups[size_t(racing)].size();
    sound.set_playlist(std::make_shared<audio::RacePlaylist>(audio::RacePlaylist::in_order(pieces, {20, 16})));
    sound.rolling();
    CHECK(streams.size() == 1 && streams[0].file == library->stream_file(racing, 20));
    CHECK(sound.report().starts == 0);
    const float table = library->levels().level(int(audio::OutputMode::Stereo), sound::VolumeTable::Stream);
    CHECK(near(streams[0].level, table * 0.5f, 1e-6));
    sound.start_signal();
    CHECK(sound.report().starts == 1 && streams.size() == 1);
    // The piece ends: the play list's next.
    playing = false;
    sound.update(std::span<const audio::CarSoundState>{}, audio::SoundListener{}, -1);
    CHECK(streams.size() == 2 && streams[1].file == library->stream_file(racing, 16));
    // The replay: the first piece again, at the replay's level.
    sound.replay();
    CHECK(streams.size() == 3 && streams[2].file == library->stream_file(racing, 20) && near(streams[2].level, table, 1e-6));
}

