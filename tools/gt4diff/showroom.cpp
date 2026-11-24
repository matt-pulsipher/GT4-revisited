// gt4r_diff: the showroom car (showroom_check.h). Routines are named by address; nothing of the program is kept.

#include "showroom_check.h"

#include "orig.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/showroom.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/game_opponents.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace gt4r::diff {

namespace {
constexpr u32 kShowroomBuild = 0x364868; // (ShowRoomCar, car parameter, condition or 0)
constexpr u32 kShowroomSize = 0x1800;    // the car ShowRoomCar's constructor (0x364760) allocates
constexpr u32 kEvaluation = 0x15E0, kPower = 0x1700, kTorque = 0x1704;
constexpr u32 kQsort = 0x5A4BF8, kOrder = 0x130660, kSearch = 0x130618, kDraw = 0x130690, kDirt = 0x1307E8;
constexpr u32 kGenerator = 0x874D50; // the program's generator's state
constexpr u32 kArcadeTables = 0x623800; // the tables the program opens when first asked (0x447A78): ARCADEINFO_NORMAL, _TUNED
constexpr u32 kTableCtor = 0x448F18, kTableOpen = 0x4490F8;
constexpr u32 kEntry = 0x44;
constexpr u32 kConditionFactor = 0x350868, kConditionLimits = 0x620320; // (limits, condition block)          // a candidate: its label (0x40 bytes), its evaluation

double relative(float a, float b) {
    const double d = std::fabs(double(a) - double(b)), m = std::fmax(std::fabs(double(a)), std::fabs(double(b)));
    return m > 0 ? d / m : 0;
}
} // namespace

