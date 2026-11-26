#include "gt4r/race/license_scene.h"

#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;
namespace render = gt3r::render;
using render::Mat4;
using render::Vec3;

constexpr float kPi = 3.14159265f;
constexpr float kHalfTurn = 3.1400001f;  // the program's own (the place's heading + 3.14, 0x408DB8; the men's turn in degrees, 0x408490)
constexpr float kScale = 0.939999998f;   // the men's size (0x409058)
constexpr float kWait = 2.0f, kFadeOut = 2.0f, kFadeIn = 1.0f; // 0x409258: state 0 for 2 s, 1 for 0x409878's 2 s, 2 for 0x409888's 1 s

const char* const kStand[LicenseScene::kMen] = {"license_stand_master", "license_stand_staff_1", "license_stand_staff_2"};
const char* const kSuccess[LicenseScene::kMen] = {"license_success_master", "license_success_staff_1", "license_success_staff_2"};
const char* const kFailed[LicenseScene::kMen] = {"license_failed_master", "license_failed_staff_1", "license_failed_staff_2"};
const char* const kCameras[3] = {"license_cam01_seq", "license_cam02_seq", "license_cam03_seq"};

Vec3 turned(const Vec3& p, float heading) { return render::rotate_y(heading).transform_point(p); }

} // namespace

void LicenseScene::read_car_file(ByteSpan file, CarSizes& s) {
    const car::CarFile car = car::CarFile::parse(file);
    if (!car.info || car.info + 0x58 > file.size()) throw FormatError("car file without an info block");
    const u32 parts = rd32(file, car.info + 0x50);
    if (!parts || parts + 0x58 > file.size()) throw FormatError("car file without a parts block");
    float seat[3], lever;
    std::memcpy(seat, file.data() + parts + 0x34, 12);
    std::memcpy(&lever, file.data() + parts + 0x4C, 4);
    s.seat_x = seat[0], s.seat_z = seat[2], s.lever_x = lever;
}

LicenseScene::LicenseScene(render::GpuContext* ctx, ByteSpan file) : ctx_(ctx) {
    // pit_001's layout (u32 offsets from 0x04): 1 the men's motions, 2 the cameras', 3 the men, 4 their shadow.
    if (file.size() < 0x14) throw FormatError("license scene file too short");
    const u32 motions = rd32(file, 4), cams = rd32(file, 8), men = rd32(file, 12), shadow = rd32(file, 16);
    if (motions >= file.size() || cams >= file.size() || men >= file.size()) throw FormatError("license scene file: section out of range");
    motions_ = menu::SkeletonMotion::parse_all(file.subspan(motions), names_);
    for (const char* const* list : {kStand, kSuccess, kFailed})
        for (int k = 0; k < kMen; ++k) {
            const auto it = names_.find(list[k]);
            if (it == names_.end() || it->second.sequence < 0 || size_t(it->second.object) >= motions_.size()) throw FormatError("license scene file: a man's motion is missing");
        }
    if (menu::SkeletonMotion::is_motion(file.subspan(cams))) cameras_ = menu::SkeletonMotion::parse_all(file.subspan(cams), camera_names_);
    build_set(file.subspan(men), crew_, "license staff");
    if (shadow && shadow < file.size() && mdls::is_mdls(file.subspan(shadow))) build_set(file.subspan(shadow), shadow_, "license staff's shadow");
}

void LicenseScene::build_set(ByteSpan set, Set& out, const char* name) {
    if (!mdls::is_mdls(set)) throw FormatError("license scene file without its men");
    out.bones = mdls::read_bones(set);
    out.symbols = mdls::read_symbols(set);
    out.skin = mdls::SkinTable::of(set);
    for (const mdls::Symbol& s : out.symbols)
        if (s.name == "SKINNING") out.skinned = int(s.value);
    const u32 count = mdls::read_header(set).models;
    out.models.resize(count);
    for (u32 i = 0; i < count && ctx_; ++i) {
        gt3r::gtm1::Options o;
        o.node = int(i);
        o.program_alpha_test = true;
        o.gs_mip_levels = true;
        scene::Model m = mdls::build_model(set, o, mdls::Scene{}, name);
        bool any = false;
        for (const scene::Mesh& mesh : m.meshes) any |= !mesh.indices.empty();
        if (!any) continue;
        if (int(i) == out.skinned) out.stored = m;
        out.models[i] = std::make_unique<render::GpuModel>(*ctx_, m);
    }
}

void LicenseScene::set_motion(int k, const char* name) {
    // (0x408268: the sequence by name, its clock from 0)
    const auto it = names_.find(name);
    Man& m = men_[size_t(k)];
    m.sequence = it == names_.end() ? -1 : it->second.sequence;
    m.object = it == names_.end() ? 0 : it->second.object;
    m.time = 0;
}

