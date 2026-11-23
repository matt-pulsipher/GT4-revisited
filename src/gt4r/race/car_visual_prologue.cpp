// CarVisual for Gran Turismo 4 Prologue's cars: its car files (car/lod/<model>) are Gran Turismo
// 3's (GTCI, a GTM1 body, the GTTR tire picture, the GTTW wheel), decoded by the shared engine's
// reader and cut into the pieces the race view draws: the body, and each wheel with its tire in
// the frame Gran Turismo 4's wheels are drawn in (units of the rim's radius, a left wheel with its
// face at x = 0). docs/formats/prologue.md "Cars".

#include "gt4r/race/car_visual.h"

#include "formats/model/car_model.h"

#include <algorithm>
#include <cmath>

namespace gt4r::race {

namespace scene = gt3r::scene;

bool CarVisual::is_gt3_car(ByteSpan file) { return gt3r::CarFile::is_car(file); }

void CarVisual::build_gt3(ByteSpan file, const Options& opt, const std::string& name) {
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
        scene::Model all = gt3r::load_car_model(file, o, name);
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
                wheels_[size_t(w)][0] = std::make_unique<render::GpuModel>(*ctx_, wheels[size_t(w)]);
            }
        }
        body_[size_t(brake)] = std::make_unique<render::GpuModel>(*ctx_, body);
    }
}

} // namespace gt4r::race
