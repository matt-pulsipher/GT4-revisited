// gt4r_diff's checks of the collision step and the road query against the program's, at every call of the running race
// (GT4R_STEP_COLLIDE with `step`, GT4R_AI_COLLIDE with `ai`): at 0x45BB60's entry every body record is read into ours
// (sim::collide::Body), ours steps them on our walls, and at its return the program's records are compared with ours
// field by field; at 0x3E5A30's entry ours answers the same point on our road, compared with the program's answer at
// its return.

#include "collide_check.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/sim/collide.h"
#include "gt4r/sim/ground.h"

#include <cmath>
#include <functional>
#include <cstdio>
#include <cstring>

namespace gt4r::diff {

namespace cl = sim::collide;
using gt3r::ee::F;

namespace {

constexpr u32 kCollide = 0x45BB60, kQuery = 0x3E5A30;
constexpr u32 kBodySize = 0x4F0;

// Each member of a body with its place in the record: kind 0 a float, 1 a word, 2 a wall point's pointer, 3 a car's
// pointer, 4 the bodies' pointer (not kept).
template <class V>
void body_fields(cl::Body& b, V&& v) {
    v("im", 0x000, b.im), v("ii", 0x004, b.ii);
    for (int k = 0; k < 8; ++k) {
        cl::Point& q = b.p[size_t(k)];
        const u32 o = 0x008 + 0x44 * u32(k);
        char n[24];
        auto nm = [&](const char* f) { return std::snprintf(n, sizeof n, "p%d.%s", k, f), std::string(n); };
        v(nm("lx"), o + 0x00, q.lx), v(nm("ly"), o + 0x04, q.ly), v(nm("ux"), o + 0x08, q.ux), v(nm("uy"), o + 0x0C, q.uy);
        v(nm("sx"), o + 0x10, q.sx), v(nm("sy"), o + 0x14, q.sy), v(nm("ex"), o + 0x18, q.ex), v(nm("ey"), o + 0x1C, q.ey);
        v(nm("flags"), o + 0x20, q.flags), v(nm("hit"), o + 0x24, q.hit), v(nm("t"), o + 0x28, q.t);
        v.wall(nm("wall"), o + 0x2C, q.wall, (q.hit || (q.flags & 1)));
        v(nm("tx"), o + 0x30, q.tx), v(nm("ty"), o + 0x34, q.ty), v.word(nm("other"), o + 0x3C, q.other), v.word(nm("edge"), o + 0x40, q.edge);
    }
    for (int k = 0; k < 8; ++k) {
        cl::Edge& e = b.e[size_t(k)];
        const u32 o = 0x228 + 0x2C * u32(k);
        char n[24];
        auto nm = [&](const char* f) { return std::snprintf(n, sizeof n, "e%d.%s", k, f), std::string(n); };
        v(nm("sx"), o + 0x00, e.sx), v(nm("sy"), o + 0x04, e.sy), v(nm("ex"), o + 0x08, e.ex), v(nm("ey"), o + 0x0C, e.ey);
        v(nm("nx"), o + 0x10, e.nx), v(nm("ny"), o + 0x14, e.ny), v(nm("d"), o + 0x18, e.d), v(nm("x1c"), o + 0x1C, e.x1c);
        v(nm("hit"), o + 0x20, e.hit), v(nm("t"), o + 0x24, e.t), v.wall(nm("wall"), o + 0x28, e.wall, e.hit != 0);
    }
    v("radius", 0x388, b.radius), v("ts", 0x38C, b.ts), v("sx", 0x390, b.sx), v("sy", 0x394, b.sy), v("sz", 0x398, b.sz), v("sh", 0x39C, b.sh);
    v("te", 0x3A0, b.te), v("ex", 0x3A4, b.ex), v("ey", 0x3A8, b.ey), v("ez", 0x3AC, b.ez), v("eh", 0x3B0, b.eh);
    v("dx", 0x3B4, b.dx), v("dy", 0x3B8, b.dy), v("dz", 0x3BC, b.dz), v("dh", 0x3C0, b.dh);
    v("vx", 0x3C4, b.vx), v("vy", 0x3C8, b.vy), v("vz", 0x3CC, b.vz), v("w", 0x3D0, b.w);
    v("bx0", 0x3D4, b.bx0), v("by0", 0x3D8, b.by0), v("bx1", 0x3DC, b.bx1), v("by1", 0x3E0, b.by1), v("bw", 0x3E4, b.bw), v("bh", 0x3E8, b.bh);
    v("have_points", 0x3EC, b.have_points), v("pair", 0x3F0, b.pair), v("wpoint", 0x3F4, b.wpoint), v("wcorner", 0x3F8, b.wcorner);
    v("t", 0x3FC, b.t), v("sensor", 0x400, b.sensor), v("sensor_t", 0x404, b.sensor_t);
    v.word("hit_edge", 0x408, b.hit_edge), v.word("other", 0x40C, b.other), v.word("vertex", 0x410, b.vertex);
    v("cpx", 0x414, b.cpx), v("cpy", 0x418, b.cpy), v.word("wk", 0x41C, b.wk), v("ux", 0x420, b.ux), v("uy", 0x424, b.uy);
    v.wall("wseg", 0x428, b.wseg, b.wpoint != 0), v.word("we", 0x42C, b.we);
    v("enabled", 0x430, b.enabled), v("front", 0x434, b.front), v("rear", 0x438, b.rear), v("wall_hit", 0x43C, b.wall_hit);
    v("car_hit", 0x440, b.car_hit), v("sensor_hit", 0x444, b.sensor_hit), v("strength", 0x448, b.strength), v.word("car", 0x44C, b.car);
    v("cx", 0x454, b.cx), v("cy", 0x458, b.cy), v("cz", 0x45C, b.cz), v("cvx", 0x460, b.cvx), v("cvy", 0x464, b.cvy), v("cvz", 0x468, b.cvz);
    v("pitch", 0x46C, b.pitch), v("roll", 0x470, b.roll), v("heading", 0x474, b.heading);
    v("pitch_rate", 0x478, b.pitch_rate), v("roll_rate", 0x47C, b.roll_rate), v("yaw_rate", 0x480, b.yaw_rate);
    v.word("hint", 0x484, b.hint);
    v("front_len", 0x48C, b.front_len), v("rear_len", 0x490, b.rear_len), v("half_width", 0x494, b.half_width), v("mass", 0x498, b.mass);
    v("iz10", 0x49C, b.iz10), v("chamfer1", 0x4A0, b.chamfer1), v("chamfer2", 0x4A4, b.chamfer2);
    for (int k = 0; k < cl::kMaxBodies; ++k) {
        char n[16];
        std::snprintf(n, sizeof n, "rsum%d", k);
        v(n, 0x4AC + 4 * u32(k), b.rsum[size_t(k)]);
        std::snprintf(n, sizeof n, "r2_%d", k);
        v(n, 0x4C4 + 4 * u32(k), b.r2[size_t(k)]);
    }
    v("dt", 0x4DC, b.dt), v.word("rumble_steps", 0x4E0, b.rumble_steps), v("rumble_level", 0x4E4, b.rumble_level);
    v.word("sound_steps", 0x4E8, b.sound_steps), v("sound_level", 0x4EC, b.sound_level);
}

u32 rd(const std::vector<u8>& r, u32 o) {
    u32 x;
    std::memcpy(&x, r.data() + o, 4);
    return x;
}

// Into ours from a record.
struct Loader {
    const std::vector<u8>& rec;
    u32 walls;
    void operator()(const std::string&, u32 o, F& f) const { f = F::raw(rd(rec, o)); }
    void operator()(const std::string&, u32 o, u32& x) const { x = rd(rec, o); }
    void word(const std::string&, u32 o, int& x) const { x = int(rd(rec, o)); }
    void wall(const std::string&, u32 o, int& x, bool) const {
        const u32 p = rd(rec, o);
        x = p >= walls ? int((p - walls) / 16) : -1;
    }
};

// Ours against the program's, member by member (each member found in ours at its offset in theirs).
struct Cmp {
    std::function<void(const std::string&, double, double)> off_by;
    const cl::Body& mine;
    const cl::Body& theirs;
    template <class T>
    const T& my(const T& m) const {
        return *reinterpret_cast<const T*>(reinterpret_cast<const u8*>(&mine) + (reinterpret_cast<const u8*>(&m) - reinterpret_cast<const u8*>(&theirs)));
    }
    void operator()(const std::string& n, u32, F& f) const {
        if (my(f).bits != f.bits) off_by(n, f.host(), my(f).host());
    }
    void operator()(const std::string& n, u32, u32& x) const {
        if (my(x) != x) off_by(n, double(x), double(my(x)));
    }
    void word(const std::string& n, u32, int& x) const {
        if (my(x) != x) off_by(n, double(x), double(my(x)));
    }
    void wall(const std::string& n, u32, int& x, bool used) const {
        if (used && my(x) != x) off_by(n, double(x), double(my(x)));
    }
};

} // namespace

CollideCheck::CollideCheck(Ee& ee, const OrigRace& race, Disc& disc, const std::string& course) : ee_(ee), race_(race) {
    const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
    if (!file) return;
    const Bytes data = disc.volume().read(*file);
    const course::CourseFile parsed = course::CourseFile::parse(data);
    road_ = std::make_unique<course::Runway>(course::Runway::parse(parsed.section(data, course::kSlotRunway)));
    ground_ = std::make_unique<sim::RoadGround>(*road_);
    walls_ = std::make_unique<cl::Walls>(*road_, *ground_);
}
CollideCheck::~CollideCheck() = default;

void CollideCheck::install(bool bodies, bool queries) {
    if (!walls_) return;
    if (bodies) ee_.add_probe(kCollide, [this](Ee&) { enter_step(); });
    if (queries) ee_.add_probe(kQuery, [this](Ee&) { enter_query(); });
}

void CollideCheck::note(const std::string& name, double theirs, double ours, const char* where) {
    Stat& s = fields_[name];
    ++s.off;
    if (s.example.empty()) {
        char b[200];
        std::snprintf(b, sizeof b, "the program's %.9g, ours %.9g (%s)", theirs, ours, where);
        s.example = b;
    }
}

void CollideCheck::enter_step() {
    const u32 m = ee_.g32(4);
    const int n = int(ee_.r32(m + 4));
    if (n <= 0 || n > cl::kMaxBodies) return;
    const u32 walls = ee_.r32(race_.runway + 0x84);
    Pending p;
    p.manager = m;
    p.bodies.resize(size_t(n));
    p.cars.resize(size_t(n));
    p.rumble.resize(size_t(n));
    for (int i = 0; i < n; ++i) {
        std::vector<u8> rec(kBodySize);
        ee_.read_bytes(m + 0xC + kBodySize * u32(i), rec.data(), kBodySize);
        Loader l{rec, walls};
        body_fields(p.bodies[size_t(i)], l);
        const u32 car = ee_.r32(m + 0xC + kBodySize * u32(i) + 0x450), F4 = car + 0x104;
        cl::CarIn& in = p.cars[size_t(i)];
        in.dt = F::raw(ee_.r32(F4 + 0x448));
        in.ghost = ee_.r8(F4 + 0x441) != 0 || ee_.r8(car + 0x14 + 0x1C) == 4;
        in.player = ee_.r8(F4 + 0x466) == 0 && ee_.r8(F4 + 0x467) == 0;
        in.steer = F::raw(ee_.r32(F4 + 0x45C));
        p.rumble[size_t(i)] = F::raw(ee_.r32(F4 + 0x610));
        p.car_addr.push_back(F4);
    }
    for (int i = 0; i < n; ++i) p.cars[size_t(i)].rumble = &p.rumble[size_t(i)];
    cl::step_all(p.bodies, p.cars, *walls_);
    for (int i = 0; i < n; ++i) p.cars[size_t(i)].rumble = nullptr;
    const u32 ra = ee_.g32(31);
    p.sp = ee_.g32(29);
    const bool fresh = steps_.find(ra) == steps_.end();
    steps_[ra].push_back(std::move(p));
    if (fresh) ee_.add_probe(ra, [this, ra](Ee&) { leave_step(ra); });
}

void CollideCheck::leave_step(u32 ra) {
    auto it = steps_.find(ra);
    if (it == steps_.end() || it->second.empty() || it->second.back().sp != ee_.g32(29)) return;
    Pending p = std::move(it->second.back());
    it->second.pop_back();
    ++calls_;
    const u32 walls = ee_.r32(race_.runway + 0x84);
    bool clean = true;
    char where[64];
    for (size_t i = 0; i < p.bodies.size(); ++i) {
        std::vector<u8> rec(kBodySize);
        ee_.read_bytes(p.manager + 0xC + kBodySize * u32(i), rec.data(), kBodySize);
        cl::Body theirs = p.bodies[i];
        Loader l{rec, walls};
        body_fields(theirs, l);
        std::snprintf(where, sizeof where, "body %zu, call %llu", i, static_cast<unsigned long long>(calls_));
        Cmp cmp{[&](const std::string& n, double a, double b) { clean = false, note(n, a, b, where); }, p.bodies[i], theirs};
        body_fields(theirs, cmp);
        const F r = F::raw(ee_.r32(p.car_addr[i] + 0x610));
        if (r.bits != p.rumble[i].bits) clean = false, note("car rumble", r.host(), p.rumble[i].host(), where);
    }
    clean_ += clean;
}

void CollideCheck::enter_query() {
    const u32 point = ee_.g32(6);
    PendingQuery q;
    q.answer = ee_.g32(5);
    q.sp = ee_.g32(29);
    const F at[3] = {F::raw(ee_.r32(point)), F::raw(ee_.r32(point + 4)), F::raw(ee_.r32(point + 8))};
    q.hit = ground_->query(at, q.mine);
    const u32 ra = ee_.g32(31);
    const bool fresh = queries_.find(ra) == queries_.end();
    queries_[ra].push_back(q);
    if (fresh) ee_.add_probe(ra, [this, ra](Ee&) { leave_query(ra); });
}

void CollideCheck::leave_query(u32 ra) {
    auto it = queries_.find(ra);
    if (it == queries_.end() || it->second.empty() || it->second.back().sp != ee_.g32(29)) return;
    PendingQuery q = it->second.back();
    it->second.pop_back();
    ++qcalls_;
    const bool hit = ee_.g32(2) != 0;
    char where[64];
    std::snprintf(where, sizeof where, "query %llu", static_cast<unsigned long long>(qcalls_));
    bool clean = true;
    auto cmp = [&](const char* n, u32 theirs, float mine) {
        const u32 m = F(mine).bits;
        if (theirs != m) clean = false, qnote(n, double(F::raw(theirs).host()), double(mine), where);
    };
    if (hit != q.hit) {
        clean = false;
        qnote("found", hit, q.hit, where);
    } else if (hit) {
        const u32 a = q.answer;
        const sim::GroundHit& h = q.mine;
        cmp("point.x", ee_.r32(a), h.point.x), cmp("point.y", ee_.r32(a + 4), h.point.y), cmp("point.z", ee_.r32(a + 8), h.point.z);
        if (ee_.r32(a + 0xC) != h.material) clean = false, qnote("material", ee_.r32(a + 0xC), h.material, where);
        if (ee_.r32(a + 0x10) != h.flags) clean = false, qnote("flags", ee_.r32(a + 0x10), h.flags, where);
        cmp("attr a", ee_.r32(a + 0x14), h.attr_a), cmp("attr b", ee_.r32(a + 0x18), h.attr_b);
        cmp("normal.x", ee_.r32(a + 0x1C), h.normal.x), cmp("normal.y", ee_.r32(a + 0x20), h.normal.y), cmp("normal.z", ee_.r32(a + 0x24), h.normal.z);
        cmp("plane", ee_.r32(a + 0x28), h.plane_d);
        if (s16(ee_.r16(a + 0x2C)) != h.triangle) clean = false, qnote("triangle", s16(ee_.r16(a + 0x2C)), h.triangle, where);
        if (s16(ee_.r16(a + 0x2E)) != h.cluster) clean = false, qnote("cluster", s16(ee_.r16(a + 0x2E)), h.cluster, where);
    }
    qclean_ += clean;
}

void CollideCheck::qnote(const std::string& name, double theirs, double ours, const char* where) {
    Stat& s = qfields_[name];
    ++s.off;
    if (s.example.empty()) {
        char b[200];
        std::snprintf(b, sizeof b, "the program's %.9g, ours %.9g (%s)", theirs, ours, where);
        s.example = b;
    }
}

void CollideCheck::print() const {
    if (calls_) {
        std::printf("  collision step (0x45BB60)       %llu calls, %llu with every body field equal\n", static_cast<unsigned long long>(calls_),
                    static_cast<unsigned long long>(clean_));
        for (const auto& [n, s] : fields_) std::printf("      %-22s differs in %llu: %s\n", n.c_str(), static_cast<unsigned long long>(s.off), s.example.c_str());
    }
    if (qcalls_) {
        std::printf("  road query (0x3E5A30)           %llu calls, %llu with every answer field equal\n", static_cast<unsigned long long>(qcalls_),
                    static_cast<unsigned long long>(qclean_));
        for (const auto& [n, s] : qfields_) std::printf("      %-22s differs in %llu: %s\n", n.c_str(), static_cast<unsigned long long>(s.off), s.example.c_str());
    }
}

} // namespace gt4r::diff
