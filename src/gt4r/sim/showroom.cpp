// Gran Turismo 4's showroom car (showroom.h).

#include "gt4r/sim/showroom.h"

#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_build.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/params_build.h"
#include "sim/exact/start.h"
#include "sim/exact/tick.h"

namespace gt4r::sim {

namespace ex = gt3r::sim::exact;
using gt3r::ee::int_to_f;
using gt3r::ee::to_int;

namespace {

constexpr F k0 = F::raw(0), kOne = F::raw(0x3F800000), kHalf = F::raw(0x3F000000);
constexpr F k60 = F::raw(0x42700000), kTwoPi = F::raw(0x40C90FDA);
constexpr F kRpm = F::raw(0x3DD6774E);       // 2 pi / 60
constexpr F kG = F::raw(0x411CCCCC);         // 9.8 (N m per kgf m)
constexpr F kPsRpm = F::raw(0x44330CCC);     // 716.2 (kgf m x rpm per PS)
constexpr F kKw = F::raw(0x3F3C49BA);        // 0.7355 (kW per PS)
constexpr F kDriveline4 = F::raw(0x41598A02); // 13.596193 PS
constexpr F kScale = F::raw(0x40200000);     // 2.5
constexpr F kFloor = F::raw(0x3C23D70A);     // 0.01
constexpr u32 kMotorBytes = 0x62045A;        // the motors' figures (0x355978 -> 0x8438C0 / 0x8438C4)

} // namespace

F capped_torque(const rules::EngineBlock& e, F t, F w) {
    if (e.power_cap_on == 0) return t;
    const F soft = e.power_cap_soft, cap = e.power_cap;
    const F p = t * w;
    if (p <= cap * (kOne - soft)) return t;
    F lim = cap * (kOne - soft * kHalf);
    if (!(cap <= p)) {
        const F d = cap - p;
        lim = lim - (d * d) / ((soft + soft) * cap);
    }
    lim = lim / w;
    return lim < t ? lim : t;
}

EngineFigures engine_figures(const rules::EngineBlock& e, F condition) {
    EngineFigures f;
    f.ps = k0, f.kgfm = k0;
    f.rev_limit = e.rev_limit;
    const int n = e.torque.n;
    // The points' speeds, to the nearest 100 rpm (half up), each kept when above the last kept.
    for (int i = 0; i < n && i < 24; ++i) {
        s32 r = to_int((e.torque.x[size_t(i)] * k60) / kTwoPi);
        r = ((r + 50) / 100) * 100;
        if (f.rpm.empty() || s32(f.rpm.back()) < r) f.rpm.push_back(s16(r));
    }
    if (f.rpm.empty()) return f;
    s32 top = s32(e.rev_limit);
    if (top < s32(f.rpm.back())) top = s32(f.rpm.back());
    size_t next = 0;
    for (s32 r = f.rpm.front(); r <= top; r += 100) {
        const F rpm = int_to_f(r);
        const F w = rpm * kRpm;
        // 0x34E9A0: the full-throttle torque by the curve times the condition's factor; 0x34EA68 its cap.
        F t = ex::lookup_bisect(e.torque, w);
        t = t * condition;
        t = capped_torque(e, t, w);
        const F kgfm = t / kG;
        const F ps = (kgfm * rpm) / kPsRpm;
        if (next < f.rpm.size() && s32(f.rpm[next]) == r) {
            f.point_ps.push_back(ps), f.point_kgfm.push_back(kgfm);
            ++next;
        }
        if (s32(f.rev_limit) < r) continue;
        if (f.kgfm < kgfm) f.kgfm = kgfm, f.torque_rpm = s16(r);
        if (f.ps < ps) f.ps = ps, f.power_rpm = s16(r);
    }
    return f;
}

F extra_power(const rules::DrivelineBlock& d, const Program& program) {
    if (d.layout == 7) {
        const F first = int_to_f(program.b(kMotorBytes)), second = int_to_f(program.b(kMotorBytes + 1));
        return (first + first + second) / kKw;
    }
    return d.x13 == 4 ? kDriveline4 : k0;
}

F evaluation(const Record& r, F ps, F kgfm) {
    const ex::Params& p = r.car.p;
    const rules::TireExtra& tires = r.x.tires;
    const rules::BodyExtra& body = r.x.body;
    // The grip: the axles' mean (P +0xFD0 / +0xFD4), and for the lift under the body (kind 7) times
    // 1 + the lift over the weight.
    F grip = (tires.axle_grip[0] + tires.axle_grip[1]) * kHalf;
    // The aerodynamics: 1 + the downforce coefficients' mean - the drag.
    F aero = (p.downforce_coef[0] + p.downforce_coef[1]) * kHalf;
    aero = aero - p.drag_coef;
    if (body.speed_part_kind == 7) grip = grip * (body.speed_part_lift / p.weight + kOne);
    aero = aero + kOne;
    // The front paved tire's grip on the first surface (P +0x418), squared, the power squared and the
    // torque's root over the mass to the power 1.5.
    const F mu = tires.tire[0].mu[0];
    const F root_mass = gt3r::ee::sqrt(p.mass);
    F x = mu * mu;
    const F root_torque = gt3r::ee::sqrt(kgfm);
    const F mass15 = p.mass * root_mass;
    x = x * ps;
    x = x * ps;
    x = x * root_torque;
    x = x / mass15;
    x = x * aero;
    x = x * grip;
    // Its eighth root over 2.5, at most 1; below 0: 0.01.
    F v = gt3r::ee::sqrt(gt3r::ee::sqrt(gt3r::ee::sqrt(x)));
    v = v / kScale;
    if (kOne < v) v = kOne;
    if (v < k0) v = kFloor;
    return v;
}

struct Showroom::Data {
    ex::ProgramData core;
};

Showroom::Showroom(db::SpecDb& db, const Program& program) : db_(db), program_(program), data_(std::make_unique<Data>()) {
    data_->core = core_program(program);
}

Showroom::~Showroom() = default;

bool Showroom::record(const Equipment& eq, Spec& spec, Record& out) const {
    build_spec(db_, program_, eq, 0, spec);
    if (spec.weight_kg() <= 0 || spec.b(0xBF) == 0) return false;
    ex::Spec core;
    core_spec(spec, core);
    ex::ConvertInputs in;
    in.data = &data_->core;
    in.base = 0x01000000u + ex::kParams;
    in.control = 0;
    in.physics_globals();
    ex::ParamImage image{};
    ex::convert_params(core, in, image);
    ex::Record whole{};
    std::copy(image.begin(), image.end(), whole.begin() + ex::kParams);
    out = Record{};
    ex::load_image(out.car, whole.data());
    gt4_params(spec, program_, out.car.p);
    gt4_extras(spec, program_, out);
    return true;
}

std::optional<ShowroomFigures> Showroom::figures(const Equipment& eq) const {
    Spec spec;
    Record r;
    if (!program_.known() || !record(eq, spec, r)) return std::nullopt;
    // ShowRoomCar's condition (0x364868: the default block, 0x3433E8) leaves the engine's curve as it is.
    const EngineFigures e = engine_figures(r.x.drive.e, kOne);
    const F extra = extra_power(r.x.drive.d, program_);
    ShowroomFigures f;
    f.power = e.ps + extra;
    f.torque = e.kgfm;
    f.power_rpm = e.power_rpm, f.torque_rpm = e.torque_rpm;
    f.evaluation = evaluation(r, f.power, e.kgfm);
    f.mass = r.car.p.mass;
    return f;
}

std::optional<ShowroomFigures> Showroom::stock(const std::string& car_label) const {
    Equipment eq;
    if (!stock_car(db_, program_, car_label, eq)) return std::nullopt;
    return figures(eq);
}

} // namespace gt4r::sim