ShowroomResult run_showroom(Disc& disc, bool print, size_t limit) {
    ShowroomResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    const sim::Showroom ours(sdb, program);
    const u32 eq_at = ee.alloc(0x200), object = ee.alloc(0x10), car = ee.alloc(kShowroomSize);
    size_t shown = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        if (limit && result.cars >= limit) break;
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) continue;
        ++result.cars;
        for (u32 o = 0; o < 0x200; o += 4) ee.w32(eq_at + o, 0);
        for (u32 o = 0; o < kShowroomSize; o += 4) ee.w32(car + o, 0);
        ee.w32(object, car), ee.w8(object + 4, 0);
        try {
            ee.call(kSetCar, {Arg::ptr(eq_at), Arg::integer(db::part_id(0, c.id))}, 50'000'000);
            ee.call(kShowroomBuild, {Arg::ptr(object), Arg::ptr(eq_at), Arg::integer(0)}, 400'000'000);
        } catch (const std::exception& e) {
            if (print && shown++ < 5) std::printf("  %s: the program's build stopped: %s\n", c.label.c_str(), e.what());
            continue;
        }
        const std::optional<sim::ShowroomFigures> f = ours.figures(eq);
        if (!f) continue;
        ++result.built;
        const u32 ev = ee.r32(car + kEvaluation), pw = ee.r32(car + kPower), tq = ee.r32(car + kTorque);
        const bool ev_ok = ev == f->evaluation.bits, pw_ok = pw == f->power.bits, tq_ok = tq == f->torque.bits;
        result.evaluation_equal += ev_ok, result.power_equal += pw_ok, result.torque_equal += tq_ok;
        const float ev_t = ee.rf(car + kEvaluation), pw_t = ee.rf(car + kPower);
        const double rev = relative(ev_t, gt3r::ee::to_host(f->evaluation.bits)), rpw = relative(pw_t, gt3r::ee::to_host(f->power.bits));
        if (rev > result.worst_evaluation) result.worst_evaluation = rev;
        if (rpw > result.worst_power) result.worst_power = rpw;
        if (!ev_ok && result.first_evaluation.empty()) result.first_evaluation = c.label;
        if (!pw_ok && result.first_power.empty()) result.first_power = c.label;
        if (print && ((!ev_ok || !pw_ok || !tq_ok) || std::getenv("GT4R_SHOWROOM_LIST")) && shown++ < (std::getenv("GT4R_SHOWROOM_LIST") ? 100000u : 12u))
            std::printf("  %-28s evaluation %.7g / ours %.7g, power %.7g / %.7g, torque %.7g / %.7g\n", c.label.c_str(), double(ev_t),
                        double(gt3r::ee::to_host(f->evaluation.bits)), double(pw_t), double(gt3r::ee::to_host(f->power.bits)),
                        double(ee.rf(car + kTorque)), double(gt3r::ee::to_host(f->torque.bits)));
    }
    // The engine condition's factor on made-up condition blocks.
    {
        const u32 cond = ee.alloc(0x20);
        const float engines[] = {-400000, -300000, -299999, -150000, -1, 0, 500000, 1000000, 1000001, 5000000, 10999999, 11000000, 20000000};
        const float oils[] = {-200000, -100001, -100000, -50000, 0, 4000000, 5000000, 5500000, 6000000, 9000000};
        for (float e : engines)
            for (float o : oils)
                for (int nw = 0; nw < 2; ++nw) {
                    for (u32 k = 0; k < 0x20; k += 4) ee.w32(cond + k, 0);
                    ee.wf(cond + 4, e), ee.wf(cond + 8, o), ee.w8(cond + 0x14, u8(nw));
                    const auto r = ee.call(kConditionFactor, {Arg::ptr(kConditionLimits), Arg::ptr(cond)}, 1'000'000);
                    const sim::F ours_f = sim::engine_condition(program, sim::F(e), sim::F(o), nw != 0);
                    ++result.conditions, result.conditions_equal += r.f0 == ours_f.bits;
                    if (r.f0 != ours_f.bits && print) std::printf("  condition %g / %g: %g vs ours %g\n", double(e), double(o), double(r.f0_host()), double(gt3r::ee::to_host(ours_f.bits)));
                }
        // Cars built with a condition block (a new engine, worn oil; a worn engine, fresh oil): the power.
        size_t n = 0;
        for (const db::CarEntry& c : sdb.cars()) {
            if (n++ >= 60) break;
            sim::Equipment eq;
            if (!sim::stock_car(sdb, program, c.label, eq)) continue;
            for (const auto& [e, o] : {std::pair<float, float>{-300000, 5800000}, {8000000, -60000}}) {
                for (u32 k = 0; k < 0x20; k += 4) ee.w32(cond + k, 0);
                ee.wf(cond + 4, e), ee.wf(cond + 8, o);
                for (u32 k = 0; k < 0x200; k += 4) ee.w32(eq_at + k, 0);
                for (u32 k = 0; k < kShowroomSize; k += 4) ee.w32(car + k, 0);
                ee.w32(object, car), ee.w8(object + 4, 0);
                ee.call(kSetCar, {Arg::ptr(eq_at), Arg::integer(db::part_id(0, c.id))}, 50'000'000);
                ee.call(kShowroomBuild, {Arg::ptr(object), Arg::ptr(eq_at), Arg::ptr(cond)}, 400'000'000);
                const std::optional<sim::ShowroomFigures> f = ours.figures(eq, sim::engine_condition(program, sim::F(e), sim::F(o), false));
                ++result.worn;
                result.worn_equal += f && ee.r32(car + kPower) == f->power.bits && ee.r32(car + kEvaluation) == f->evaluation.bits &&
                                     s16(ee.r16(car + 0x1648)) == f->weight;
            }
        }
    }
    if (print)
        std::printf("showroom: conditions equal %zu of %zu, worn cars %zu of %zu\n", result.conditions_equal, result.conditions, result.worn_equal, result.worn);
    if (print)
        std::printf("showroom: %zu cars, %zu built; evaluation equal on %zu (worst %.2g), power on %zu (worst %.2g), torque on %zu\n",
                    result.cars, result.built, result.evaluation_equal, result.worst_evaluation, result.power_equal, result.worst_power,
                    result.torque_equal);
    return result;
}

