#include "gt4r/race/replay_file.h"

#include "base/gzip.h"

#include <cstring>

namespace gt4r::race {

namespace {

struct Writer {
    Bytes b;
    void u8v(u8 v) { b.push_back(v); }
    void u32v(u32 v) {
        for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
    }
    void f(float v) {
        u32 x;
        std::memcpy(&x, &v, 4);
        u32v(x);
    }
    void vec(const Vec3& v) { f(v.x), f(v.y), f(v.z); }
    void str(const std::string& s) {
        u32v(u32(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
};

struct Reader {
    ByteSpan d;
    size_t at = 0;
    void need(size_t n) {
        if (at + n > d.size()) throw FormatError("replay: the file ends early");
    }
    u8 u8v() {
        need(1);
        return d[at++];
    }
    u32 u32v() {
        need(4);
        const u32 v = rd32(d, at);
        at += 4;
        return v;
    }
    float f() {
        const u32 x = u32v();
        float v;
        std::memcpy(&v, &x, 4);
        return v;
    }
    Vec3 vec() {
        Vec3 v;
        v.x = f(), v.y = f(), v.z = f();
        return v;
    }
    std::string str() {
        const u32 n = u32v();
        if (n > 4096) throw FormatError("replay: a bad text");
        need(n);
        std::string s(reinterpret_cast<const char*>(d.data() + at), n);
        at += n;
        return s;
    }
};

void put_state(Writer& w, const CarState& c) {
    w.vec(c.position), w.vec(c.right), w.vec(c.up), w.vec(c.forward), w.vec(c.velocity);
    w.f(c.speed), w.f(c.engine_rpm);
    w.u32v(u32(c.gear));
    w.f(c.throttle), w.f(c.brake), w.f(c.steer);
    w.u8v(u8((c.brake_lamps ? 1 : 0) | (c.reverse_lamps ? 2 : 0) | (c.landed ? 4 : 0)));
    for (const WheelState& wh : c.wheels) {
        w.vec(wh.center);
        w.f(wh.steer), w.f(wh.spin), w.f(wh.spin_rate), w.f(wh.travel), w.f(wh.slip);
        w.u8v(wh.on_ground ? 1 : 0), w.u8v(wh.surface), w.u8v(wh.material);
    }
    w.f(c.course_v);
    w.u32v(u32(c.lap));
    w.f(c.contact), w.u8v(c.contact_kind), w.u32v(u32(c.contact_car));
}

CarState get_state(Reader& r) {
    CarState c;
    c.position = r.vec(), c.right = r.vec(), c.up = r.vec(), c.forward = r.vec(), c.velocity = r.vec();
    c.speed = r.f(), c.engine_rpm = r.f();
    c.gear = int(s32(r.u32v()));
    c.throttle = r.f(), c.brake = r.f(), c.steer = r.f();
    const u8 flags = r.u8v();
    c.brake_lamps = flags & 1, c.reverse_lamps = flags & 2, c.landed = flags & 4;
    for (WheelState& wh : c.wheels) {
        wh.center = r.vec();
        wh.steer = r.f(), wh.spin = r.f(), wh.spin_rate = r.f(), wh.travel = r.f(), wh.slip = r.f();
        wh.on_ground = r.u8v() != 0, wh.surface = r.u8v(), wh.material = r.u8v();
    }
    c.course_v = r.f();
    c.lap = int(s32(r.u32v()));
    c.contact = r.f(), c.contact_kind = r.u8v(), c.contact_car = int(s32(r.u32v()));
    return c;
}

} // namespace

Bytes ReplayFile::encode() const {
    Writer body;
    body.str(course), body.str(sequence), body.str(race);
    body.u32v(u32(laps));
    body.u32v(u32(cars.size()));
    for (const Car& c : cars) body.str(c.label), body.u32v(c.paint);
    body.u32v(u32(steps.size()));
    for (const std::vector<CarState>& s : steps)
        for (size_t i = 0; i < cars.size(); ++i) put_state(body, i < s.size() ? s[i] : CarState{});
    Writer out;
    for (char ch : std::string("GT4RRPL1")) out.u8v(u8(ch));
    out.u32v(u32(body.b.size()));
    const Bytes packed = gt3r::deflate_zlib(body.b);
    out.u32v(u32(packed.size()));
    out.b.insert(out.b.end(), packed.begin(), packed.end());
    return out.b;
}

ReplayFile ReplayFile::decode(ByteSpan file) {
    if (!is_replay(file)) throw FormatError("not a replay of this game");
    const u32 size = rd32(file, 8), packed = rd32(file, 12);
    if (size > (1u << 30) || 16 + u64(packed) > file.size() || packed < 6) throw FormatError("replay: bad sizes");
    // (zlib: a two-byte header, the raw stream, the checksum.)
    const Bytes body = gt3r::inflate_raw(file.subspan(18, packed - 2), size);
    Reader r{body};
    ReplayFile f;
    f.course = r.str(), f.sequence = r.str(), f.race = r.str();
    f.laps = int(s32(r.u32v()));
    const u32 n = r.u32v();
    if (n > 64) throw FormatError("replay: too many cars");
    for (u32 i = 0; i < n; ++i) {
        Car c;
        c.label = r.str(), c.paint = r.u32v();
        f.cars.push_back(c);
    }
    const u32 steps = r.u32v();
    if (u64(steps) * n * 64 > body.size()) throw FormatError("replay: the steps do not fit");
    f.steps.resize(steps);
    for (std::vector<CarState>& s : f.steps) {
        s.reserve(n);
        for (u32 i = 0; i < n; ++i) s.push_back(get_state(r));
    }
    return f;
}

} // namespace gt4r::race
