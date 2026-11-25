// CarVisual for Gran Turismo 4 Prologue's cars: its car files (car/lod/<model>) are Gran Turismo
// 3's (GTCI, a GTM1 body, the GTTR tire picture, the GTTW wheel), decoded by the shared engine's
// reader and cut into the pieces the race view draws: the body, and each wheel with its tire in
// the frame Gran Turismo 4's wheels are drawn in (units of the rim's radius, a left wheel with its
// face at x = 0); its shadows (the body's shape thrown along the light, the shadow under the car,
// the headlamps' light on the road: the model's nodes 1, 6 and 5, as the shared engine's race takes
// them) and its lamps (the info block's lists). docs/formats/prologue.md "Cars".

#include "gt4r/race/car_visual.h"

#include "formats/model/brake_model.h"
#include "formats/model/car_model.h"
#include "gt4r/race/car_pose.h"
#include "race/lamp_glows.h"
#include "race/wheel_fx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace scene = gt3r::scene;

namespace {

// One car (toyt0030, the "mtr_gmv_04") names its info block "CIF0" where the others have "GTCI";
// the engine's reader checks the name and reads nothing else of the block when it builds the model.
Bytes with_info_named(ByteSpan file) {
    Bytes b(file.begin(), file.end());
    if (b.size() >= 0x40 && rd32(b, 4) + 4 <= b.size() && std::memcmp(b.data() + rd32(b, 4), "CIF0", 4) == 0)
        std::memcpy(b.data() + rd32(b, 4), "GTCI", 4);
    return b;
}

} // namespace

bool CarVisual::is_gt3_car(ByteSpan file) { return gt3r::CarFile::is_car(file) || gt3r::CarFile::is_car(with_info_named(file)); }

