#include "gt4r/race/rail_simulation.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

namespace {

Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
Vec3 unit(const Vec3& a, const Vec3& fallback = {0, 0, 1}) {
    const float l = length(a);
    return l > 1e-6f ? a * (1.0f / l) : fallback;
}
Vec3 level(const Vec3& a) { return {a.x, 0, a.z}; }
// `v` turned about the unit axis `k` by `angle` (right-handed).
Vec3 turn(const Vec3& v, const Vec3& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c));
}

constexpr Vec3 kUp{0, 1, 0};
constexpr float kGrip = 10.0f;      // sideways acceleration a car holds, metres per second squared
constexpr float kBraking = 11.0f;
constexpr float kMaxRoom = 7.0f;    // the most room used to either side of the line
constexpr float kTravel = 0.09f;    // a spring's travel either way, metres
constexpr float kRoadKind = 0;      // Runway::Triangle::kind of the paved road

// A critically underdamped spring toward `target`.
void spring(float& x, float& rate, float target, float omega, float damping, float dt) {
    rate += (omega * omega * (target - x) - 2 * damping * omega * rate) * dt;
    x += rate * dt;
}

} // namespace

RailLine::RailLine(const course::Runway& road) : length(road.length) {
    std::vector<course::Runway::Checkpoint> cps = road.checkpoints;
    std::sort(cps.begin(), cps.end(), [](const auto& a, const auto& b) { return a.v < b.v; });
    // Checkpoints at the same distance (a course's seam) would give a segment no length.
    cps.erase(std::unique(cps.begin(), cps.end(), [](const auto& a, const auto& b) { return std::fabs(a.v - b.v) < 0.01f; }), cps.end());
    const size_t n = cps.size();
    if (n < 3 || length <= 0) return;
    points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const auto& c = cps[i];
        Point& p = points[i];
        p.v = c.v;
        // To the right of the direction of travel: along the checkpoint, whichever way its ends
        // are stored.
        const Vec3 ahead = level(cps[(i + 1) % n].middle - cps[(i + n - 1) % n].middle);
        Vec3 across = unit(level(c.right - c.left), {1, 0, 0});
        if (dot(across, cross(ahead, kUp)) < 0) across = across * -1.0f;
        p.across = across;
        // The paved strip under the middle point: walk the checkpoint's line in half-metre steps
        // and keep the run of road triangles that holds the middle (the nearest one otherwise).
        const float to_left = std::fabs(dot(c.left - c.middle, across)), to_right = std::fabs(dot(c.right - c.middle, across));
        const float step = 0.5f;
        float best_a = 0, best_b = 0, best_gap = 1e9f;
        bool in_run = false, any = false;
        float run_a = 0;
        auto close_run = [&](float a, float b) {
            const float gap = a > 0 ? a : b < 0 ? -b : 0; // how far the run is from the middle
            if (gap < best_gap) best_gap = gap, best_a = a, best_b = b, any = true;
        };
        for (float d = -to_left; d <= to_right + step; d += step) {
            const Vec3 q = c.middle + across * d;
            float y;
            int tri = -1;
            const bool road_here = d <= to_right && road.height_at(q.x, q.z, c.middle.y + 4.0f, y, &tri) && tri >= 0 &&
                                   road.triangles[size_t(tri)].kind == kRoadKind && std::fabs(y - c.middle.y) < 4.0f;
            if (road_here && !in_run) in_run = true, run_a = d;
            if (!road_here && in_run) in_run = false, close_run(run_a, d - step);
        }
        float center = 0, room = 2.0f;
        if (any && best_b - best_a >= 2.0f) center = (best_a + best_b) * 0.5f, room = (best_b - best_a) * 0.5f;
        room = std::min(room, kMaxRoom);
        p.pos = c.middle + across * center;
        float y;
        if (road.height_at(p.pos.x, p.pos.z, c.middle.y + 4.0f, y)) p.pos.y = y;
        p.left = p.right = room;
    }
    // A pit entry or a run-off widens the strip for a checkpoint or two: keep the line from
    // stepping sideways there by easing each point toward its neighbors' mean.
    std::vector<Vec3> eased(n);
    for (size_t i = 0; i < n; ++i) {
        const Vec3 mean = (points[(i + n - 1) % n].pos + points[(i + 1) % n].pos) * 0.5f;
        const float side = dot(mean - points[i].pos, points[i].across);
        const float move = std::clamp(side * 0.5f, -points[i].left * 0.5f, points[i].right * 0.5f);
        eased[i] = points[i].pos + points[i].across * move;
    }
    for (size_t i = 0; i < n; ++i) points[i].pos = eased[i];
}

