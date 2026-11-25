#include "gt4r/race/pit_scene.h"

#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;
using render::Mat4;

constexpr float kPi = 3.14159265f;
// The program's own rounded headings.
constexpr float kFromLeft = 1.57075f, kFromRight = 4.71224976f;
constexpr float kScale = 0.939999998f; // the crew's size (0x3D1BE0)
constexpr float kHub = 0.459999949f;   // the height the motions have a wheel's middle at

const char* const kSequences[PitScene::kMen] = {"tiremanLF_1seq", "tiremanRF_1seq", "tiremanLR_1seq", "tiremanRR_1seq", "fuelman_1seq", "lollipopman_1seq"};

} // namespace

PitScene::PitScene(render::GpuContext* ctx, ByteSpan file) : ctx_(ctx) {
    // Sections (u32 offsets from 0x04): 1 the crew's motions, 2 the cameras', 3 the crew, 4 their shadow.
    if (file.size() < 0x14) throw FormatError("pit file too short");
    const u32 motions = rd32(file, 4), crew = rd32(file, 12);
    if (motions >= file.size() || crew >= file.size()) throw FormatError("pit file: section out of range");
    std::map<std::string, menu::SkeletonMotion::Name> names;
    motions_ = menu::SkeletonMotion::parse_all(file.subspan(motions), names);
    for (int k = 0; k < kMen; ++k) {
        const auto it = names.find(kSequences[k]);
        if (it == names.end() || it->second.sequence < 0 || size_t(it->second.object) >= motions_.size()) throw FormatError("pit file: a crew's motion is missing");
        seq_[k] = {it->second.object, it->second.sequence};
    }
    // When a wheel is off its hub and on it again (0x3CEB90: the named entries' starts and the program's own times).
    static constexpr float kRelease[4] = {0.716666639f, 1.26666665f, 0.399999976f, 0.48333332f};
    static constexpr float kFix[4] = {1.89999998f, 2.43333316f, 2.16666675f, 2.3499999f};
    for (int k = 0; k < 4; ++k) {
        const auto& q = motions_[size_t(seq_[k].object)].sequences[size_t(seq_[k].sequence)];
        release_[0][k] = release_[1][k] = fix_[0][k] = fix_[1][k] = -1;
        for (const auto& e : q.entries) {
            if (e.name == "release") release_[0][k] = e.start + kRelease[k], release_[1][k] = release_[0][k] + 0.166666657f;
            if (e.name == "fix") fix_[0][k] = e.start + kFix[k], fix_[1][k] = fix_[0][k] + 0.166666657f;
        }
        const auto nodes = motions_[size_t(seq_[k].object)].nodes(motions_[size_t(seq_[k].object)].pose(seq_[k].sequence, 0, 0), Mat4::identity());
        if (const auto it = nodes.find("tire1"); it != nodes.end()) tire1_z_[k] = it->second.m[14];
    }
    const ByteSpan set = file.subspan(crew);
    if (!mdls::is_mdls(set)) throw FormatError("pit file without its crew");
    bones_ = mdls::read_bones(set);
    symbols_ = mdls::read_symbols(set);
    skin_ = mdls::SkinTable::of(set);
    for (const mdls::Symbol& s : symbols_)
        if (s.name == "SKINNING") skinned_ = int(s.value);
    const u32 count = mdls::read_header(set).models;
    models_.resize(count);
    for (u32 i = 0; i < count && ctx; ++i) {
        gt3r::gtm1::Options o;
        o.node = int(i);
        o.program_alpha_test = true;
        o.gs_mip_levels = true;
        scene::Model m = mdls::build_model(set, o, mdls::Scene{}, "pit crew");
        bool any = false;
        for (const scene::Mesh& mesh : m.meshes) any |= !mesh.indices.empty();
        if (!any) continue;
        if (int(i) == skinned_) stored_ = m;
        models_[i] = std::make_unique<render::GpuModel>(*ctx, m);
    }
}

void PitScene::read_car_file(ByteSpan file, CarSizes& s) {
    const car::CarFile car = car::CarFile::parse(file);
    if (!car.info || car.info + 0x58 > file.size()) throw FormatError("car file without an info block");
    const u32 parts = rd32(file, car.info + 0x50);
    if (!parts || parts + 0xC4 > file.size()) throw FormatError("car file without a parts block");
    const auto v = [&](size_t at) {
        float f[3];
        std::memcpy(f, file.data() + at, 12);
        return render::Vec3{f[0], f[1], f[2]};
    };
    s.flags = rd16(file, car.info);
    s.filler = v(parts + 0xAC);
    s.filler_axis = v(parts + 0xB8);
}