void CarVisual::build_gt3(ByteSpan file_in, const Options& opt, const std::string& name) {
    const Bytes named = with_info_named(file_in);
    const ByteSpan file(named);
    gt3_file_ = true;
    try {
        lamps_ = gt3r::race::CarLampSet::parse(gt3r::CarFile::parse(file).info);
    } catch (const std::exception&) {
    }
    gt3r::WheelSpec ws;
    ws.wheelbase = spec_.wheelbase;
    for (int a = 0; a < 2; ++a) {
        ws.track[a] = 2 * spec_.half_track[a];
        ws.width[a] = spec_.tire_width[a];
        ws.radius[a] = spec_.tire_radius[a];
        // (The engine's rim is at R - 0.75 x the sidewall: the database's rim.)
        ws.sidewall[a] = std::max(0.01f, (spec_.tire_radius[a] - spec_.rim_radius[a]) / 0.75f);
    }
    static const char* const kWheel[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    for (int brake = 0; brake < 2; ++brake) {
        gt3r::CarModelOptions o;
        o.lod = -1;
        o.paint = int(opt.paint);
        o.resolve_paint = false;
        o.brake = brake != 0;
        o.wheel_spec = ws;
        o.effects = brake == 0; // (the shadows' nodes)
        scene::Model all = gt3r::load_car_model(file, o, name);
        if (brake == 0) {
            static const char* const kNodes[3] = {"node1", "node6", "node5"};
            for (size_t k = 0; k < 3; ++k) shadow_src_[k] = scene::extract(all, [k](const scene::Mesh& m) { return m.name == kNodes[k]; });
            std::erase_if(all.meshes, [](const scene::Mesh& m) { return m.name.rfind("node", 0) == 0; });
        }
        scene::Model body;
        body.name = name;
        body.textures = all.textures;
        std::array<scene::Model, 4> wheels;
        for (scene::Mesh& m : all.meshes) {
            int w = -1;
            for (int k = 0; k < 4; ++k)
                if (m.name.rfind(kWheel[k], 0) == 0) w = k;
            if (w < 0) {
                body.meshes.push_back(std::move(m));
                continue;
            }
            if (brake || m.name.find("_band") != std::string::npos) continue; // (the arch's shade does not turn)
            // Back to the hub's frame: the engine put the hub at (+-track / 2, R, -+wheelbase / 2)
            // and turned a right wheel half round about y.
            const int axle = w / 2;
            const bool right = (w & 1) != 0;
            const float cx = (right ? 0.5f : -0.5f) * ws.track[axle], cy = ws.radius[axle], cz = (axle == 0 ? -0.5f : 0.5f) * ws.wheelbase;
            const float r = spec_.rim_radius[axle], half = 0.5f * ws.width[axle];
            for (scene::Vertex& v : m.vertices) {
                float x = v.pos[0] - cx, y = v.pos[1] - cy, z = v.pos[2] - cz;
                if (right) x = -x, z = -z, v.normal[0] = -v.normal[0], v.normal[2] = -v.normal[2];
                v.pos[0] = (x + half) / r, v.pos[1] = y / r, v.pos[2] = z / r;
            }
            wheels[size_t(w)].meshes.push_back(std::move(m));
        }
        if (body.meshes.empty()) throw FormatError("The car's body has nothing to draw.");
        if (brake == 0) {
            body.compute_bounds();
            min_ = {body.bounds_min[0], body.bounds_min[1], body.bounds_min[2]};
            max_ = {body.bounds_max[0], body.bounds_max[1], body.bounds_max[2]};
            for (const scene::Mesh& mesh : body.meshes) lods_ = std::max(lods_, mesh.lod + 1);
            for (int w = 0; w < 4; ++w) {
                if (wheels[size_t(w)].meshes.empty()) continue;
                wheels[size_t(w)].name = name + "/wheel";
                wheels[size_t(w)].textures = all.textures;
                to_gpu(wheels_[size_t(w)][0], wheels[size_t(w)]);
            }
        }
        to_gpu(body_[size_t(brake)], body);
    }
    // The brake parts (race/brake.bin's rotor and caliper pictures by the info block's two records, and the rotor's
    // glow): the shared engine's, made in the hub's frame and moved to the wheel's frame here (its outer face).
    if (!opt.brake_parts.empty() && gt3r::BrakeParts::is_brake_bin(opt.brake_parts)) try {
            const gt3r::BrakeParts bin = gt3r::BrakeParts::parse(opt.brake_parts);
            const gt3r::CarFile f = gt3r::CarFile::parse(file);
            const std::array<gt3r::BrakeRecord, 2> records = gt3r::parse_brake_records(f.info);
            float shininess = 0;
            if (f.tire.size() >= 0x1C) std::memcpy(&shininess, f.tire.data() + 0x18, 4);
            for (int w = 0; w < 4; ++w) {
                const bool right = (w & 1) != 0;
                const float face = (right ? 0.5f : -0.5f) * ws.width[w / 2];
                auto to_wheel = [face](scene::Model m) {
                    for (scene::Mesh& mesh : m.meshes)
                        for (scene::Vertex& v : mesh.vertices) v.pos[0] -= face;
                    return m;
                };
                scene::Model parts = gt3r::build_brake_parts(bin, records[size_t(w / 2)], right, shininess);
                if (!parts.meshes.empty()) to_gpu(brakes_[size_t(w)], to_wheel(std::move(parts)));
                scene::Model glow = gt3r::build_brake_glow(bin, records[size_t(w / 2)], right);
                if (!glow.meshes.empty()) to_gpu(glows_[size_t(w)], to_wheel(std::move(glow)));
            }
        } catch (const std::exception&) {
        }
}

bool CarVisual::gt3_brakes_shown(const CarState& s, int w, const render::Vec3& eye, const LodView* view, bool& parts) const {
    // The wheel's level by its size on screen (0x412918: the projection's x scale x the wheel's bounding radius over
    // the hub's depth; 0 over 0.09, 1 over 0.03); the projection's x scale of the program's 4:3 picture is the view's
    // factor K x 0.75 x 0.7 / 224 (race_world.cpp). The parts while the eye is outboard of the outer face.
    const int axle = w / 2;
    const render::Mat4 frame = wheel_frame(s, spec_, w);
    if (view && view->k > 0) {
        const render::Vec3 hub{s.wheels[w].center.x, s.wheels[w].center.y, s.wheels[w].center.z};
        const float r = spec_.tire_radius[axle], half = 0.5f * spec_.tire_width[axle];
        if (gt3r::race::wheel_detail(view->k * (0.75f * 0.7f / 224.0f), std::sqrt(r * r + half * half), render::dot(hub - eye, view->forward)) >= 2)
            return false;
    }
    const float x = render::inverse(frame).transform_point(eye).x;
    parts = (w & 1) ? x > 0 : x < 0;
    return true;
}

} // namespace gt4r::race
