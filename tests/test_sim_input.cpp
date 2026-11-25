// The driver's input, phase 5 (0x349FD0, rules_input.h), against the user's own program: the race start's tables (0x347A00,
// with made-up controller settings), and the phase and its routines at every call of the program's own race step for each
// controller kind and option (tools/gt4diff taps_input.cpp, GT4R_INPUT_VARY). Reads the Gran Turismo 4 disc of GT4R_DISC;
// skips without it.

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_input.h"

#include <cstdlib>
#include <string>

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(sim_input_frame_and_mail) {
    u8 b[32];
    for (u32 k = 0; k < 32; ++k) b[k] = u8(0x11 * k + 3);
    const sim::rules::PadFrame f = sim::rules::PadFrame::from_bytes(b);
    CHECK(f.flags == 3 && f.kind == 0x14 && f.shift == gt3r::s8(0x25) && f.count == 0x36);
    CHECK(f.steer == gt3r::s16(0x5847) && f.nitrous == 0x7A && f.throttle == 0x9C8B && f.third == 0x02F1);
    u8 back[32];
    f.to_bytes(back);
    bool same = true;
    for (u32 k = 0; k < 32; ++k) same = same && back[k] == b[k];
    CHECK(same);
    // The mailbox: the pit answer always, the B-spec command only for control sources 0 / 1; a computer's controller
    // leaves its frame alone.
    sim::rules::PadMail mail;
    mail.pit = {1, 2, 3, 4}, mail.command = {4, 2};
    sim::rules::PadFrame g;
    sim::rules::post_mail(g, mail, 2, true);
    CHECK(g.count == 4 && g.buttons[0] == 1 && g.buttons[3] == 4 && g.buttons[4] == 0 && mail.pit[0] == 0 && mail.command[0] == 4);
    sim::rules::post_mail(g, mail, 1, true);
    CHECK(g.count == 6 && g.buttons[0] == 0 && g.buttons[4] == 4 && g.buttons[5] == 2 && mail.command[0] == 0);
}

TEST(sim_input_tables_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    const u32 cond = ee.alloc(diff::kCondSize, 64);
    // The race start's 0x347A00 (and the B-spec pace levels, 0x3F1790) on the disc's settings and on made-up ones (each pedal
    // curve kind with percentages in and out of range, a zero ramp, the slip pull off, no active steering window), for each
    // course road byte, against ours from the program as it then stands.
    const u32 K = sim::rules::kPadSettings;
    struct Change {
        u32 offset;
        u8 value;
    };
    const std::vector<std::vector<Change>> runs = {
        {},
        {{0x65, 1}, {0x66, 40}, {0x6C, 2}, {0x6D, 130}, {0x70, 3}, {0x71, 0}},
        {{0x65, 1}, {0x66, 0}, {0x6C, 2}, {0x6D, 0xF6}, {0x70, 3}, {0x71, 250}, {0x10, 0}, {0x31, 0}, {0x14, 0}, {0x61, 3}, {0x64, 7}},
        {{0x65, 4}, {0x66, 50}, {0x6C, 1}, {0x6D, 100}, {0x70, 2}, {0x71, 100}, {0x2A, 0}, {0x2B, 30}, {0x2E, 0xE0}, {0x36, 50}},
    };
    size_t checked = 0, equal = 0;
    for (const std::vector<Change>& run : runs)
        for (u8 road : {u8(0), u8(2)}) {
            for (const Change& c : run) ee.w8(K + c.offset, c.value);
            ee.w8(cond + 0x10000 - 0x3422, road);
            ee.call(0x347A00, {diff::Arg::ptr(cond)}, 50'000'000);
            ee.call(0x3F1790, {}, 10'000'000);
            Bytes image(diff::kBssStart - 0x100000);
            ee.read_bytes(0x100000, image.data(), u32(image.size()));
            const sim::Program program = sim::Program::from_image(std::move(image), 0x100000);
            const sim::rules::PadTables theirs = sim::rules::read_pad_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
            std::string why;
            const bool same = sim::rules::same(sim::rules::pad_tables(program, road), theirs, &why);
            if (!same) std::printf("  settings run %zu, road %u: %s\n", checked / 2, road, why.c_str());
            ++checked, equal += same;
        }
    CHECK(checked == 8 && equal == checked);
}

TEST(sim_input_step_routines_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step with the phase and each of its routines run in place at every call: an ordinary box (the
    // NSX) through every variation of GT4R_INPUT_VARY (controller kinds 0, 2, 3, 4, 6, digital and analog axes, the pedal
    // styles, the wheel types and pedal curves, the prefilter, the settings' switches, the nitrous, pit answers and B-spec
    // commands, the two-wheeler, categories 4 and 6, the gearbox kinds, reversing, the race object's virtuals, made-up
    // sliding), and a stepless box (the Fit), the one-speed box (the Prius) and the box of kind 4 (the MTR GMV) through the
    // ones their boxes change.
    struct Run {
        const char* car;
        std::vector<int> modes;
    };
    for (const Run& run : {Run{"nsx_r_02", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}}, Run{"fit_w_ff_01", {0, 3, 11, 12}},
                           Run{"prius_g_02", {0, 4, 12}}, Run{"mtr_gmv_04", {0, 3, 10}}}) {
        for (int mode : run.modes) {
            if (mode) setenv("GT4R_INPUT_VARY", std::to_string(mode).c_str(), 1);
            const diff::StepResult r = diff::run_step(*disc, false, "tsukuba", run.car, 600);
            unsetenv("GT4R_INPUT_VARY");
            if (!r.ran) std::printf("  %s, variation %d: %s\n", run.car, mode, r.error.c_str());
            CHECK(r.ran && !r.taps.empty());
            size_t phase = 0;
            for (const diff::StepTap& t : r.taps) {
                if (t.name.rfind("input", 0) != 0) continue;
                if (t.equal != t.calls) std::printf("  %s, variation %d, %s: %zu of %zu calls equal\n", run.car, mode, t.name.c_str(), t.equal, t.calls);
                CHECK(t.equal == t.calls);
                if (t.name == "input") phase = t.calls;
            }
            CHECK(phase > 500);
        }
    }
}
