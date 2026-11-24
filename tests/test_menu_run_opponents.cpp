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
}