void PitScene::place_car(Car& out, const CarVisual& visual, const race::CarState& s) {
    out.body = visual.body_matrix(s);
    out.height = s.position.y;
    const render::Vec3 at{out.body.m[12], out.body.m[13], out.body.m[14]};
    for (int w = 0; w < 4; ++w) {
        const auto& c = s.wheels[w].center;
        const render::Vec3 d{c.x - at.x, c.y - at.y, c.z - at.z};
        out.hub[w] = {render::dot(d, {out.body.m[0], out.body.m[1], out.body.m[2]}), render::dot(d, {out.body.m[4], out.body.m[5], out.body.m[6]}),
                      render::dot(d, {out.body.m[8], out.body.m[9], out.body.m[10]})};
        out.steer[w] = s.wheels[w].steer, out.spin[w] = s.wheels[w].spin;
    }
}

void PitScene::set_car(size_t i, const CarSizes& s) {
    if (cars_.size() <= i) cars_.resize(i + 1);
    cars_[i].sizes = s;
}

void PitScene::reset() {
    for (Stop& c : cars_) {
        const CarSizes s = c.sizes;
        c = {};
        c.sizes = s;
    }
    hose_ = false;
}

float PitScene::random(float lo, float hi) {
    seed_ = seed_ * 0x900D309u + 1;
    return lo + (hi - lo) * (float(seed_ >> 8) / 16777216.0f);
}

// Where the motion is sent (0x3CF6F0, 0x3CF730, 0x3CF780).
int PitScene::number(const Stop& c, int k) const {
    const int s = c.stage;
    if (s <= 0 || s >= 6) return -1;
    if (k < 4) return s == 1 ? 0 : s == 2 ? 1 : 2;
    if (k == 4) {
        if (s < 3) return 0;
        if (s == 3) return c.now.refuelling ? 1 : c.fuelled ? 2 : 0;
        return 2;
    }
    if (s == 1) return 0;
    bool done = true;
    for (int w = 0; w < 4; ++w) done &= c.men[size_t(w)].idle || c.men[size_t(w)].done;
    if (done && s < 5) return 2;
    return s < 4 ? 1 : 3;
}

// The men at their wheels (0x3CDFD8 → 0x3CD620).
void PitScene::working_places(Stop& c) {
    for (int k = 0; k < 4; ++k) {
        const float half = c.sizes.tire_width[k / 2] * 0.5f;
        Man& m = c.men[size_t(k)];
        m.pos = {k & 1 ? c.now.hub[k].x + half : c.now.hub[k].x - half, 0, c.now.hub[k].z};
        m.heading = k & 1 ? kFromRight : kFromLeft;
    }
}

// Beside the lane until the car is in its box (0x3CDB20).
void PitScene::waiting_places(Stop& c) {
    working_places(c);
    auto& m = c.men;
    m[0].pos.x = m[2].pos.x = std::min(m[0].pos.x, m[2].pos.x);
    m[1].pos.x = m[3].pos.x = std::max(m[1].pos.x, m[3].pos.x);
    if (c.sizes.flags & 0x10) { // (the file's pit on the right)
        m[0].pos.x -= 1;
        m[1].pos.x += random(0.1f, 0.25f);
        m[2].pos.x -= 1;
        m[3].pos.x += random(0.6f, 0.7f);
    } else {
        m[0].pos.x -= random(0.1f, 0.25f);
        m[1].pos.x += random(0.1f, 0.25f);
        m[2].pos.x -= random(0.6f, 0.7f);
        m[3].pos.x += random(0.6f, 0.7f);
    }
}

