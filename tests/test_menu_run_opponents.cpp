// Arcade's opponents: the showroom car's evaluation (sim/showroom.h) and the opponent picker
// (menu/game_opponents.h), each against the user's own program, and the picks for the reference's
// Arcade races.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/game_opponents.h"
#include "gt4r/sim/program.h"
#include "showroom_check.h"

#include <algorithm>
#include <cstdio>
#include <set>

using script::Value;

namespace {

Disc* program_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

// Every car as sold: the program's ShowRoomCar and ours give the same evaluation, power and torque,
// bit for bit.
TEST(sim_program_showroom_equal) {
    Disc* disc = program_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::ShowroomResult r = diff::run_showroom(*disc, false);
    CHECK(r.ran && r.cars > 700 && r.built == r.cars);
    CHECK(r.evaluation_equal == r.built);
    CHECK(r.power_equal == r.built && r.torque_equal == r.built);
    if (r.evaluation_equal != r.built || r.power_equal != r.built)
        std::printf("  first: evaluation %s, power %s\n", r.first_evaluation.c_str(), r.first_power.c_str());
}

// The picker's steps: the sort, the search, the draw and the dirt step equal the program's.
TEST(sim_program_picker_steps_equal) {
    Disc* disc = program_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::PickerResult r = diff::run_picker(*disc, false);
    CHECK(r.ran && r.sorts > 0 && r.searches > 0 && r.draws > 0 && r.dirt > 0);
    CHECK(r.sorts_equal == r.sorts && r.searches_equal == r.searches && r.draws_equal == r.draws && r.dirt_equal == r.dirt);
}

// The scripts' PickUpEnemyCars on Arcade's list of the US disc, as Arcade's single race asks (offset
// (opponents' level - 2.5) x 0.01, less 0.01 for a race under 5,000 m; range 0.005; five cars). The
// reference's grids: a Ford Ka '01 at Tsukuba, level 0 (four races), five of VW Beetle 1100 '49, Honda Z
// ACT '70, N360 '67, TODAY G '85, Daihatsu Midget II '98, Honda S500 '63; a Ford GT '05 at Special Stage
// Route 5, Normal: Dodge Viper GTSR Concept '00, Shelby Cobra 427 '67, RUF CTR2 '96, Mercedes SL 65 AMG
// '04, Ford GT '02.
TEST(disc_menu_arcade_opponents) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    if (f.page() != "TopRoot") return;
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GameRoot");
    script::Vm& vm = f.rt->vm();
    const Value list = vm.lookup("ArcadeProject::EnemyData::enemy_carlist");
    CHECK(list.items() && list.items()->size() > 600);
    if (!list.items()) return;
    const Value pick = vm.lookup("main::menu::MCarData::PickUpEnemyCars");
    auto picks = [&](const char* label, float offset, std::set<std::string>& seen) {
        const std::vector<Value> car{Value::string(label), Value::integer(0)};
        const Value cg = vm.call(vm.lookup("main::menu::MCarGarage"), car);
        const std::vector<Value> args{cg, list, Value::number(offset), Value::number(0.005f), Value::integer(5), Value::integer(0)};
        bool fives = true;
        for (int k = 0; k < 40; ++k) {
            const Value got = vm.call(pick, args);
            std::set<std::string> one;
            if (got.items())
                for (const Value& p : *got.items()) {
                    const std::vector<Value>* pair = p.items();
                    if (pair && pair->size() == 2) one.insert((*pair)[0].to_string());
                }
            fives = fives && one.size() == 5 && !one.count(label);
            seen.insert(one.begin(), one.end());
        }
        return fives;
    };
    std::set<std::string> ka, gt;
    CHECK(picks("ka_01", -0.035f, ka));
    CHECK(ka == std::set<std::string>({"_1100_standard_type11_49", "z_act_70", "n360_67", "today_g_85", "midget2_d_type_98", "s500_63"}));
    CHECK(picks("ford_gt_05", -0.025f, gt));
    for (const char* c : {"viper_gtsr_00", "cobra_67", "ctr2_96", "sl65_amg_04", "ford_gt_concept_dmv_02"}) CHECK(gt.count(c));
    CHECK(gt.size() == 6);
    for (const std::string& c : gt) std::printf("  %s", c.c_str());
    std::printf("\n");
    // The reference's grids on longer courses (24-arcade-grids): the Ka at Laguna Seca, Suzuka, Trial
    // Mountain and Grand Valley drew from exactly six cars; the Ford GT '05 at New York and the Mustang
    // GT '05 at Hong Kong each fielded five of their window.
    std::set<std::string> ka_long, gt_long, mustang;
    CHECK(picks("ka_01", -0.025f, ka_long));
    CHECK(ka_long == std::set<std::string>({"today_g_85", "midget2_d_type_98", "s500_63", "ram_1500_hemi_4cab_04", "panda_super_ie_90",
                                            "karmann_ghia_type1_1500_68"}));
    CHECK(picks("ford_gt_05", -0.025f, gt_long));
    for (const char* c : {"viper_gtsr_00", "ctr_87", "cobra_67", "sl65_amg_04", "ford_gt_concept_dmv_02"}) CHECK(gt_long.count(c));
    CHECK(picks("mustang_gt_05", -0.025f, mustang));
    for (const char* c : {"celica_gt_four_rc_91", "stagea_25t_rs_fours_98", "legacy_wgn_30r_03", "legnum_vr4_v_98", "_240sx_silvia_us_96"})
        CHECK(mustang.count(c));
}