namespace {

// The curve through the line's points at `v`: its place and its derivative along the segment.
void curve_at(const RailLine& line, float v, Vec3& pos, Vec3& d1, size_t& segment, float& t) {
    const auto& points = line.points;
    const size_t n = points.size();
    v = std::fmod(v, line.length);
    if (v < 0) v += line.length;
    // The segment [i, i + 1) that holds v; the last one runs through the seam.
    size_t i = size_t(std::upper_bound(points.begin(), points.end(), v, [](float x, const RailLine::Point& p) { return x < p.v; }) - points.begin());
    i = i == 0 ? n - 1 : i - 1;
    const RailLine::Point& p1 = points[i];
    const RailLine::Point& p2 = points[(i + 1) % n];
    const RailLine::Point& p0 = points[(i + n - 1) % n];
    const RailLine::Point& p3 = points[(i + 2) % n];
    float span = p2.v - p1.v, along = v - p1.v;
    if (span <= 0) span += line.length;
    if (along < 0) along += line.length;
    t = std::clamp(along / span, 0.0f, 1.0f);
    const float t2 = t * t, t3 = t2 * t;
    // A Catmull-Rom curve through the points.
    const Vec3 a = p1.pos * 2.0f, b = p2.pos - p0.pos, c = p0.pos * 2.0f - p1.pos * 5.0f + p2.pos * 4.0f - p3.pos,
               d = p1.pos * 3.0f - p0.pos - p2.pos * 3.0f + p3.pos;
    pos = (a + b * t + c * t2 + d * t3) * 0.5f;
    d1 = (b + c * (2 * t) + d * (3 * t2)) * 0.5f;
    if (dot(d1, d1) < 1e-8f) d1 = p2.pos - p1.pos;
    segment = i;
}

} // namespace

RailLine::Sample RailLine::at(float v) const {
    Sample s;
    if (!valid()) return s;
    size_t i;
    float t;
    Vec3 d1;
    curve_at(*this, v, s.pos, d1, i, t);
    const Point& p1 = points[i];
    const Point& p2 = points[(i + 1) % points.size()];
    s.tangent = unit(d1);
    s.across = unit(cross(level(s.tangent), kUp), p1.across);
    s.left = p1.left + (p2.left - p1.left) * t;
    s.right = p1.right + (p2.right - p1.right) * t;
    // How fast the direction turns to the right per metre, on the level: over a car's length
    // either way, so that a kink between two points does not read as a hairpin.
    const float reach = 5.0f;
    Vec3 pa, pb, da, db;
    curve_at(*this, v - reach, pa, da, i, t);
    curve_at(*this, v + reach, pb, db, i, t);
    da = unit(level(da)), db = unit(level(db));
    const float sine = da.x * db.z - da.z * db.x, cosine = dot(da, db);
    s.curvature = std::atan2(sine, cosine) / (2 * reach);
    return s;
}

void RailLine::project(const Vec3& p, float& v, float& offset) const {
    v = 0, offset = 0;
    const size_t n = points.size();
    if (!valid()) return;
    float best = 1e30f;
    for (size_t i = 0; i < n; ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % n];
        const Vec3 ab = b.pos - a.pos;
        const float len2 = dot(ab, ab);
        const float t = len2 > 0 ? std::clamp(dot(p - a.pos, ab) / len2, 0.0f, 1.0f) : 0;
        const Vec3 q = a.pos + ab * t;
        const float d2 = dot(p - q, p - q);
        if (d2 >= best) continue;
        best = d2;
        float span = b.v - a.v;
        if (span <= 0) span += length;
        v = a.v + span * t;
    }
    if (v >= length) v -= length;
    const Sample s = at(v);
    offset = dot(p - s.pos, s.across);
}