// The man with the fuel beside the filler (0x3CE4A0), further out until the car is in its box (0x3CE930).
void PitScene::fuel_place(Stop& c, bool waiting) {
    Man& m = c.men[4];
    const CarSizes& s = c.sizes;
    const float nx = -s.filler_axis.x, nz = -s.filler_axis.z, px = s.filler_axis.x, pz = s.filler_axis.z;
    const float most = std::max(std::max(nx, nz), std::max(px, pz));
    const int side = most == nx ? 0 : most == nz ? 1 : most == px ? 2 : 3;
    const float out = random(1.0999999f, 1.19999993f);
    constexpr float kClear = 0.449999988f, kAside = 0.49999997f;
    if (side == 0 || side == 2) {
        // Beside the car, clear of the men at its wheels.
        const int front = side == 0 ? 1 : 0, rear = side == 0 ? 3 : 2;
        const float zf = c.men[size_t(front)].pos.z, zr = c.men[size_t(rear)].pos.z;
        float z = s.filler.z;
        const auto aside = [&](float at) {
            if (std::fabs(z - at) < kClear) z = at < z ? at + kAside : at - kAside;
        };
        aside(side == 0 ? zf : zf);
        aside(zr);
        if (std::fabs(z - zf) < kClear) z = (zf + zr) * 0.5f;
        const float turn = std::clamp(std::atan2(z - s.filler.z, 0.1f), -0.523583293f, 0.523583293f);
        if (side == 0) {
            m.pos = {s.width * 0.5f + 0.1f, 0, z};
            m.heading = kFromRight - turn;
            if (waiting) m.pos.x = m.pos.x + 0.1f + out;
        } else {
            m.pos = {-s.width * 0.5f - 0.1f, 0, z};
            m.heading = turn + kFromLeft;
            if (waiting) m.pos.x = m.pos.x - 0.1f - out;
        }
    } else if (side == 1) {
        m.pos = {s.filler.x, 0, s.rear + 0.1f};
        m.heading = 3.1415f;
        if (waiting) {
            m.pos = {c.now.hub[3].x + 0.1f + out, 0, c.now.hub[3].z + 0.75f};
            m.heading = kFromRight;
        }
    } else {
        if (waiting) {
            m.pos = {s.filler.x, 0, -0.1f - s.front - out};
            m.heading = 0;
        }
    }
}

// The scene's state of a box (0x3D0DF0).
void PitScene::place(Stop& c, int stage) {
    const Car& n = c.now;
    const render::Vec3 at{n.body.m[12], n.body.m[13], n.body.m[14]};
    if (c.phase == 0 && stage == 0) {
        c.phase = 1;
        for (float& s : c.spin) s = 0;
    }
    if (c.phase == 1) {
        if (!n.has_box) return;
        c.place = n.box;
        c.heading = n.box_heading;
        waiting_places(c);
        fuel_place(c, true);
        // The man with the sign, ahead of the car (0x3CEB90; the file's pit on the right).
        c.men[5].pos = {std::max(c.sizes.width * 0.5f, 1.70799994f), 0, -1 - c.sizes.front};
        c.men[5].heading = 0;
        c.phase = 2;
    } else if (c.phase == 2) {
        if (stage < 2) return;
        c.place.x = at.x, c.place.z = at.z;
        for (int w = 0; w < 4; ++w) c.spin[w] = n.spin[w];
        working_places(c);
        fuel_place(c, false);
        c.phase = 4;
    } else if (c.phase == 4) {
        if (stage < 4 && stage > 0) {
            c.place.x = at.x, c.place.z = at.z;
            c.parted = n.body;
            for (int w = 0; w < 4; ++w) c.parted_hub[w] = n.hub[w];
        } else {
            c.phase = 5;
        }
    } else if (c.phase == 5) {
        c.phase = 0;
    }
}

menu::SkeletonMotion::Pose PitScene::pose_of(const Stop& c, int k) const {
    // The tires' men take a wheel of the car's size: between the motion's two for 827 mm and 456 mm across (0x3CC980).
    float mix = 0;
    if (k < 4) mix = std::clamp(1.0f - ((c.sizes.tire_radius[k / 2] * 2 - 0.45599997f) / 0.370999992f), 0.0f, 1.0f);
    return motions_[size_t(seq_[k].object)].pose(seq_[k].sequence, c.men[size_t(k)].time, mix);
}