// The men round the car as it will stand at a place (0x408DB8 → 0x408B08): the master at the driver's door 0.5 m out, the first
// of his staff a metre behind him 0.75 m out, the other across the car by its front wheel 0.75 m out; each turned to the seat.
void LicenseScene::place_men(int index) {
    const course::Runway::Place& p = places_[size_t(index)];
    place_ = {p.pos.x, p.pos.y, p.pos.z};
    if (float y = 0; height_ && height_(place_.x, place_.z, y, nullptr)) place_.y = y; // (0x409B48: the road under it)
    heading_ = p.heading + kHalfTurn;
    // The driver's side: the seat right of the lever (0x408B08: virtual 2 against 18).
    const bool right = car_.seat_x >= car_.lever_x;
    const int near = right ? 1 : 0, far = right ? 0 : 1;
    const float out = right ? 1.0f : -1.0f;
    const Vec3 at[kMen] = {{car_.wheel_x[near] + out * 0.5f, 0, car_.seat_z},
                           {car_.wheel_x[near] + out * 0.75f, 0, car_.seat_z + 1.0f},
                           {car_.wheel_x[far] - out * 0.75f, 0, car_.wheel_z[far]}};
    for (int k = 0; k < kMen; ++k) {
        Man& m = men_[size_t(k)];
        const float turn = std::atan2(-(at[k].x - car_.seat_x), -(at[k].z - car_.seat_z)); // (0x57D118)
        // (0x407DE8: in the place's frame; 0x407EF0: on the road)
        m.pos = turned(at[k], heading_) + place_;
        m.ground = {0, 1, 0};
        if (float y = 0; height_ && height_(m.pos.x, m.pos.z, y, &m.ground)) m.pos.y = y;
        else m.pos.y = 0, m.ground = {0, 1, 0};
        m.heading = turn + heading_;
    }
}

void LicenseScene::start(const std::vector<course::Runway::Place>& places, const CarSizes& car, Height height) {
    places_ = places, car_ = car, height_ = std::move(height);
    on_ = !places_.empty();
    result_ = 0, state_ = was_ = 0, clock_ = 0;
    brightness_ = 1, fade_rate_ = 0;
    camera_sequence_ = -1, camera_time_ = 0;
    shot_on_ = shot_before_on_ = false;
    drawn_ = false;
    if (!on_) return;
    // With two places the men wait at the second through the run (and are drawn), else at the first (not drawn).
    drawn_ = places_.size() >= 2;
    place_men(drawn_ ? 1 : 0);
    for (int k = 0; k < kMen; ++k) set_motion(k, kStand[k]);
}

float LicenseScene::shadow_rise(const Vec3& n, const Vec3& shift) { return n.y == 0 ? 0.0f : -(n.x * shift.x + n.z * shift.z) / n.y; }

void LicenseScene::set_result(bool passed) {
    if (!on_ || result_ != 0) return;
    result_ = passed ? 1 : 2;
}

// A sequence's clock a step later at the number 1 (0x427800 → 0x429BC0): past every entry's number, so each plays through and
// the last goes round. ✔ The original's pass scene (15-states state_license_b1_pass_result: a staff man's clock 14.666 s 20.466 s
// into his 15.167 s sequence, its last entry of 1.933 s from 13.233 s gone round).
float LicenseScene::advance(const menu::SkeletonMotion::Sequence& q, float t, float dt) const {
    if (q.entries.empty() || q.length <= 0) return t + dt;
    const float next = t + dt;
    if (next < q.length) return q.advance(t, dt, 1);
    const auto& last = q.entries.back();
    if (last.duration <= 0) return last.start;
    return last.start + std::fmod(next - last.start, last.duration);
}

bool LicenseScene::step(float dt) {
    if (!on_) return false;
    // The men's clocks (0x4083B0), then the state (0x409258), then the camera's (0x4091E8).
    for (Man& m : men_) {
        if (m.sequence < 0 || size_t(m.object) >= motions_.size()) continue;
        const auto& seqs = motions_[size_t(m.object)].sequences;
        if (size_t(m.sequence) < seqs.size()) m.time = advance(seqs[size_t(m.sequence)], m.time, dt);
    }
    bool arrived = false;
    was_ = state_;
    if (result_ != 0) {
        if (state_ == 0) {
            clock_ += dt;
            if (kWait <= clock_) state_ = 1, clock_ = 0;
        } else if (state_ == 1) {
            clock_ += dt;
            if (kFadeOut <= clock_) {
                state_ = 2, clock_ = 0;
                place_men(0);
                drawn_ = true;
                for (int k = 0; k < kMen; ++k) set_motion(k, result_ == 1 ? kSuccess[k] : kFailed[k]);
                // The next of the three camera sequences, in turn from one test to the next.
                const auto it = camera_names_.find(kCameras[cameras_shown_ % 3]);
                camera_sequence_ = it == camera_names_.end() ? -1 : it->second.sequence;
                camera_time_ = 0;
                cameras_shown_ = (cameras_shown_ + 1) % 3;
                arrived = true;
            }
        } else if (state_ == 2) {
            clock_ += dt;
            if (kFadeIn <= clock_) state_ = 3, clock_ = 0;
        }
        // The race's picture (0x3BC6E0): fading out over 2 s as state 1 begins, in over 1 s as state 2 begins.
        if (state_ == 1 && was_ != 1) fade_rate_ = -1 / kFadeOut;
        if (state_ == 2 && was_ != 2) fade_rate_ = 1 / kFadeIn;
    }
    brightness_ = std::clamp(brightness_ + fade_rate_ * dt, 0.0f, 1.0f);
    step_camera(dt);
    return arrived;
}

