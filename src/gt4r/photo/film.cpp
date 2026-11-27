#include "gt4r/photo/film.h"

#include "base/ee_float.h"

#include <cmath>
#include <cstring>

namespace gt4r::photo::film {

namespace {

constexpr float kDeg = 57.2957795f;

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(const Vec3& a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

u32 tag_of(const char tag[5]) { return u32(u8(tag[0])) | u32(u8(tag[1])) << 8 | u32(u8(tag[2])) << 16 | u32(u8(tag[3])) << 24; }

} // namespace

// ---------------------------------------------------------------------------------------------- the stream

void Writer::f32_(float v) {
    u32 b;
    std::memcpy(&b, &v, 4);
    u32_(b);
}

void Writer::text(const std::string& s) {
    for (char c : s)
        if (c) u8_(u8(c));
    u8_(0);
}

// A chunk opened (0x45B4D8): its head's place kept, 8 bytes held for it; closed (0x45B548): the tag and the size with the head.
void Writer::open(const char tag[5]) {
    open_.push_back(out_.size());
    u32_(tag_of(tag)), u32_(0);
}

void Writer::close() {
    if (open_.empty()) return;
    const size_t at = open_.back();
    open_.pop_back();
    const u32 size = u32(out_.size() - at);
    for (int i = 0; i < 4; ++i) out_[at + 4 + size_t(i)] = u8(size >> (8 * i));
}

u8 Reader::u8_() {
    if (at_ + 1 > end_) return ok_ = false, 0;
    return d_[at_++];
}
u16 Reader::u16_() {
    const u16 lo = u8_();
    return u16(lo | u16(u8_()) << 8);
}
u32 Reader::u32_() {
    const u32 lo = u16_();
    return lo | u32(u16_()) << 16;
}
float Reader::f32_() {
    const u32 b = u32_();
    float v;
    std::memcpy(&v, &b, 4);
    return v;
}
Vec3 Reader::vec() {
    Vec3 v;
    v.x = f32_(), v.y = f32_(), v.z = f32_();
    return v;
}
std::string Reader::text() {
    std::string s;
    while (ok_)
        if (const u8 c = u8_()) s += char(c);
        else break;
    return s;
}
void Reader::bytes(u8* to, size_t n) {
    for (size_t i = 0; i < n; ++i) to[i] = u8_();
}

std::string Reader::next_tag() const {
    if (at_ + 8 > end_) return {};
    return std::string(reinterpret_cast<const char*>(d_.data() + at_), 4);
}

std::optional<Reader> Reader::chunk(const char tag[5]) {
    if (at_ + 8 > end_ || rd32(d_, at_) != tag_of(tag)) return std::nullopt;
    const u32 size = rd32(d_, at_ + 4);
    if (size < 8 || at_ + size > end_) return std::nullopt;
    Reader r(d_, at_ + 8, at_ + size);
    at_ += size;
    return r;
}

std::optional<std::pair<std::string, Bytes>> Reader::raw_chunk() {
    const std::string tag = next_tag();
    if (tag.empty()) return std::nullopt;
    const u32 size = rd32(d_, at_ + 4);
    if (size < 8 || at_ + size > end_) return std::nullopt;
    std::pair<std::string, Bytes> out{tag, Bytes(d_.begin() + ptrdiff_t(at_ + 8), d_.begin() + ptrdiff_t(at_ + size))};
    at_ += size;
    return out;
}

// ---------------------------------------------------------------------------------------------- the film

namespace {

void put_raw(Writer& w, const char tag[5], const std::optional<Bytes>& b) {
    if (!b) return;
    w.open(tag), w.bytes(*b), w.close();
}

// CPRM (0x459E30): u8 0, then the sponsor's virtuals in this order.
void write_params(Writer& w, const Params& p) {
    w.open("CPRM");
    w.u8_(0);
    w.vec(p.ground_turn), w.vec(p.ground_at), w.vec(p.velocity), w.vec(p.turn), w.vec(p.at);
    w.f32_(p.v15), w.f32_(p.v16);
    w.u8_(p.v18 ? 1 : 0);
    w.f32_(p.v19), w.f32_(p.v20), w.f32_(p.v21), w.f32_(p.v22);
    for (const Params::Wheel& h : p.wheels) {
        w.vec(h.turn), w.vec(h.at);
        w.f32_(h.v11), w.f32_(h.v12), w.f32_(h.v13), w.f32_(h.v14), w.f32_(h.spun), w.f32_(h.v24), w.f32_(h.v25);
    }
    for (float v : p.v26) w.f32_(v);
    w.f32_(p.rpm);
    for (const Vec3& v : p.v27) w.vec(v);
    w.close();
}

std::optional<Params> read_params(Reader r) {
    Params p;
    r.u8_();
    p.ground_turn = r.vec(), p.ground_at = r.vec(), p.velocity = r.vec(), p.turn = r.vec(), p.at = r.vec();
    p.v15 = r.f32_(), p.v16 = r.f32_();
    p.v18 = r.u8_() != 0;
    p.v19 = r.f32_(), p.v20 = r.f32_(), p.v21 = r.f32_(), p.v22 = r.f32_();
    for (Params::Wheel& h : p.wheels) {
        h.turn = r.vec(), h.at = r.vec();
        h.v11 = r.f32_(), h.v12 = r.f32_(), h.v13 = r.f32_(), h.v14 = r.f32_(), h.spun = r.f32_(), h.v24 = r.f32_(), h.v25 = r.f32_();
    }
    for (float& v : p.v26) v = r.f32_();
    p.rpm = r.f32_();
    for (Vec3& v : p.v27) v = r.vec();
    if (!r.ok()) return std::nullopt;
    return p;
}

} // namespace

Bytes gadgets_payload(std::span<const GadgetSlot> slots) {
    using gt3r::ee::F;
    Writer w;
    w.u8_(0), w.u16_(u16(slots.size()));
    for (const GadgetSlot& g : slots) {
        w.u8_(g.used ? 1 : 0);
        if (!g.used) continue;
        w.u8_(g.moving ? 1 : 0);
        if (!g.moving) continue;
        w.u8_(g.kind);
        // (0x3E55F8: mul.s by 128, div.s by pi 0x40490FDA, cvt.w.s, the low byte)
        for (float a : g.angles) w.u8_(u8(gt3r::ee::to_int(F(a) * F::raw(0x43000000) / F::raw(0x40490FDA))));
        w.vec(g.pos);
    }
    return w.data();
}

Bytes no_particles() { return Bytes{0, 0}; }

namespace {

namespace rp = race::posture;
struct Q {
    float x, y, z, w;
};
Q qmul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// The turn whose matrix has the columns c0, c1, c2 (the figure's x, y, z in course space).
Q quat_of(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
    const float t = c0.x + c1.y + c2.z;
    Q q;
    if (t > 0) {
        const float s = std::sqrt(t + 1) * 2;
        q = {(c1.z - c2.y) / s, (c2.x - c0.z) / s, (c0.y - c1.x) / s, s / 4};
    } else if (c0.x > c1.y && c0.x > c2.z) {
        const float s = std::sqrt(1 + c0.x - c1.y - c2.z) * 2;
        q = {s / 4, (c1.x + c0.y) / s, (c2.x + c0.z) / s, (c1.z - c2.y) / s};
    } else if (c1.y > c2.z) {
        const float s = std::sqrt(1 + c1.y - c0.x - c2.z) * 2;
        q = {(c1.x + c0.y) / s, s / 4, (c2.y + c1.z) / s, (c2.x - c0.z) / s};
    } else {
        const float s = std::sqrt(1 + c2.z - c0.x - c1.y) * 2;
        q = {(c2.x + c0.z) / s, (c2.y + c1.z) / s, s / 4, (c0.y - c1.x) / s};
    }
    return q;
}
bool plate(int n) { return n == rp::kPelvis || n == rp::kScapula; }
Vec3 host(const rp::V3& v) { return {v.x.host(), v.y.host(), v.z.host()}; }
rp::V3 ee(const Vec3& v) { return {rp::f(v.x), rp::f(v.y), rp::f(v.z)}; }

} // namespace

Bytes figure_payload(const rp::Skeleton& s, const Vec3& at, const Vec3& right, const Vec3& up, const Vec3& forward) {
    // The figure's frame: x right, y up, z back.
    const Vec3 back = mul(forward, -1);
    const Q turn = quat_of(right, up, back);
    Writer w;
    w.u8_(1);
    for (int n = 0; n < rp::kNodes; ++n) {
        const rp::Bone& b = s[n];
        const Vec3 p = host(b.pos);
        w.vec(add(add(add(at, mul(right, p.x)), mul(up, p.y)), mul(back, p.z)));
        const Q q = qmul(turn, {b.q.x.host(), b.q.y.host(), b.q.z.host(), b.q.w.host()});
        w.f32_(q.x), w.f32_(q.y), w.f32_(q.z), w.f32_(q.w);
        w.vec(host(b.vec));
        if (plate(n)) w.vec(host(b.left)), w.vec(host(b.right));
    }
    // ❓ The figure's four floats (+0x680) and the posture caller's fields, as the original's film holds them (1, 1, 0, 0.5; a
    // bool 1, a word that changes from frame to frame, 1, 0, 0, the identity, 0, 0).
    for (float v : {1.0f, 1.0f, 0.0f, 0.5f}) w.f32_(v);
    w.u8_(1), w.u32_(0), w.u8_(1), w.f32_(0), w.f32_(0);
    for (int i = 0; i < 16; ++i) w.f32_(i % 5 == 0 ? 1.0f : 0.0f);
    w.u8_(0);
    for (int i = 0; i < 8; ++i) w.u8_(0);
    return w.data();
}

std::optional<rp::Skeleton> figure_of(ByteSpan payload, const Vec3& at, const Vec3& right, const Vec3& up, const Vec3& forward, const rp::Skeleton& base) {
    if (payload.size() < kFigureBytes) return std::nullopt;
    Reader r(payload);
    if (r.u8_() != 1) return std::nullopt;
    const Vec3 back = mul(forward, -1);
    const Q turn = quat_of(right, up, back), undo{-turn.x, -turn.y, -turn.z, turn.w};
    rp::Skeleton s = base;
    for (int n = 0; n < rp::kNodes; ++n) {
        rp::Bone& b = s[n];
        const Vec3 d = sub(r.vec(), at);
        b.pos = ee({dot(d, right), dot(d, up), dot(d, back)});
        Q q;
        q.x = r.f32_(), q.y = r.f32_(), q.z = r.f32_(), q.w = r.f32_();
        q = qmul(undo, q);
        b.q = {rp::f(q.x), rp::f(q.y), rp::f(q.z), rp::f(q.w)};
        b.vec = ee(r.vec());
        if (plate(n)) b.left = ee(r.vec()), b.right = ee(r.vec());
    }
    if (!r.ok()) return std::nullopt;
    return s;
}

Bytes Film::encode() const {
    Writer w;
    // PMDI and FILM (0x334FC8).
    w.open("PMDI"), w.u8_(photo.pmdi ? 1 : 0), w.close();
    w.open("FILM");
    w.u8_(2);
    w.u16_(photo.tile_w), w.u16_(photo.tile_h), w.u8_(photo.tiles_x), w.u8_(photo.tiles_y);
    w.f32_(photo.aspect);
    w.u8_(photo.upright ? 1 : 0), w.u8_(photo.pictures), w.u8_(photo.p124 ? 1 : 0);
    w.f32_(photo.shutter_frames);
    w.u32_(u32(photo.filter));
    w.f32_(photo.p12c), w.f32_(photo.saturation), w.f32_(photo.exposure), w.f32_(photo.kelvin), w.f32_(photo.p13c);
    w.bytes(photo.frame);
    w.close();
    // RHDR (0x33A2A0): u8 2, the course, the race's kind, the cars, the frames, the display state when kept, each car's CDTL.
    w.open("RHDR");
    w.u8_(2);
    w.text(course), w.text(kind);
    w.u8_(u8(cars.size()));
    for (const Car& c : cars) w.text(c.label), w.text(c.paint), w.u8_(c.b);
    w.u32_(u32(frames.size()));
    put_raw(w, "ENVP", display);
    for (const Car& c : cars) {
        w.open("CDTL");
        w.u8_(1);
        for (const std::string& s : c.looks) w.text(s);
        w.u8_(c.c);
        w.close();
    }
    w.close();
    // SFRM a frame (0x33A018): its camera (PCAM, 0x33ADE8), a CARD a car (0x339F38), the gadgets and particles when kept.
    for (const Frame& f : frames) {
        w.open("SFRM");
        w.open("PCAM");
        w.u8_(1);
        const Camera& c = f.camera;
        w.vec(c.eye), w.vec(c.target), w.vec(c.up);
        w.f32_(c.fov), w.f32_(c.aspect), w.f32_(c.near), w.f32_(c.far);
        w.f32_(c.f_number), w.f32_(c.shutter), w.f32_(c.exposure), w.f32_(c.kelvin);
        w.close();
        for (const CarFrame& car : f.cars) {
            w.open("CARD");
            write_params(w, car.params);
            put_raw(w, "DRVR", car.driver);
            put_raw(w, "NAVI", car.navigator);
            put_raw(w, "SMOK", car.skids);
            w.close();
        }
        put_raw(w, "GDGT", f.gadgets);
        put_raw(w, "PTCL", f.particles);
        w.close();
    }
    return w.data();
}

std::optional<Film> Film::decode(ByteSpan data) {
    Film film;
    Reader top(data);
    if (auto r = top.chunk("PMDI")) film.photo.pmdi = r->u8_() != 0;
    auto f = top.chunk("FILM");
    if (!f) return std::nullopt;
    Photo& p = film.photo;
    f->u8_();
    p.tile_w = f->u16_(), p.tile_h = f->u16_(), p.tiles_x = f->u8_(), p.tiles_y = f->u8_();
    p.aspect = f->f32_();
    p.upright = f->u8_() != 0, p.pictures = f->u8_(), p.p124 = f->u8_() != 0;
    p.shutter_frames = f->f32_();
    p.filter = s32(f->u32_());
    p.p12c = f->f32_(), p.saturation = f->f32_(), p.exposure = f->f32_(), p.kelvin = f->f32_(), p.p13c = f->f32_();
    f->bytes(p.frame.data(), p.frame.size());
    if (!f->ok()) return std::nullopt;
    auto h = top.chunk("RHDR");
    if (!h) return std::nullopt;
    h->u8_();
    film.course = h->text(), film.kind = h->text();
    const u8 n = h->u8_();
    for (u8 i = 0; i < n; ++i) {
        Car c;
        c.label = h->text(), c.paint = h->text(), c.b = h->u8_();
        film.cars.push_back(std::move(c));
    }
    const u32 frames = h->u32_();
    if (!h->ok() || frames > 2) return std::nullopt;
    if (auto e = h->chunk("ENVP")) {
        Bytes b(e->end() - e->at());
        e->bytes(b.data(), b.size());
        film.display = std::move(b);
    }
    for (Car& c : film.cars)
        if (auto d = h->chunk("CDTL")) {
            d->u8_();
            for (std::string& s : c.looks) s = d->text();
            c.c = d->u8_();
        }
    while (auto s = top.chunk("SFRM")) {
        Frame fr;
        auto cam = s->chunk("PCAM");
        if (!cam) return std::nullopt;
        cam->u8_();
        Camera& c = fr.camera;
        c.eye = cam->vec(), c.target = cam->vec(), c.up = cam->vec();
        c.fov = cam->f32_(), c.aspect = cam->f32_(), c.near = cam->f32_(), c.far = cam->f32_();
        c.f_number = cam->f32_(), c.shutter = cam->f32_(), c.exposure = cam->f32_(), c.kelvin = cam->f32_();
        if (!cam->ok()) return std::nullopt;
        while (auto card = s->chunk("CARD")) {
            CarFrame car;
            auto prm = card->chunk("CPRM");
            if (!prm) return std::nullopt;
            const std::optional<Params> params = read_params(*prm);
            if (!params) return std::nullopt;
            car.params = *params;
            while (auto raw = card->raw_chunk()) {
                if (raw->first == "DRVR") car.driver = std::move(raw->second);
                else if (raw->first == "NAVI") car.navigator = std::move(raw->second);
                else if (raw->first == "SMOK") car.skids = std::move(raw->second);
            }
            fr.cars.push_back(std::move(car));
        }
        while (auto raw = s->raw_chunk()) {
            if (raw->first == "GDGT") fr.gadgets = std::move(raw->second);
            else if (raw->first == "PTCL") fr.particles = std::move(raw->second);
        }
        film.frames.push_back(std::move(fr));
    }
    if (film.frames.empty()) return std::nullopt;
    return film;
}

// ---------------------------------------------------------------------------------------------- a car's state

Vec3 angles_of(const Vec3& right, const Vec3& up, const Vec3& forward) {
    (void)up;
    const float pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, forward.y)));
    const float yaw = std::atan2(-forward.x, -forward.z);
    // The right axis taken back through the yaw and the pitch is the roll's (cos, sin, 0).
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const Vec3 r1{cy * right.x - sy * right.z, right.y, sy * right.x + cy * right.z}; // Ry(-yaw)
    const Vec3 r2{r1.x, cp * r1.y + sp * r1.z, -sp * r1.y + cp * r1.z};               // Rx(-pitch)
    const float roll = std::atan2(r2.y, r2.x);
    return {pitch * kDeg, yaw * kDeg, roll * kDeg};
}