void PitScene::step(std::span<const Car> cars, int shown, float dt, const std::function<void(const char*)>& sound) {
    hose_ = false;
    for (size_t i = 0; i < cars.size() && i < cars_.size(); ++i) {
        Stop& c = cars_[i];
        c.now = cars[i];
        // (The box's place and heading are read once, a car on the lane needs no more.)
        c.was = c.stage;
        c.stage = c.now.stage;
        if (c.stage != c.was)
            for (Man& m : c.men) m.idle = m.idle_next;
        place(c, c.stage);
        // Nobody at a wheel that is not changed, nobody with the fuel when none is wanted, nobody at all round a car on
        // its way out (0x35BD10); they go when the stage changes.
        if (c.stage == 1 && c.now.kind >= 2 && c.now.kind <= 4) {
            bool front = false, rear = false, fuel = false;
            if (c.now.kind != 4) {
                const u16 v = c.now.service;
                if (v & 0x11) {
                    front = rear = (v & 6) != 0;
                    fuel = (v & 10) != 0;
                } else {
                    front = (v & 0x40) != 0;
                    rear = (v & 0x80) != 0;
                    fuel = (v & 0x100) != 0;
                }
            }
            if (!front) c.men[0].idle_next = c.men[1].idle_next = true;
            if (!rear) c.men[2].idle_next = c.men[3].idle_next = true;
            if (!fuel) c.men[4].idle_next = true;
            if (c.now.kind == 4) c.men[5].idle_next = true;
        }
        for (int k = 0; k < kMen; ++k) {
            Man& m = c.men[size_t(k)];
            const int want = number(c, k);
            const auto& motion = motions_[size_t(seq_[k].object)];
            const auto& q = motion.sequences[size_t(seq_[k].sequence)];
            if (k == 4 && c.now.refuelling) c.fuelled = true;
            if (want < 0) {
                m.time = 0;
                m.idle_next = false;
                m.done = false;
                if (k == 4) c.fuelled = false;
                if (k < 4) c.turn_out[k] = 1, c.turn_in[k] = 0;
                continue;
            }
            if (k < 4 && !m.done) {
                const auto* e = q.at(m.time);
                for (const auto& x : q.entries)
                    if (&x < e && x.name == "tire_end") m.done = true;
            }
            bool move = true;
            if (k == 4) {
                // The nozzle goes as far as the filler is low, waits there and comes back the way it went (0x3CFA98).
                const auto* e = q.at(m.time);
                if (e && e->name == "refuel") {
                    // (The car's height is the course's own, not over the road.)
                    const float height = c.now.height + c.sizes.filler.y;
                    const float part = std::clamp((height - 0.611999989f) / 0.289999962f, 0.00999999978f, 0.98999995f);
                    const float end = e->start + e->duration;
                    if (want < 2) {
                        if (e->start + (end - e->start) * part < m.time) {
                            move = false;
                            if (int(i) == shown) hose_ = true;
                        }
                    } else {
                        m.time = m.time + (end - m.time) * 2 - dt;
                    }
                }
            }
            if (k < 4) {
                const float t = m.time;
                float &out = c.turn_out[k], &in = c.turn_in[k];
                if (t < release_[0][k]) out = 1, in = 0;
                else if (t < release_[1][k]) out = (release_[1][k] - t) / (release_[1][k] - release_[0][k]), in = 0;
                else if (t < fix_[0][k]) out = 0, in = 0;
                else if (t < fix_[1][k]) out = 0, in = 1 - (fix_[1][k] - t) / (fix_[1][k] - fix_[0][k]);
                else out = 0, in = 1;
            }
            const bool hose_changed = k == 4 && (want == 1) != c.was_refuelling;
            if (k == 4) c.was_refuelling = want == 1;
            // (The tank's own sound as the refuelling begins and ends.)
            if (hose_changed && !m.idle && int(i) == shown && sound) sound("oiltank");
            if (!move || m.idle) continue;
            const float before = m.time;
            m.time = q.advance(before, dt, want);
            if (int(i) != shown || !sound) continue;
            static constexpr const char* kChannels[4][2] = {
                {"wheel_gun", "wheelgun"}, {"wheel_nut", "dropnut"}, {"tire_sound2", "droptire"}, {"jack_up", "jackup"}};
            for (const auto& ch : kChannels)
                if (motion.events(seq_[k].sequence, ch[0], before, m.time, dt) > 0) sound(ch[1]);
        }
    }
}