RailSimulation::RailSimulation(const course::Runway& road, std::vector<RailCar> specs) : road_(&road), line_(road) {
    cars_.resize(specs.size());
    states_.resize(specs.size());
    for (size_t i = 0; i < specs.size(); ++i) {
        Car& c = cars_[i];
        c.spec = specs[i];
        if (i < road.grid.size()) {
            line_.project(road.grid[i].pos, c.v, c.offset);
        } else if (!road.grid.empty()) { // behind the last place, on alternate sides
            float v, offset;
            line_.project(road.grid.back().pos, v, offset);
            const float back = float(i - road.grid.size() + 1) * 8.0f;
            c.v = v - back, c.offset = (i & 1) ? 1.5f : -1.5f;
        } else {
            c.v = -float(i) * 8.0f, c.offset = (i & 1) ? 1.5f : -1.5f;
        }
        if (line_.valid()) {
            c.v = std::fmod(c.v, line_.length);
            if (c.v < 0) c.v += line_.length;
            const RailLine::Sample s = line_.at(c.v);
            c.offset = std::clamp(c.offset, -s.left, s.right);
        }
        place(c, states_[i], 0, 0, CarInput{}, 0);
        // A grid behind the line starts the first lap when it crosses it.
        states_[i].lap = line_.valid() && c.v > line_.length * 0.5f ? -1 : 0;
    }
}

void RailSimulation::step(std::span<const CarInput> inputs, float dt) {
    if (!line_.valid() || dt <= 0) return;
    for (size_t i = 0; i < cars_.size(); ++i) {
        Car& c = cars_[i];
        CarState& st = states_[i];
        const CarInput in = i < inputs.size() ? inputs[i] : CarInput{};
        const RailCar& spec = c.spec;
        const RailLine::Sample s = line_.at(c.v);

        // Along the road.
        const float throttle = std::clamp(in.throttle, 0.0f, 1.0f), brake = std::clamp(std::max(in.brake, in.handbrake ? 0.7f : 0.0f), 0.0f, 1.0f);
        float accel = 0;
        if (in.reverse) {
            accel = -throttle * spec.acceleration * 0.6f * std::max(0.0f, 1 + c.speed / 9.0f);
        } else {
            accel = throttle * spec.acceleration * std::max(0.0f, 1 - c.speed / spec.top_speed);
        }
        const float moving = c.speed > 0.05f ? 1.0f : c.speed < -0.05f ? -1.0f : 0.0f;
        accel -= moving * (brake * kBraking + 0.15f + 0.00045f * c.speed * c.speed);
        // A bend taken faster than the tires hold: the car runs wide and loses speed.
        const float sideways = c.speed * c.speed * s.curvature; // + to the right
        const float over = std::max(0.0f, std::fabs(sideways) - kGrip);
        accel -= moving * over * 0.6f;
        const float before = c.speed;
        c.speed += accel * dt;
        if (moving != 0 && before * c.speed < 0 && throttle < 0.01f) c.speed = 0; // braking stops at rest
        if (moving == 0 && throttle < 0.01f) c.speed = 0;
        const float long_accel = (c.speed - before) / dt;

        // Across the road.
        const float reach = std::clamp(std::fabs(c.speed), 0.0f, 16.0f) * 0.42f * (c.speed < 0 ? -1.0f : 1.0f);
        const float want = std::clamp(in.steer, -1.0f, 1.0f) * reach - (sideways > 0 ? 1.0f : -1.0f) * over * 0.35f;
        c.lateral_speed += (want - c.lateral_speed) * std::min(1.0f, 6.0f * dt);
        c.offset += c.lateral_speed * dt;
        const float lo = -s.left - 1.0f, hi = s.right + 1.0f;
        if (c.offset < lo) c.offset = lo, c.lateral_speed = std::max(c.lateral_speed, 0.0f);
        if (c.offset > hi) c.offset = hi, c.lateral_speed = std::min(c.lateral_speed, 0.0f);

        // A car to the inside of a bend covers less of the line per metre driven.
        c.v += c.speed * dt / std::max(0.3f, 1 - s.curvature * c.offset);
        if (c.v >= line_.length) c.v -= line_.length, ++st.lap;
        if (c.v < 0) c.v += line_.length, --st.lap;

        c.yaw = std::atan2(c.lateral_speed, std::max(std::fabs(c.speed), 4.0f)) * (c.speed < 0 ? -1.0f : 1.0f);

        // Gears: the top speed shared out over the gears, shifting by the revs.
        auto gear_top = [&](int g) { return spec.top_speed * std::pow(float(g) / float(spec.gears), 0.8f) * 1.02f; };
        if (in.reverse && c.speed <= 0.5f) c.gear = -1;
        else if (c.gear < 1) c.gear = 1;
        float rpm = 900;
        if (c.gear >= 1) {
            rpm = std::fabs(c.speed) / gear_top(c.gear) * spec.rev_limit;
            if (rpm > spec.rev_limit * 0.94f && c.gear < spec.gears) ++c.gear;
            else if (rpm < spec.rev_limit * 0.50f && c.gear > 1) --c.gear;
            const bool shift = in.shift_up || in.shift_down;
            if (shift && !c.shift_held) c.gear = std::clamp(c.gear + (in.shift_up ? 1 : -1), 1, spec.gears);
            c.shift_held = shift;
            rpm = std::fabs(c.speed) / gear_top(c.gear) * spec.rev_limit;
        } else {
            rpm = std::fabs(c.speed) / 9.0f * spec.rev_limit * 0.6f;
        }
        // (Standing on the throttle at rest revs the engine.)
        st.engine_rpm = std::clamp(std::max(rpm, 900.0f + throttle * (std::fabs(c.speed) < 3 ? 2500.0f : 0.0f)), 900.0f, spec.rev_limit);
        st.gear = c.gear;
        st.throttle = throttle, st.brake = brake, st.steer = std::clamp(in.steer, -1.0f, 1.0f);
        st.brake_lamps = brake > 0.02f;
        st.reverse_lamps = c.gear == -1;

        place(c, st, long_accel, sideways, in, dt);
    }
}

