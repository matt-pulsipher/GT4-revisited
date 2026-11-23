// gt4tool race: the race view's data, for checks from the command line.
//
//   gt4tool race <disc> sections <course>          the course file's sections
//   gt4tool race <disc> rail <course> [seconds]    runs the stand-in simulation on the course's road
//   gt4tool race <disc> models <course> <slot>     a slot's model set as the race view decodes it
//   gt4tool race <disc> script <course> [slot] [ms]  runs a slot's model scripts at a time: the registers they set
//   gt4tool race <disc> car <model> [paint] [n]    a race car's body (its model n) as decoded: materials, pictures, colors

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/model_script.h"
#include "gt4r/race/rail_simulation.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

using namespace gt4r;

namespace {

using Args = std::vector<std::string>;

Bytes read_course(Disc& disc, const std::string& name) {
    const std::string path = name.rfind("crs/", 0) == 0 ? name : "crs/" + name;
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    if (!e) throw FormatError("no file " + path);
    Bytes data = disc.volume().read(*e);
    if (!course::CourseFile::looks_like_course(data)) throw FormatError(path + " is not a course");
    return data;
}

int race_sections(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    for (const course::Section& s : file.sections()) std::printf("%2d  %-4s  0x%08x  %8u bytes\n", s.slot, s.tag.c_str(), s.offset, s.size);
    return 0;
}

int race_rail(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    const course::Runway road = course::Runway::parse(file.section(data, course::kSlotRunway));
    const float seconds = a.size() > 2 ? std::strtof(a[2].c_str(), nullptr) : 120.0f;
    std::vector<race::RailCar> specs(6);
    race::RailSimulation sim(road, specs);
    const race::RailLine& line = sim.line();
    std::printf("lap %.1f m, %zu line points, %zu grid places\n", road.length, line.points.size(), road.grid.size());
    if (!line.valid()) return 1;
    std::vector<race::CarInput> inputs(specs.size());
    float max_travel = 0, top = 0;
    std::vector<float> lap_start(specs.size(), -1);
    for (int i = 0; i < int(seconds * 60); ++i) {
        for (size_t c = 0; c < specs.size(); ++c) inputs[c] = race::follow_line(line, sim.cars()[c], c & 1 ? 2.0f : -2.0f, 1.0f - 0.04f * float(c));
        const int lap0 = sim.cars()[0].lap;
        sim.step(inputs, 1.0f / 60);
        const race::CarState& s = sim.cars()[0];
        if (s.lap != lap0) {
            if (lap_start[0] >= 0) std::printf("  lap %d: %.2f s\n", s.lap, float(i) / 60 - lap_start[0]);
            lap_start[0] = float(i) / 60;
        }
        top = std::max(top, s.speed);
        for (const race::CarState& c : sim.cars())
            for (const race::WheelState& w : c.wheels)
                if (std::fabs(w.travel) > max_travel) {
                    max_travel = std::fabs(w.travel);
                    if (max_travel > 0.25f) std::printf("  travel %.2f m at %.0f m along (t = %.1f s)\n", w.travel, c.course_v, float(i) / 60);
                }
        if (i % 600 == 0)
            std::printf("t %5.1f  v %7.1f m  lap %d  %5.1f km/h  gear %d  %4.0f rpm  at (%.1f, %.1f, %.1f)\n", float(i) / 60, s.course_v, s.lap,
                        s.speed * 3.6f, s.gear, s.engine_rpm, s.position.x, s.position.y, s.position.z);
    }
    std::printf("top speed %.1f km/h, largest suspension travel %.3f m\n", top * 3.6f, max_travel);
    return 0;
}

// A course slot's model set as the race view decodes it: per model, what it draws and how.
int race_models(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    const int slot = a.size() > 2 ? std::atoi(a[2].c_str()) : course::kSlotWorld;
    const ByteSpan s = file.section(data, slot);
    if (!mdls::is_mdls(s)) throw FormatError("the slot holds no model set");
    gt3r::gtm1::Options o;
    o.reflection = false, o.course_env_maps = true, o.alpha_test = true, o.gs_state = true;
    mdls::Scene sc;
    sc.color_scale = course::scenery_color_scale(file.section(data, course::kSlotEnv));
    const gt3r::scene::Model m = mdls::build_model(s, o, sc, a[1]);
    std::printf("%zu meshes, %zu textures, color scale %.2f\n", m.meshes.size(), m.textures.size(), sc.color_scale);
    const size_t limit = a.size() > 3 ? size_t(std::atoi(a[3].c_str())) : 40;
    for (size_t i = 0; i < m.meshes.size() && i < limit; ++i) {
        const gt3r::scene::Mesh& mesh = m.meshes[i];
        std::printf("model %d: %zu vertices\n", mesh.node, mesh.vertices.size());
        for (const gt3r::scene::Primitive& p : mesh.primitives) {
            double c[4] = {};
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (u32 k = 0; k < p.index_count; ++k) {
                const gt3r::scene::Vertex& v = mesh.vertices[mesh.indices[p.first_index + k]];
                for (int j = 0; j < 4; ++j) c[j] += double((v.color >> (8 * j)) & 0xFF);
                for (int j = 0; j < 3; ++j) lo[j] = std::min(lo[j], v.pos[j]), hi[j] = std::max(hi[j], v.pos[j]);
            }
            const double n = std::max<double>(1, p.index_count);
            std::string tex = "none";
            if (p.texture >= 0) {
                const gt3r::scene::Texture& t = m.textures[size_t(p.texture)];
                double alpha = 0;
                for (u32 px : t.rgba) alpha += double(px >> 24);
                char buf[64];
                std::snprintf(buf, sizeof buf, "%d (%ux%u, mean alpha %.0f)", p.texture, t.width, t.height, t.rgba.empty() ? 0.0 : alpha / double(t.rgba.size()));
                tex = buf;
            }
            std::printf("  %5u tris  flags %06x  alpha ref %.2f  texture %s  color %.0f %.0f %.0f %.0f  box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
                        p.index_count / 3, p.flags, p.alpha_ref, tex.c_str(), c[0] / n, c[1] / n, c[2] / n, c[3] / n, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        }
    }
    return 0;
}

// A race car's body (car/lod/<model>, model 0, the nearest level) with a paint put on.
int race_car(Disc& disc, const Args& a) {
    const gt3r::Volume::Entry* e = disc.volume().find("car/lod/" + a[1]);
    if (!e) throw FormatError("no file car/lod/" + a[1]);
    const Bytes file = disc.volume().read(*e);
    const car::CarFile c = car::CarFile::parse(file);
    Bytes pat_file;
    ByteSpan pat;
    if (c.body_paint) pat = ByteSpan(file).subspan(c.body_paint);
    else if (const gt3r::Volume::Entry* p = disc.volume().find("car/lod/" + a[1] + ".pat")) pat_file = disc.volume().read(*p), pat = pat_file;
    const u32 paint = a.size() > 2 ? u32(std::atoi(a[2].c_str())) : 0;
    std::printf("%u paints\n", car::paint_count(pat));
    const Bytes body = car::paint_count(pat) ? car::apply_paint(ByteSpan(file).subspan(c.body), pat, paint) : Bytes(file.begin() + c.body, file.end());
    const mdls::Header h = mdls::read_header(body);
    std::printf("body: %u models, %u shapes, %u materials, %u variations, %u texture sets x %u lists\n", h.models, h.shapes, h.materials, h.variations,
                h.texture_sets, h.texture_lists);
    for (u32 i = 0; i < h.materials && i < 12; ++i) {
        std::printf("  material %u:", i);
        for (int k = 0; k < 20; ++k) {
            float f;
            std::memcpy(&f, body.data() + h.material_table + 0x50 * size_t(i) + 4 * size_t(k), 4);
            std::printf(" %.2f", f);
        }
        std::printf("\n");
    }
    gt3r::gtm1::Options o;
    o.node = a.size() > 3 ? std::atoi(a[3].c_str()) : 0; // (1 and 2: the shadows)
    mdls::Scene sc;
    sc.resolve_callbacks = true;
    for (u16 id = 11; id <= 39; ++id) sc.callback_values[id] = 99;
    const gt3r::scene::Model m = mdls::build_model(body, o, sc, a[1]);
    for (const gt3r::scene::Mesh& mesh : m.meshes)
        for (const gt3r::scene::Primitive& p : mesh.primitives) {
            double c4[4] = {};
            for (u32 k = 0; k < p.index_count; ++k) {
                const gt3r::scene::Vertex& v = mesh.vertices[mesh.indices[p.first_index + k]];
                for (int j = 0; j < 4; ++j) c4[j] += double((v.color >> (8 * j)) & 0xFF);
            }
            const double n = std::max<double>(1, p.index_count);
            double t[4] = {};
            std::string size = "none";
            if (p.texture >= 0) {
                const gt3r::scene::Texture& tex = m.textures[size_t(p.texture)];
                for (u32 px : tex.rgba)
                    for (int j = 0; j < 4; ++j) t[j] += double((px >> (8 * j)) & 0xFF);
                for (double& x : t) x /= std::max<double>(1, double(tex.rgba.size()));
                size = std::to_string(tex.width) + "x" + std::to_string(tex.height);
            }
            std::printf("  %5u tris  flags %06x  texture %2d %-8s mean %3.0f %3.0f %3.0f %3.0f  vertex %3.0f %3.0f %3.0f %3.0f  material %s d %.2f %.2f %.2f a %.2f s %.2f sh %.0f\n",
                        p.index_count / 3, p.flags, p.texture, size.c_str(), t[0], t[1], t[2], t[3], c4[0] / n, c4[1] / n, c4[2] / n, c4[3] / n,
                        p.material.valid ? "yes" : "no ", p.material.diffuse[0], p.material.diffuse[1], p.material.diffuse[2], p.material.ambient[0],
                        p.material.specular[0], p.material.shininess);
        }
    return 0;
}

// A course slot's model scripts run once at a time of the race: the variables, and what each
// scripted model's registers come to.
int race_script(Disc& disc, const Args& a) {
    const Bytes data = read_course(disc, a[1]);
    const course::CourseFile file = course::CourseFile::parse(data);
    const int slot = a.size() > 2 ? std::atoi(a[2].c_str()) : course::kSlotWorld;
    const int ms = a.size() > 3 ? std::atoi(a[3].c_str()) : 1230;
    const mdls::Script script = mdls::Script::parse(file.section(data, slot));
    if (script.empty()) {
        std::printf("no script\n");
        return 0;
    }
    std::printf("%zu bytes of code, %u + %u cells, host:", script.code.size(), script.register_cells, script.second_cells);
    for (const mdls::Script::Variable& v : script.host) std::printf(" %s@%u", v.name.c_str(), v.cell);
    std::printf(", %zu named registers\n", script.registers.size());
    mdls::ScriptState state(script);
    state.set_host(script.host_cell("time"), u32(ms));
    state.set_host_f(script.host_cell("deltaT"), 1.0f / 60);
    size_t setup = 0, tick = 0, draw = 0, failed = 0;
    auto far = [](u32, std::span<const u32>) { return 1000.0f; }; // (no car near)
    for (const mdls::Script::Entries& e : script.models)
        if (e.setup >= 0) ++setup, failed += !mdls::run_script(script, e.setup, state, far);
    for (const mdls::Script::Entries& e : script.models)
        if (e.tick >= 0) ++tick, failed += !mdls::run_script(script, e.tick, state, far);
    for (size_t i = 0; i < script.models.size(); ++i) {
        const mdls::Script::Entries& e = script.models[i];
        if (e.draw < 0) continue;
        const std::vector<u32> before = state.registers;
        const bool ok = mdls::run_script(script, e.draw, state, far);
        failed += !ok;
        if (draw++ < 24) {
            std::printf("  model %zu:%s", i, ok ? "" : " (stopped)");
            for (size_t c = 0; c < state.registers.size(); ++c)
                if (state.registers[c] != before[c]) {
                    std::string name = "r" + std::to_string(c);
                    for (const mdls::Script::Variable& v : script.registers)
                        if (v.cell == c) name = v.name;
                    std::printf("  %s = %d / %g", name.c_str(), s32(state.registers[c]), state.register_f(u32(c)));
                }
            std::printf("\n");
        }
    }
    std::printf("%zu set-up, %zu tick and %zu draw scripts run, %zu stopped\n", setup, tick, draw, failed);
    return failed ? 1 : 0;
}

} // namespace

int cmd_race(Disc& disc, const Args& a) {
    if (a[0] == "sections") return race_sections(disc, a);
    if (a[0] == "rail") return race_rail(disc, a);
    if (a[0] == "models") return race_models(disc, a);
    if (a[0] == "car") return race_car(disc, a);
    if (a[0] == "script") return race_script(disc, a);
    std::fprintf(stderr, "race: unknown request %s\n", a[0].c_str());
    return 2;
}