// The frame a wheel stands in (0x3D1488, 0x3D2C48): `which` 0 the wheel that was on the car, 1 the new one.
bool PitScene::wheel_frame(const Stop& c, int k, int which, Mat4& out) const {
    const Man& m = c.men[size_t(k)];
    const int s = c.stage;
    bool hub_only = false;
    if (m.idle) {
        if (which != 1 || s != 3) return false;
        hub_only = true;
    } else if (s <= 0 || (which == 0 ? s < 3 : s >= 4)) {
        return false;
    }
    const auto yaw = [](const Mat4& b) { return std::atan2(b.m[8], b.m[10]); };
    const Mat4& body = c.now.body;
    const render::Vec3 hub = c.now.hub[k];
    Mat4 at;
    if ((which == 0 && s < 4) || (which == 1 && s >= 3)) {
        // At the hub of the car as it stands: its own turn for the wheel that was on it, the box's for the new one.
        Mat4 f = body;
        if (which == 1) f = render::translate({body.m[12], body.m[13], body.m[14]}) * render::rotate_y(c.heading - yaw(body)) *
                            render::translate({-body.m[12], -body.m[13], -body.m[14]}) * body;
        at = f * render::translate({hub.x, hub.y - kHub, hub.z});
    } else if (which == 1 && s == 2) {
        at = render::translate({body.m[12], c.place.y, body.m[14]}) * render::rotate_y(c.heading) * render::translate({hub.x, 0, hub.z});
    } else if (which == 0) {
        const render::Vec3 h = c.parted_hub[k];
        at = c.parted * render::translate({h.x, h.y - kHub, h.z});
    } else {
        at = render::translate(c.place) * render::rotate_y(c.heading) * render::translate({hub.x, 0, hub.z});
    }
    const float half = c.sizes.tire_width[k / 2] * 0.5f;
    Mat4 node = render::translate({0, kHub, 0});
    if (!hub_only) {
        const auto nodes = motions_[size_t(seq_[k].object)].nodes(pose_of(c, k), Mat4::identity());
        const auto it = nodes.find(which ? "tire1" : "tire0");
        if (it == nodes.end()) return false;
        node = it->second;
        // A wheel narrower than the motion's 259 mm keeps its outer face in the man's hands.
        if (which == 1 && tire1_z_[k] != 0) node.m[14] *= ((0.258999974f - c.sizes.tire_width[k / 2]) + tire1_z_[k]) / tire1_z_[k];
    }
    out = at * render::rotate_y(k & 1 ? -kPi / 2 : kPi / 2) * render::translate({0, 0, half}) * node * render::translate({0, 0, -half}) *
          render::rotate_y(-kPi / 2);
    // The hub's turn, while the wheel is on it.
    const float turn = hub_only ? 1.0f : which ? c.turn_in[k] : c.turn_out[k];
    if (turn > 0) out = out * render::rotate_y(-c.now.steer[k] * turn);
    const float spin = hub_only ? c.now.spin[k] : c.spin[k];
    out = out * render::rotate_x(k & 1 ? spin : -spin);
    return true;
}

void PitScene::add_items(std::vector<render::DrawItem>& items, std::vector<Wheel>& wheels, size_t car) {
    if (car >= cars_.size()) return;
    Stop& c = cars_[car];
    if (c.stage <= 0 || c.phase < 1) return;
    const Mat4 root = render::translate(c.place) * render::rotate_y(c.heading);
    for (int k = 0; k < kMen; ++k) {
        const Man& m = c.men[size_t(k)];
        if (number(c, k) < 0 || m.idle) continue;
        const Mat4 at = root * render::translate(m.pos) * render::rotate_y(m.heading) * render::scale({kScale, kScale, kScale});
        const auto nodes = motions_[size_t(seq_[k].object)].nodes(pose_of(c, k), Mat4::identity());
        // The models named for a node are drawn at it (the helmet, the wheel gun, the nozzle, the sign), the men by their bones.
        for (const mdls::Symbol& s : symbols_) {
            const auto it = nodes.find(s.name);
            if (it == nodes.end() || s.value >= models_.size() || !models_[s.value]) continue;
            items.push_back({models_[s.value].get(), at * it->second, 0});
        }
        if (skinned_ < 0 || !models_[size_t(skinned_)]) continue;
        std::vector<scene::Mesh> meshes;
        mdls::pose(stored_.meshes, skin_, menu::palette(bones_, nodes), meshes);
        std::unique_ptr<render::GpuModel>& g = posed_[size_t(k)];
        if (!g) g = std::make_unique<render::GpuModel>(*ctx_, *models_[size_t(skinned_)]);
        if (g->update_geometry(meshes)) items.push_back({g.get(), at, 0});
    }
    for (int k = 0; k < 4; ++k)
        for (int which = 0; which < 2; ++which) {
            Wheel w;
            w.wheel = k;
            if (wheel_frame(c, k, which, w.frame)) wheels.push_back(w);
        }
}

} // namespace gt4r::race