void axes_of(const Vec3& angles, Vec3& right, Vec3& up, Vec3& forward) {
    const float p = angles.x / kDeg, y = angles.y / kDeg, r = angles.z / kDeg;
    const float cp = std::cos(p), sp = std::sin(p), cy = std::cos(y), sy = std::sin(y), cr = std::cos(r), sr = std::sin(r);
    // Ry(yaw) Rx(pitch) Rz(roll) applied to the body's x, y and -z.
    auto turn = [&](Vec3 v) {
        v = {cr * v.x - sr * v.y, sr * v.x + cr * v.y, v.z};       // Rz
        v = {v.x, cp * v.y - sp * v.z, sp * v.y + cp * v.z};       // Rx
        return Vec3{cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z}; // Ry
    };
    right = turn({1, 0, 0}), up = turn({0, 1, 0}), forward = turn({0, 0, -1});
}

Params params_of(const race::CarState& s, const race::CarState* before) {
    Params p;
    p.turn = p.ground_turn = angles_of(s.right, s.up, s.forward);
    p.ground_at = s.position;
    p.at = sub(s.position, mul(s.up, s.ground_drop));
    p.velocity = s.velocity;
    p.v18 = s.brake_lamps;
    p.rpm = std::round(s.engine_rpm);
    for (size_t w = 0; w < 4; ++w) {
        const race::WheelState& o = s.wheels[w];
        Params::Wheel& h = p.wheels[w];
        // Its angle as the program counts it (the spin less: virtual 9), its steering; its place in the body's frame.
        h.turn = {-std::fmod(o.spin, 6.2831855f) * kDeg, -o.steer * kDeg, 0};
        const Vec3 d = sub(o.center, p.at);
        h.at = {dot(d, s.right), dot(d, s.up), -dot(d, s.forward)};
        const float last = before ? -std::fmod(before->wheels[w].spin, 6.2831855f) * kDeg : h.turn.x + o.spin_rate / 60 * kDeg;
        h.spun = std::remainder(h.turn.x - last, 360.0f);
        h.v14 = -h.spun;
    }
    return p;
}

race::CarState state_of(const Params& p) {
    race::CarState s;
    axes_of(p.turn, s.right, s.up, s.forward);
    s.position = p.ground_at;
    s.ground_drop = dot(sub(p.ground_at, p.at), s.up);
    s.velocity = p.velocity;
    s.speed = dot(p.velocity, s.forward);
    s.engine_rpm = p.rpm;
    s.brake_lamps = p.v18;
    s.body_placed = true;
    for (size_t w = 0; w < 4; ++w) {
        const Params::Wheel& h = p.wheels[w];
        race::WheelState& o = s.wheels[w];
        o.center = add(add(add(p.at, mul(s.right, h.at.x)), mul(s.up, h.at.y)), mul(s.forward, -h.at.z));
        o.spin = -h.turn.x / kDeg;
        o.spin_rate = -h.spun / kDeg * 60;
        o.steer = -h.turn.y / kDeg;
        o.on_ground = true;
    }
    return s;
}

} // namespace gt4r::photo::film