// The scene's camera (0x4091E8, 0x4094C8): its sequence's first clip, eye, target and up in the place's frame.
void LicenseScene::step_camera(float dt) {
    shot_before_ = shot_, shot_before_on_ = shot_on_;
    shot_on_ = false;
    if (state_ < 2 || cameras_.empty() || camera_sequence_ < 0) return;
    const menu::SkeletonMotion& m = cameras_[0];
    if (size_t(camera_sequence_) >= m.sequences.size()) return;
    camera_time_ = advance(m.sequences[size_t(camera_sequence_)], camera_time_, dt);
    const auto pose = m.pose(camera_sequence_, camera_time_, 0);
    menu::SkeletonMotion::Camera cam;
    if (!m.camera(pose, cam)) return;
    const Vec3 eye = turned(cam.eye, heading_) + place_, target = turned(cam.target, heading_) + place_, up = turned(cam.up, heading_);
    shot_.eye = eye;
    shot_.view = render::look_at(eye, target, up);
    shot_.angle = cam.angle;
    shot_on_ = true;
}

bool LicenseScene::camera(CameraShot& out, bool before) const {
    out = before ? shot_before_ : shot_;
    return before ? shot_before_on_ : shot_on_;
}

void LicenseScene::add_items(std::vector<render::DrawItem>& items) {
    if (!on_ || !drawn_ || !ctx_) return;
    std::vector<scene::Mesh> meshes;
    for (int k = 0; k < kMen; ++k) {
        const Man& man = men_[size_t(k)];
        if (man.sequence < 0 || size_t(man.object) >= motions_.size()) continue;
        const menu::SkeletonMotion& motion = motions_[size_t(man.object)];
        // (0x408490: the turn in degrees as heading x 180 / 3.14)
        const Mat4 at = render::translate(man.pos) * render::rotate_y(man.heading * (kPi / kHalfTurn)) * render::scale({kScale, kScale, kScale});
        const auto nodes = motion.nodes(motion.pose(man.sequence, man.time, 0), Mat4::identity());
        const auto figure = [&](const Mat4& to, Set& set) {
            // The models named for a node (the helmet, the pen, the board) at it, the man by his bones.
            for (const mdls::Symbol& s : set.symbols) {
                const auto it = nodes.find(s.name);
                if (it == nodes.end() || s.value >= set.models.size() || !set.models[s.value]) continue;
                items.push_back({set.models[s.value].get(), to * it->second, 0});
            }
            if (set.skinned < 0 || size_t(set.skinned) >= set.models.size() || !set.models[size_t(set.skinned)]) return;
            mdls::pose(set.stored.meshes, set.skin, menu::palette(set.bones, nodes), meshes);
            std::unique_ptr<render::GpuModel>& g = set.posed[size_t(k)];
            if (!g) g = std::make_unique<render::GpuModel>(*ctx_, *set.models[size_t(set.skinned)]);
            if (g->update_geometry(meshes)) items.push_back({g.get(), to, 0});
        };
        if (shadows_ && shadow_.skinned >= 0) {
            // The shadow's figure thrown along the light, at most 1.5 m a metre, onto the road's plane under the man (0x408490,
            // 0x407FA0: the plane's rise along the shadow per metre of height).
            float sx = at.m[0] * shear_.x + at.m[2] * shear_.z, sz = at.m[8] * shear_.x + at.m[10] * shear_.z;
            if (const float l2 = sx * sx + sz * sz; l2 >= 2.25f) {
                const float by = 1.5f / std::sqrt(l2);
                sx *= by, sz *= by;
            }
            Mat4 flat;
            // (the shift a metre of the man's height makes, in course space)
            const Vec3 shift = Vec3{at.m[0] * sx + at.m[8] * sz, 0, at.m[2] * sx + at.m[10] * sz} * (1 / kScale);
            flat.m = {1, 0, 0, 0, sx, shadow_rise(man.ground, shift), sz, 0, 0, 0, 1, 0, 0, 0.01f, 0, 1};
            figure(at * flat, shadow_);
        }
        figure(at, crew_);
    }
}

} // namespace gt4r::race