// The car on the road at its place along and across the line: the body's axes from the ground
// under its four wheels and the springs' lean, the wheels on the ground itself.
void RailSimulation::place(Car& c, CarState& st, float long_accel, float lat_accel, const CarInput& in, float dt) const {
    const RailCar& spec = c.spec;
    if (!line_.valid()) return;
    const RailLine::Sample s = line_.at(c.v);
    const Vec3 center = s.pos + s.across * c.offset;
    const Vec3 ahead = turn(unit(level(s.tangent)), kUp, -c.yaw); // (a positive yaw points the nose to the right)
    const Vec3 side = cross(ahead, kUp);

    // The ground under each wheel, and a smoothed copy the body rides on.
    Vec3 ground[4], smooth[4];
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        const float along = (axle == 0 ? 0.5f : -0.5f) * spec.wheelbase, out = ((w & 1) ? 1.0f : -1.0f) * spec.half_track[axle];
        Vec3 p = center + ahead * along + side * out;
        // The line's own height there, when no road is under the wheel.
        p.y = center.y + s.tangent.y / std::max(0.2f, length(level(s.tangent))) * along;
        float y;
        int tri = -1;
        const float top = (c.has_ground ? c.last_ground[w] : p.y) + 1.5f;
        st.wheels[w].on_ground = true;
        st.wheels[w].surface = 0;
        if (road_->height_at(p.x, p.z, top, y, &tri)) {
            p.y = y;
            if (tri >= 0) st.wheels[w].surface = road_->triangles[size_t(tri)].kind;
        }
        ground[w] = p;
        const float keep = c.has_ground && dt > 0 ? std::exp(-dt / 0.07f) : 0.0f;
        c.last_ground[w] = p.y + (c.last_ground[w] - p.y) * keep;
        smooth[w] = {p.x, c.last_ground[w], p.z};
    }
    c.has_ground = true;

    // The springs: the nose lifts under power and dips under braking, the body leans out of a bend.
    if (dt > 0) {
        spring(c.pitch, c.pitch_rate, std::clamp(long_accel * 0.0032f, -0.05f, 0.04f), 9.0f, 0.42f, dt);
        spring(c.roll, c.roll_rate, std::clamp(-lat_accel * 0.0036f, -0.06f, 0.06f), 8.0f, 0.42f, dt);
        spring(c.heave, c.heave_rate, -0.0012f * std::fabs(long_accel) - 0.001f * std::fabs(lat_accel), 10.0f, 0.4f, dt);
    }
    Vec3 forward = unit((smooth[0] + smooth[1]) - (smooth[2] + smooth[3]), ahead);
    Vec3 right = unit((smooth[1] + smooth[3]) - (smooth[0] + smooth[2]), side);
    Vec3 up = unit(cross(right, forward), kUp);
    right = unit(cross(forward, up), side);
    forward = turn(forward, right, c.pitch), up = turn(up, right, c.pitch);
    right = turn(right, forward, c.roll), up = turn(up, forward, c.roll);

    const Vec3 middle = (smooth[0] + smooth[1] + smooth[2] + smooth[3]) * 0.25f;
    st.position = middle + up * c.heave;
    st.right = right, st.up = up, st.forward = forward;
    st.speed = c.speed;
    st.velocity = s.tangent * c.speed + s.across * c.lateral_speed;
    st.course_v = c.v;

    const float slide = std::clamp((std::fabs(lat_accel) - kGrip * 0.85f) / (kGrip * 0.4f), 0.0f, 1.0f);
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        WheelState& ws = st.wheels[w];
        const float r = spec.tire_radius[axle];
        ws.center = ground[w] + kUp * r;
        const float along = (axle == 0 ? 0.5f : -0.5f) * spec.wheelbase, out = ((w & 1) ? 1.0f : -1.0f) * spec.half_track[axle];
        const Vec3 rest = st.position + forward * along + right * out + up * r;
        // (The springs' reach: past it the wheel leaves the road or the body's line.)
        ws.travel = std::clamp(dot(ws.center - rest, up), -kTravel, kTravel);
        ws.center = rest + up * ws.travel;
        // The front wheels point where the car is turning, a little more with the stick.
        ws.steer = axle == 0 ? std::clamp(std::atan(spec.wheelbase * s.curvature) + in.steer * 0.22f, -0.6f, 0.6f) : 0.0f;
        float rate = c.speed / r;
        ws.slip = slide;
        if (in.handbrake && axle == 1) rate = 0, ws.slip = std::fabs(c.speed) > 2 ? 1.0f : ws.slip;
        if (axle == 1 && in.throttle > 0.9f && std::fabs(c.speed) < 9.0f && !in.reverse && c.gear == 1) // spinning away from rest
            rate = std::max(rate, 14.0f / r), ws.slip = std::max(ws.slip, 0.8f);
        if (in.brake > 0.95f && std::fabs(c.speed) > 12.0f) ws.slip = std::max(ws.slip, 0.35f);
        ws.spin_rate = rate;
        ws.spin += rate * dt;
        if (ws.spin > 1e4f || ws.spin < -1e4f) ws.spin = std::fmod(ws.spin, 6.2831853f);
    }
}