PickerResult run_picker(Disc& disc, bool print) {
    namespace op = menu::opponents;
    PickerResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    std::mt19937 rng(4);
    const u32 arr = ee.alloc(kEntry * 200), out = ee.alloc(4 * 16);
    auto put = [&](const std::vector<op::Candidate>& list) {
        for (size_t i = 0; i < list.size(); ++i) {
            const u32 at = arr + kEntry * u32(i);
            for (u32 o = 0; o < 0x40; ++o) ee.w8(at + o, o < list[i].label.size() ? u8(list[i].label[o]) : 0);
            ee.w32(at + 0x40, list[i].score.bits);
        }
    };
    auto label_at = [&](u32 i) { return ee.read_cstr(arr + kEntry * i, 0x40); };
    auto made_up = [&](size_t n, int distinct) {
        std::vector<op::Candidate> list;
        for (size_t i = 0; i < n; ++i)
            list.push_back({"c" + std::to_string(i), op::F(float(int(rng() % u32(distinct))) * 0.0625f + 0.25f)});
        return list;
    };
    // The sort: lists of every length to 120, with few distinct evaluations (ties) and with many.
    for (int t = 0; t < 400; ++t) {
        std::vector<op::Candidate> list = made_up(size_t(t % 121), t % 2 ? 3 + t % 5 : 1000);
        put(list);
        ee.call(kQsort, {Arg::ptr(arr), Arg::integer(s32(list.size())), Arg::integer(s32(kEntry)), Arg::ptr(kOrder)}, 50'000'000);
        op::sort(list);
        bool same = true;
        for (size_t i = 0; i < list.size(); ++i) same = same && label_at(u32(i)) == list[i].label;
        ++result.sorts, result.sorts_equal += same;
        if (!same && print) std::printf("  sort of %zu differs\n", list.size());
    }
    // The search, on sorted lists, at the list's own values and between them.
    for (int t = 0; t < 400; ++t) {
        std::vector<op::Candidate> list = made_up(size_t(1 + t % 60), t % 3 ? 7 : 1000);
        op::sort(list);
        put(list);
        const op::F x = t % 2 ? list[rng() % list.size()].score : op::F(float(rng() % 1000) * 0.0632f);
        const int lo = int(rng() % list.size()), hi = int(list.size());
        const auto r = ee.call(kSearch, {Arg::ptr(arr), Arg::integer(lo), Arg::integer(hi), Arg::fbits(x.bits)}, 1'000'000);
        const int ours = op::search(list, lo, hi, x);
        ++result.searches, result.searches_equal += r.v0_s32() == ours;
    }
    // The draw: windows of 0 .. 12 places, 0 .. 5 places wanted, with and without a place excluded.
    for (int t = 0; t < 400; ++t) {
        const u32 seed = rng();
        const int lo = int(rng() % 20), hi = lo + int(rng() % 13), count = int(rng() % 6);
        const int exclude = t % 2 && hi > lo ? lo + int(rng() % u32(hi - lo)) : -1;
        ee.w32(kGenerator, seed);
        for (u32 i = 0; i < 16; ++i) ee.w32(out + 4 * i, 0x5A5A5A5A);
        ee.call(kDraw, {Arg::integer(lo), Arg::integer(hi), Arg::ptr(out), Arg::integer(count), Arg::integer(exclude)}, 5'000'000);
        menu::ProgramRandom g{seed};
        const std::vector<int> ours = op::draw(g, lo, hi, count, exclude);
        bool same = ee.r32(kGenerator) == g.s;
        for (int i = 0; i < count; ++i) same = same && s32(ee.r32(out + 4 * u32(i))) == ours[size_t(i)];
        ++result.draws, result.draws_equal += same;
        if (!same && print && result.draws - result.draws_equal < 4) std::printf("  draw [%d, %d) of %d without %d differs\n", lo, hi, count, exclude);
    }
    // The dirt step on the disc's cars (ARCADEINFO_NORMAL opened as the program opens it).
    if (const gt3r::Volume::Entry* e = disc.volume().find(sdb.folder() + "ARCADEINFO_NORMAL.dbt")) {
        const Bytes file = disc.volume().read(*e);
        const u32 image = ee.alloc_bytes(file), object = ee.alloc(0x6C);
        ee.call(kTableCtor, {Arg::ptr(object)});
        ee.call(kTableOpen, {Arg::ptr(object), Arg::ptr(image)});
        ee.w32(kArcadeTables, object);
        // The label lists the lookups by label read (the database's +0x88 + 4 x table id: an object whose first word is the
        // list's image), for GENERIC_CAR and ENEMY_CARS.
        for (const auto& [t, name] : {std::pair<u32, const char*>{0, "GENERIC_CAR"}, {0x26, "ENEMY_CARS"}})
            if (const gt3r::Volume::Entry* le = disc.volume().find(sdb.folder() + name + ".idi")) {
                const u32 list = ee.alloc(0x10);
                ee.w32(list, ee.alloc_bytes(disc.volume().read(*le)));
                ee.w32(kDatabase + 0x88 + 4 * t, list);
            }
        std::vector<std::string> labels;
        for (const db::CarEntry& c : sdb.cars()) labels.push_back(c.label);
        const db::Labels* names = sdb.labels("GENERIC_CAR");
        const db::Table* table = sdb.table("ARCADEINFO_NORMAL");
        const op::DirtRace dirt_race = [&](const std::string& label) {
            const std::optional<u32> id = names ? names->id(label) : std::nullopt;
            const ByteSpan r = table && id ? table->row(*id) : ByteSpan{};
            if (r.size() < 0x38) return -1;
            for (size_t i = 0x30; i < 0x38; ++i)
                if (r[i] != 0xFF) return 1;
            return 0;
        };
        for (int t = 0; t < 200; ++t) {
            std::vector<op::Candidate> list;
            const size_t n = 20 + rng() % 150;
            for (size_t i = 0; i < n; ++i) list.push_back({labels[rng() % labels.size()], op::F(0.5f)});
            put(list);
            const int count = 1 + int(rng() % 5), exclude = t % 2 ? int(rng() % n) : -1;
            std::vector<int> places;
            for (int i = 0; i < count; ++i) places.push_back(int(rng() % (n - 10)));
            for (int i = 0; i < count; ++i) ee.w32(out + 4 * u32(i), u32(places[size_t(i)]));
            ee.call(kDirt, {Arg::ptr(out), Arg::integer(count), Arg::ptr(arr), Arg::integer(exclude)}, 50'000'000);
            op::dirt_places(places, list, exclude, dirt_race);
            bool same = true;
            for (int i = 0; i < count; ++i) same = same && s32(ee.r32(out + 4 * u32(i))) == places[size_t(i)];
            ++result.dirt, result.dirt_equal += same;
            if (!same && print && result.dirt - result.dirt_equal <= 3) {
                std::printf("  dirt step, %d places, without %d:", count, exclude);
                for (int i = 0; i < count; ++i) std::printf(" %d/%d", s32(ee.r32(out + 4 * u32(i))), places[size_t(i)]);
                std::printf("\n");
                for (int i = 0; i < count; ++i) {
                    const int at = s32(ee.r32(out + 4 * u32(i)));
                    if (at >= 0 && size_t(at) < list.size()) std::printf("    theirs %s (%d)\n", list[size_t(at)].label.c_str(), dirt_race(list[size_t(at)].label));
                    if (places[size_t(i)] >= 0 && size_t(places[size_t(i)]) < list.size()) std::printf("    ours %s (%d)\n", list[size_t(places[size_t(i)])].label.c_str(), dirt_race(list[size_t(places[size_t(i)])].label));
                }
            }
        }
    }
    if (print)
        std::printf("picker: sort equal on %zu of %zu, search %zu of %zu, draw %zu of %zu, dirt step %zu of %zu\n", result.sorts_equal, result.sorts,
                    result.searches_equal, result.searches, result.draws_equal, result.draws, result.dirt_equal, result.dirt);
    return result;
}

} // namespace gt4r::diff