// A new car's figures (0x434E38 -> 0x440F78, 0x434FD0): run in, its showroom car's power; the never-driven
// Panda Super i.e. '90 the original shows at 44 HP.
TEST(disc_menu_new_car_power) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    script::Vm& vm = f.rt->vm();
    const Value garage = vm.attribute(vm.lookup("main::game"), "garage");
    const std::vector<Value> args{Value::string("panda_super_ie_90"), Value::integer(0)};
    const Value place = vm.call(vm.attribute(garage, "addNewCar"), args);
    CHECK(place.is_number() && place.i >= 0);
    if (!place.is_number() || place.i < 0) return;
    const menu::GarageEntry& e = f.rt->game().garage[size_t(place.i)];
    CHECK(e.power == 4454 && e.weight == 730); // 44.545 PS, the showroom car's (sim_program_showroom_equal)
    // In the player's unit (US: HP) as the car pages write it (getCurrentPs).
    const menu::GameState::Unit unit = f.rt->game().power_unit();
    const s32 shown = s32(float(e.power) / 100.0f * unit.factor + 0.5f);
    std::printf("  Panda: entry power %u, weight %u, shown %d %s\n", e.power, e.weight, shown, unit.name);
    CHECK(shown == 44);
    // The Nissan R390 GT1 Race Car '98: its car page writes the catalog's figure (getCatPs: ENGINE +0x1A,
    // 650 PS = 641 HP), the race menus' grid the showroom car's (0x364E90: 800 PS = 789 HP), as in the
    // original (reference 12-incar, r390_gt1_race_car_98_tsukuba_tt.mp4).
    const std::vector<Value> r390{Value::string("r390_race_98"), Value::integer(0)};
    const Value cg = vm.call(vm.lookup("main::menu::MCarGarage"), r390);
    const Value cat = vm.call(vm.attribute(cg, "getCatPs"), {});
    const Value at = vm.call(vm.attribute(garage, "addNewCar"), r390);
    const s32 grid = at.is_number() && at.i >= 0 ? s32(float(f.rt->game().garage[size_t(at.i)].power) / 100.0f * unit.factor) : -1;
    std::printf("  R390 GT1 Race Car: car page %s HP, showroom %d HP\n", cat.to_string().c_str(), grid);
    CHECK(cat.is_number() && cat.i == 641);
    CHECK(grid == 789);
}

// The settings pages' charts: the engine's (0x34B960) equal to the program's on every car, new and worn; the gear
// chart on all but the three cars with a stepless box of kind 1 (menu.md "MCarGarage": the converter's hold speed).
TEST(sim_program_charts_equal) {
    Disc* disc = program_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const diff::ChartsResult r = diff::run_charts(*disc, false);
    CHECK(r.ran && r.cars > 700);
    CHECK(r.engine_equal == r.cars);
    CHECK(r.gears_equal + 3 >= r.cars);
}

// MCarGarage.buildEngineCurveData / buildGearRatioData as the settings pages call them (0x148B60, 0x148940).
TEST(disc_menu_car_charts) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    script::Vm& vm = f.rt->vm();
    const std::vector<Value> car{Value::string("ka_01"), Value::integer(0)};
    const Value cg = vm.call(vm.lookup("main::menu::MCarGarage"), car);
    const Value curves = vm.call(vm.attribute(cg, "buildEngineCurveData"), {});
    const std::vector<Value>* c = curves.items();
    CHECK(c && c->size() == 6);
    if (!c || c->size() != 6) return;
    const std::vector<Value>* power = (*c)[0].items();
    CHECK(power && power->size() >= 2);
    float top = 0;
    if (power)
        for (const Value& p : *power) top = std::max(top, (*p.items())[1].as_float());
    CHECK(top > 0.999f && top <= 1.0f);                     // over its own peak
    CHECK((*c)[2].as_float() > 59 && (*c)[2].as_float() < 62); // the Ka's 60 PS
    const std::vector<Value> at{Value::integer(1)};
    const Value gears = vm.call(vm.attribute(cg, "buildGearRatioData"), at);
    const std::vector<Value>* g = gears.items();
    CHECK(g && g->size() == 10); // five gears, a pair of points each
    if (g && g->size() >= 2) CHECK((*g)[1].items() && (*(*g)[1].items())[0].as_float() > (*(*g)[0].items())[0].as_float());
}