CarInput follow_line(const RailLine& line, const CarState& car, float offset, float pace) {
    CarInput in;
    if (!line.valid()) return in;
    float v, off;
    line.project(car.position, v, off);
    const float speed = std::max(car.speed, 0.0f);
    const float look = 8.0f + speed * 0.55f;
    const RailLine::Sample target = line.at(v + look);
    const float room_left = target.left - 1.0f, room_right = target.right - 1.0f;
    const Vec3 aim = target.pos + target.across * std::clamp(offset, -std::max(room_left, 0.0f), std::max(room_right, 0.0f));
    const Vec3 to = unit(level(aim - car.position), car.forward);
    const Vec3 fwd = unit(level(car.forward));
    const Vec3 right = cross(fwd, kUp);
    const float angle = std::atan2(dot(to, right), dot(to, fwd));
    in.steer = std::clamp(angle * 2.2f, -1.0f, 1.0f);

    // The speed the bends ahead allow, braking in time for each.
    float allowed = 1e9f;
    const float hold = kGrip * 0.82f * std::clamp(pace, 0.2f, 1.0f), slow = kBraking * 0.6f;
    for (float d = 0; d <= 260.0f; d += 8.0f) {
        const float k = std::fabs(line.at(v + d).curvature);
        const float corner = std::sqrt(hold / std::max(k, 1e-4f));
        allowed = std::min(allowed, std::sqrt(corner * corner + 2 * slow * d));
    }
    if (speed < allowed - 1.5f) in.throttle = 1;
    else if (speed < allowed) in.throttle = 0.35f;
    else if (speed > allowed + 0.5f) in.brake = std::clamp((speed - allowed) / 5.0f, 0.1f, 1.0f);
    return in;
}

} // namespace gt4r::race
