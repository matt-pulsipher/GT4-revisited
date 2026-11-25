#include "gt4r/app/menu_cars.h"

#include "base/log.h"
#include "gt4r/app/app.h"
#include "gt4r/app/menu_image_key.h"
#include "gt4r/app/menu_face3d.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/stage_env.h"
#include "gt4r/menu/stage_motion.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/car_spec.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/showroom.h"
#include "render/gpu_context.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <future>

namespace gt4r {

namespace render = gt3r::render;
using render::Mat4;
using render::Vec3;

namespace {

using menu_face3d::kDeg;
constexpr int kEnvSize = 128; // the stage's reflection picture (the program's is 256 x 256)

course::Vec3 cv(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 apply(const Mat4& m, const Vec3& v, float w) {
    return {m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z + m.at(0, 3) * w, m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z + m.at(1, 3) * w,
            m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z + m.at(2, 3) * w};
}

// The car standing in the model's frame `model`: its axes and its wheels at rest, so that the
// body is drawn with its origin (the middle of the wheelbase on its underside) at the frame's.
// `hang`: how far each axle's wheels hang below the body (the jack's lift).
race::CarState rest_state(const Mat4& model, const race::CarSpec& spec, const std::array<float, 2>& hang = {0, 0}) {
    race::CarState s;
    s.position = cv(apply(model, {0, 0, 0}, 1));
    s.right = cv(render::normalize(apply(model, {1, 0, 0}, 0)));
    s.up = cv(render::normalize(apply(model, {0, 1, 0}, 0)));
    s.forward = cv(render::normalize(apply(model, {0, 0, -1}, 0))); // the front is at -z
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        const float x = (w & 1) ? spec.half_track[axle] : -spec.half_track[axle];
        const float z = axle == 0 ? -spec.wheelbase / 2 : spec.wheelbase / 2;
        s.wheels[w].center = cv(apply(model, {x, spec.tire_radius[axle] - spec.ride_height[axle] - hang[size_t(axle)], z}, 1));
    }
    return s;
}

} // namespace

MenuCars::MenuCars(App& app) : app_(app) {
    if (app.gpu()) view_ = std::make_unique<render::View3D>(*app.gpu());
}

MenuCars::~MenuCars() {
    cars_.clear();
    view_.reset();
    mirror_view_.reset();
    stage_view_.reset();
    if (mirror_target_) SDL_DestroyTexture(mirror_target_);
    if (stage_target_) SDL_DestroyTexture(stage_target_);
    for (SDL_Texture* t : focus_targets_)
        if (t) SDL_DestroyTexture(t);
    if (app_.gpu())
    {
        for (auto& [k, t] : env_)
            if (t) app_.gpu()->release_texture(t);
        if (env_picture_) app_.gpu()->release_texture(env_picture_);
    }
}

void MenuCars::begin_frame(u64 frame) {
    frame_ = frame;
    for (auto& [w, f] : faces_) f.seen = false;
}

void MenuCars::end_frame() {
    for (auto it = faces_.begin(); it != faces_.end();)
        if (!it->second.seen || it->second.widget.expired()) it = faces_.erase(it);
        else ++it;
}

MenuCars::Car* MenuCars::car(const std::string& code, int color) {
    const std::string key = code + "#" + std::to_string(color);
    std::unique_ptr<Car>& c = cars_[key];
    if (c) {
        if (c->failed) return nullptr;
        if (c->loading.valid()) {
            // Built on a worker; sent to the GPU here, on the thread that draws, a few models a
            // picture (about 3 ms of each).
            if (c->loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
            try {
                c->building = c->loading.get();
            } catch (const std::exception& e) {
                LOG_WARN("Menu car %s: %s", code.c_str(), e.what());
                c->failed = true;
                return nullptr;
            }
        }
        if (c->building) {
            const Uint64 t0 = SDL_GetTicksNS();
            bool more = true;
            while (more && SDL_GetTicksNS() - t0 < 3000000) more = c->building->upload_one();
            if (more) return nullptr;
            c->visual = std::move(c->building);
            LOG_INFO("Menu car %s ready %.1f ms after it was asked for", code.c_str(), double(SDL_GetTicksNS() - c->asked) / 1e6);
        }
        return c->visual ? c.get() : nullptr;
    }
    c = std::make_unique<Car>();
    c->asked = SDL_GetTicksNS();
    db::SpecDb* db = app_.db();
    if (!db || !app_.gpu()) return c->failed = true, nullptr;
    if (models_.empty())
        for (const db::CarEntry& e : db->cars()) models_[e.label] = e.model;
    const auto model = models_.find(code);
    try {
        // The files and the database here (their readers are this thread's); the models are
        // built on a worker so that the menus go on moving while a car loads.
        if (model == models_.end()) throw FormatError("no such car in the database");
        const std::string path = "car/menu/" + model->second;
        std::optional<Bytes> file = app_.vfs()->read(path);
        if (!file || !(car::CarFile::is_car(*file) || race::CarVisual::is_gt3_car(*file))) throw FormatError("no showroom model " + path); // (Prologue's: Gran Turismo 3's car files)
        std::optional<Bytes> pat = app_.vfs()->read(path + ".pat");
        c->spec = race::read_car_spec(*db, code);
        const std::vector<u32> paints = race::car_paints(*db, code);
        race::CarVisual::Options o;
        o.reflection_by_draw = true;
        const u32 pick = u32(std::max(color, 0));
        o.paint = paints.empty() ? pick : paints[pick % paints.size()];
        render::GpuContext* gpu = app_.gpu();
        c->loading = std::async(std::launch::async, [gpu, f = std::move(*file), p = pat ? std::move(*pat) : Bytes{}, spec = c->spec, o, code]() {
            return race::CarVisual::make_unuploaded(*gpu, f, p, spec, o, code);
        });
        LOG_INFO("Menu car %s asked for (%.1f ms on the drawing thread)", code.c_str(), double(SDL_GetTicksNS() - c->asked) / 1e6);
    } catch (const std::exception& e) {
        LOG_WARN("Menu car %s: %s", code.c_str(), e.what());
        c->failed = true;
    }
    return nullptr;
}

SDL_GPUTexture* MenuCars::env_texture(menu::Project* project, const std::string& page, const std::string& path) {
    const gt3r::Tex1::Image* img = project && !path.empty() ? project->image(page, path, true) : nullptr;
    if (!img || !img->width || !img->height || !app_.gpu()) return nullptr;
    SDL_GPUTexture*& t = env_[image_key(*img)];
    if (!t) {
        render::Uploader up(app_.gpu()->device());
        t = up.texture_rgba(img->width, img->height, reinterpret_cast<const u32*>(img->rgba.data()), false);
        up.submit();
    }
    return t;
}

void MenuCars::load_stage() {
    if (stage_tried_) return;
    stage_tried_ = true;
    try {
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/bg.mdl")) {
            // Its models are the backdrops the light motion cross-fades.
            const u32 count = f->size() > 0x18 ? rd16(*f, 0x16) : 0;
            for (u32 i = 0; i < count && i < 8; ++i) {
                mdls::Options mo;
                mo.model = int(i);
                gt3r::scene::Model bg = mdls::build_model(*f, mo, "bg" + std::to_string(i));
                // (Its program writes no depth: the backdrops share their places.)
                for (gt3r::scene::Mesh& mesh : bg.meshes)
                    for (gt3r::scene::Primitive& p : mesh.primitives) p.flags |= gt3r::scene::kNoDepthWrite;
                stage_.push_back(std::make_unique<render::GpuModel>(*app_.gpu(), bg));
            }
        }
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/tire.mdl")) {
            // A cylinder of radius 1 and length 1 about x, drawn black with the thrown shadows only.
            gt3r::scene::Model tire = mdls::build_model(*f, mdls::Options{}, "tire");
            for (gt3r::scene::Mesh& mesh : tire.meshes) {
                for (gt3r::scene::Vertex& v : mesh.vertices) v.color = 0xFF000000u;
                for (gt3r::scene::Primitive& p : mesh.primitives)
                    p.texture = -1, p.flags = gt3r::scene::kUnlit | gt3r::scene::kDoubleSided;
            }
            stage_tire_ = std::make_unique<render::GpuModel>(*app_.gpu(), tire);
        }
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/camera.mot"))
            camera_motion_ = std::make_unique<menu::StageMotion>(menu::StageMotion::parse(*f));
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/light.mot"))
            light_motion_ = std::make_unique<menu::StageMotion>(menu::StageMotion::parse(*f));
        Mat4 view, proj;
        Vec3 eye;
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/env.mdl"); f && stage_camera({}, view, proj, eye)) {
            std::array<std::array<float, 3>, 3> rows;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) rows[size_t(r)][size_t(c)] = view.at(r, c);
            // (From 0.8 m above the car's place: the stage's middle, where the pages that keep this stage put it.)
            env_loading_ = std::async(std::launch::async, [file = *f, rows] {
                std::vector<SphereMap> maps;
                const u32 count = file.size() > 0x18 ? rd16(file, 0x16) : 0;
                for (u32 i = 0; i < count && i < 8; ++i) {
                    mdls::Options mo;
                    mo.model = int(i);
                    maps.push_back(menu::sphere_map(mdls::build_model(file, mo, "env" + std::to_string(i)), {0, 0.8f, 0}, rows, kEnvSize));
                }
                return maps;
            });
        }
    } catch (const std::exception& e) {
        LOG_WARN("Car stage: %s", e.what());
    }
}

SDL_GPUTexture* MenuCars::stage_environment() {
    if (env_loading_.valid() && env_loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            env_maps_ = env_loading_.get();
        } catch (const std::exception& e) {
            LOG_WARN("Car stage's reflection: %s", e.what());
        }
    }
    if (env_maps_.empty() || !light_motion_) return nullptr;
    if (env_picture_ && frame_ - env_made_ < 4 && frame_ >= env_made_) return env_picture_;
    env_made_ = frame_;
    std::vector<u32> px(size_t(kEnvSize) * kEnvSize, 0xFF000000u);
    std::vector<float> w;
    for (size_t i = 0; i < env_maps_.size(); ++i)
        w.push_back(std::clamp(light_motion_->named_at("Alpha" + std::to_string(i), stage_time_, i == 0 ? 1.0f : 0.0f), 0.0f, 1.0f));
    for (size_t k = 0; k < px.size(); ++k) {
        u32 c = 0xFF000000u;
        for (int b = 0; b < 3; ++b) {
            float v = 0;
            for (size_t i = 0; i < env_maps_.size(); ++i) v += w[i] * env_maps_[i][k][size_t(b)];
            c |= u32(std::clamp(v, 0.0f, 255.0f)) << (8 * b);
        }
        px[k] = c;
    }
    render::Uploader up(app_.gpu()->device());
    if (!env_picture_) env_picture_ = up.texture_rgba(kEnvSize, kEnvSize, px.data(), false);
    else up.update_rgba(env_picture_, kEnvSize, kEnvSize, px.data(), false, true);
    up.submit();
    return env_picture_;
}

const menu::StageMotion* MenuCars::option_motion(menu::Project* project, const std::string& page, const std::string& path) {
    auto [it, fresh] = option_motions_.try_emplace(page + ":" + path);
    if (fresh && project) try {
            if (const std::optional<Bytes> f = project->entry(page, path); f && menu::StageMotion::is_motion(*f))
                it->second = std::make_unique<menu::StageMotion>(menu::StageMotion::parse(*f));
        } catch (const std::exception& e) {
            LOG_WARN("Car face motion %s: %s", path.c_str(), e.what());
        }
    return it->second && it->second->camera ? it->second.get() : nullptr;
}

// A skinned set's reflection passes (shapes of kind 2, ALPHA 0x58: Cd + Cs x Ad) add the set's own
// sphere-map picture weighted by the frame alpha its clothes' texture wrote there (pitmanFix.mdl's
// draw program: alpha test off, the base shape opaque). The weight is put into the pass's vertex
// colours: the texture's alpha where the base shape has the same vertex.
void weigh_reflections(gt3r::scene::Model& m) {
    struct Place {
        u32 w[3];
        bool operator<(const Place& o) const { return std::memcmp(w, o.w, sizeof w) < 0; }
    };
    for (gt3r::scene::Mesh& mesh : m.meshes) {
        std::map<Place, float> written;
        auto place = [](const gt3r::scene::Vertex& v) {
            Place p;
            std::memcpy(p.w, v.pos, sizeof p.w);
            return p;
        };
        for (const gt3r::scene::Primitive& p : mesh.primitives) {
            if ((p.flags & (gt3r::scene::kEnvironmentMap | gt3r::scene::kAlphaBlend)) || p.texture < 0) continue;
            const gt3r::scene::Texture& t = m.textures[size_t(p.texture)];
            if (!t.width || !t.height) continue;
            for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) {
                const gt3r::scene::Vertex& v = mesh.vertices[mesh.indices[i]];
                const int x = int(std::floor((v.uv[0] - std::floor(v.uv[0])) * float(t.width))), y = int(std::floor((v.uv[1] - std::floor(v.uv[1])) * float(t.height)));
                const u32 c = t.rgba[size_t(std::clamp(y, 0, int(t.height) - 1)) * t.width + size_t(std::clamp(x, 0, int(t.width) - 1))];
                written[place(v)] = float(c >> 24) / 255.0f;
            }
        }
        std::vector<bool> done(mesh.vertices.size(), false);
        for (const gt3r::scene::Primitive& p : mesh.primitives) {
            if (!(p.flags & gt3r::scene::kEnvironmentMap)) continue;
            for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) {
                const u32 k = mesh.indices[i];
                if (done[k]) continue;
                done[k] = true;
                gt3r::scene::Vertex& v = mesh.vertices[k];
                const auto it = written.find(place(v));
                const float w = it == written.end() ? 0.0f : it->second;
                u32 c = v.color & 0xFF000000u;
                for (int b = 0; b < 3; ++b) c |= u32(float((v.color >> (8 * b)) & 0xFF) * w + 0.5f) << (8 * b);
                v.color = c;
            }
        }
    }
}

const MenuCars::ModelSet* MenuCars::model_set(menu::Project* project, const std::string& page, const std::string& path) {
    auto [it, fresh] = model_sets_.try_emplace(page + ":" + path);
    if (fresh) {
        it->second = std::make_unique<ModelSet>();
        if (project && app_.gpu()) try {
                if (const std::optional<Bytes> f = project->entry(page, path); f && mdls::is_mdls(*f)) {
                    ModelSet& set = *it->second;
                    set.bones = mdls::read_bones(*f);
                    set.symbols = mdls::read_symbols(*f);
                    if (!set.bones.empty()) {
                        set.skin = mdls::SkinTable::of(*f);
                        for (const mdls::Symbol& sym : set.symbols)
                            if (sym.name == "SKINNING") set.skinned_model = int(sym.value);
                    }
                    const u32 count = rd16(*f, 0x16);
                    for (u32 i = 0; i < count && i < 16; ++i) {
                        gt3r::gtm1::Options mo;
                        mo.node = int(i);
                        mo.static_env_maps = !set.bones.empty(); // (the mechanic's own reflection picture)
                        gt3r::scene::Model m = mdls::build_model(*f, mo, mdls::Scene{}, path + "#" + std::to_string(i));
                        if (!set.bones.empty()) weigh_reflections(m);
                        // The page sees the face's picture through its alpha: an opaque
                        // primitive's texels are opaque there (the clothes' texture alpha weighs
                        // the reflection passes in the frame, not what shows).
                        if (!set.bones.empty()) {
                            std::vector<bool> opaque(m.textures.size(), false), blended(m.textures.size(), false);
                            for (const gt3r::scene::Mesh& mesh : m.meshes)
                                for (const gt3r::scene::Primitive& p : mesh.primitives)
                                    if (p.texture >= 0 && size_t(p.texture) < m.textures.size() && p.index_count)
                                        ((p.flags & (gt3r::scene::kAlphaBlend | gt3r::scene::kAlphaTest | gt3r::scene::kAdditive)) ? blended : opaque)[size_t(p.texture)] = true;
                            for (size_t t = 0; t < m.textures.size(); ++t)
                                if (opaque[t] && !blended[t])
                                    for (u32& c : m.textures[t].rgba) c |= 0xFF000000u;
                        }
                        set.models.push_back(std::make_unique<render::GpuModel>(*app_.gpu(), m));
                        set.stored.push_back(set.bones.empty() ? gt3r::scene::Model{} : std::move(m));
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("Car face models %s: %s", path.c_str(), e.what());
            }
    }
    return it->second.get();
}

const menu::SkeletonMotion* MenuCars::skeleton_motion(menu::Project* project, const std::string& page, const std::string& path) {
    auto [it, fresh] = skeleton_motions_.try_emplace(page + ":" + path);
    if (fresh && project) try {
            if (const std::optional<Bytes> f = project->entry(page, path); f && menu::SkeletonMotion::is_motion(*f))
                it->second = std::make_unique<menu::SkeletonMotion>(menu::SkeletonMotion::parse(*f));
        } catch (const std::exception& e) {
            LOG_WARN("Car face motion %s: %s", path.c_str(), e.what());
        }
    return it->second.get();
}

struct MenuCars::Sim {
    sim::Program program;
    sim::Showroom showroom;
    Sim(db::SpecDb& db, Disc& disc) : program(sim::Program::load(disc)), showroom(db, program) {}
};

std::array<float, 2> MenuCars::wheel_droop(const std::string& code) {
    auto [it, fresh] = droops_.try_emplace(code, std::array<float, 2>{0, 0});
    if (!fresh) return it->second;
    try {
        db::SpecDb* db = app_.db();
        if (!db || !app_.disc()) return it->second;
        if (!sim_) sim_ = std::make_unique<Sim>(*db, *app_.disc());
        sim::Equipment eq;
        if (!sim::stock_equipment(*db, code, eq)) return it->second;
        if (const auto d = sim_->showroom.droop(eq))
            for (size_t a = 0; a < 2; ++a) it->second[a] = std::max((*d)[a].host(), 0.0f);
    } catch (const std::exception& e) {
        LOG_WARN("Menu car %s: wheel droop: %s", code.c_str(), e.what());
    }
    LOG_INFO("Menu car %s: wheels hang %.3f / %.3f m on the jack", code.c_str(), double(it->second[0]), double(it->second[1]));
    return it->second;
}

std::array<float, 2> MenuCars::body_length(const std::string& code, const race::CarSpec& spec) {
    auto [it, fresh] = body_lengths_.try_emplace(code, std::array<float, 2>{spec.wheelbase / 2 + 0.9f, spec.wheelbase / 2 + 0.9f});
    if (!fresh) return it->second;
    // The body's box of the car's first paint row (VARIATION +0x18 / +0x1C, mm): how far it
    // reaches ahead of and behind its middle.
    if (db::SpecDb* db = app_.db()) {
        const db::Labels* cars = db->labels("GENERIC_CAR");
        const db::Table* link = db->table("CAR_VARIATION_american");
        const db::Table* rows = db->table("VARIATIONamerican");
        const std::optional<u32> id = cars ? cars->id(code) : std::nullopt;
        if (id && link && rows) {
            const ByteSpan l = link->row(*id);
            size_t first = 0;
            if (l.size() >= 4 && rows->rows(rd32(l, 0), first) > 0) {
                const ByteSpan r = rows->row_at(first);
                float ahead = 0, behind = 0;
                if (r.size() >= 0x20) std::memcpy(&ahead, r.data() + 0x18, 4), std::memcpy(&behind, r.data() + 0x1C, 4);
                if (ahead > 500 && ahead < 5000 && behind > 500 && behind < 5000) it->second = {ahead / 1000, behind / 1000};
            }
        }
    }
    return it->second;
}

void MenuCars::add_model_motions(Face& f, const menu::CarFaceSettings& s, const menu::CarMotion& m, menu::Project* project, const std::string& page,
                                 const Mat4& car, const race::CarSpec& spec, float fade, std::vector<render::DrawItem>& out) {
    std::vector<const ModelSet*> sets;
    for (const std::string& path : s.model_sets) sets.push_back(model_set(project, page, path));
    if (m.model_motions.empty()) {
        // Without motions each set is drawn as it is stored (garageShadow.mdl).
        for (const ModelSet* set : sets)
            if (set)
                for (const auto& g : set->models) out.push_back(render::DrawItem{g.get(), Mat4::identity(), 0});
        return;
    }
    // The places on the car the motions start from (0x156AA0, table 0x68FDC0), in the car's frame
    // (x to its right, the front at -z): the outer faces of the left tires at the axles, midway
    // and three quarters back, and the body's ends on its centre line.
    const float front = -spec.wheelbase / 2, rear = spec.wheelbase / 2, mid = (front + rear) / 2;
    const float left_front = -(spec.half_track[0] + spec.tire_width[0] / 2), left_rear = -(spec.half_track[1] + spec.tire_width[1] / 2);
    const std::array<float, 2> ends = body_length(s.car_code, spec);
    struct Place {
        float x, z, yaw;
    };
    const Place places[11] = {{0, 0, 0},
                              {left_front, front, 0},
                              {-left_front, front, 180},
                              {left_rear, rear, 0},
                              {-left_rear, rear, 180},
                              {left_front, mid, 0},
                              {-left_front, mid, 180},
                              {left_front, front + 0.75f * (rear - front), 0},
                              {-left_front, front + 0.75f * (rear - front), 180},
                              {0, mid - ends[0], -90},
                              {0, mid + ends[1], 90}};
    if (f.posed.size() < m.model_motions.size()) f.posed.resize(m.model_motions.size());
    for (size_t e = 0; e < m.model_motions.size(); ++e) {
        const menu::CarMotion::ModelMotion& mm = m.model_motions[e];
        const int index = mm.set >= 0 && size_t(mm.set) < sets.size() ? mm.set : 0;
        const ModelSet* set = index < int(sets.size()) ? sets[size_t(index)] : nullptr;
        const menu::SkeletonMotion* motion = skeleton_motion(project, page, mm.path);
        if (!set || set->models.empty() || !motion) continue;
        const Place& p = places[mm.align >= 0 && mm.align <= 10 ? mm.align : 0];
        const float k = m.model_scale;
        // The motion's start: the car's matrix, the place, turned, scaled (flattened onto the
        // floor for a shadow).
        const Mat4 start = car * render::translate({p.x, 0, p.z}) * render::rotate_y((mm.rot + p.yaw) * kDeg) * render::scale({k, mm.shadow ? 0.0f : k, k});
        const float t = m.clock.time(frame_, motion->length(), motion->loop_start());
        const std::map<std::string, Mat4> nodes = motion->nodes(t, start);
        // A symbol named as a node: that model drawn there (the oil change's cart, careerPos).
        for (const mdls::Symbol& sym : set->symbols) {
            const auto at = nodes.find(sym.name);
            if (at == nodes.end() || sym.value >= set->models.size()) continue;
            render::DrawItem it{set->models[sym.value].get(), at->second, 0};
            it.alpha = fade;
            out.push_back(it);
        }
        // The skinned model moved by the bones.
        if (set->skinned_model >= 0 && size_t(set->skinned_model) < set->stored.size() && !set->skin.empty()) {
            const mdls::Palette pal = menu::palette(set->bones, nodes);
            std::vector<gt3r::scene::Mesh> meshes;
            mdls::pose(set->stored[size_t(set->skinned_model)].meshes, set->skin, pal, meshes);
            std::unique_ptr<render::GpuModel>& g = f.posed[e];
            if (!g) g = std::make_unique<render::GpuModel>(*app_.gpu(), *set->models[size_t(set->skinned_model)]);
            if (g->update_geometry(meshes)) {
                render::DrawItem it{g.get(), Mat4::identity(), 0};
                it.sphere_map = true;
                it.alpha = fade;
                out.push_back(it);
            }
        }
    }
}

// The stage's time: 1/60 s a picture, round the light motion's length to its loop's start (0x157378).
void MenuCars::step_stage() {
    if (stage_stepped_ == 0 || stage_stepped_ > frame_) stage_stepped_ = frame_ ? frame_ - 1 : 0;
    stage_time_ += float(std::min<u64>(frame_ - stage_stepped_, 10)) / 60.0f;
    stage_stepped_ = frame_;
    const float len = light_motion_ ? light_motion_->length() : 0;
    if (len > 0 && stage_time_ > len) stage_time_ -= len - light_motion_->loop_start();
}

bool MenuCars::stage_camera(const menu::FaceView& v, Mat4& view, Mat4& proj, Vec3& eye) const {
    if (!camera_motion_ || !camera_motion_->camera) return false;
    // The stage's camera (menu/select/camera.mot at the stage's time): its eye, the point it looks at and its up; its field of
    // view across the page's height (0x1D5C00).
    const auto& c = *camera_motion_->camera;
    auto at_t = [&](int i) { return camera_motion_->at(c[size_t(i)], stage_time_); };
    eye = {at_t(0), at_t(1), at_t(2)};
    view = render::look_at(eye, {at_t(3), at_t(4), at_t(5)}, {at_t(6), at_t(7), at_t(8)});
    menu::FaceView sv = v;
    sv.view_angle = (camera_motion_->fov ? camera_motion_->at(*camera_motion_->fov, stage_time_) : 0.45f) / kDeg;
    sv.near = 0.1f, sv.far = 100;
    proj = menu_face3d::projection_of(sv, {0, 0, 640, 480}, {0, 0, 640, 480});
    return true;
}

// The backdrops averaged by their weights at the stage's time (light.mot's Alpha0 .. Alpha3), one over another at its part of
// the sum so far (0x1D5588), then faded to black by what the sum lacks of 1 (0x1D5620).
std::vector<render::DrawItem> MenuCars::backdrops() const {
    std::vector<render::DrawItem> out;
    if (!light_motion_) return out;
    std::vector<float> weights;
    float whole = 0, sum = 0;
    for (size_t i = 0; i < stage_.size(); ++i) {
        weights.push_back(std::clamp(light_motion_->named_at("Alpha" + std::to_string(i), stage_time_, i == 0 ? 1.0f : 0.0f), 0.0f, 1.0f));
        whole += weights[i];
    }
    for (size_t i = 0; i < stage_.size(); ++i)
        if (stage_[i] && weights[i] > 0) {
            sum += weights[i];
            if (weights[i] / sum < 1.0f / 128) continue;
            render::DrawItem it{stage_[i].get(), Mat4::identity(), -2};
            it.alpha = weights[i] / sum;
            it.tint = std::min(whole, 1.0f);
            it.in_order = true;
            out.push_back(it);
        }
    return out;
}

void MenuCars::draw_backdrops(const std::vector<render::DrawItem>& backdrops, const Mat4& view, const Mat4& proj, const Vec3& eye,
                              const SDL_FRect& page_rect, float opacity) {
    SDL_Renderer* r = app_.renderer();
    if (!stage_view_) stage_view_ = std::make_unique<render::View3D>(*app_.gpu());
    auto target = [r](SDL_Texture*& t, int w, int h) {
        if (t && (t->w != w || t->h != h)) SDL_DestroyTexture(t), t = nullptr;
        if (!t && (t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h))) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        return t != nullptr;
    };
    // The frame buffer's 640 x 448 reduced 2:1 (0x1D56B0's targets).
    constexpr int kW = 320, kH = 224;
    if (!target(stage_target_, std::max(1, int(page_rect.w + 0.5f)), std::max(1, int(page_rect.h + 0.5f))) || !target(focus_targets_[0], kW, kH) ||
        !target(focus_targets_[1], kW, kH))
        return;
    SDL_Texture* before = SDL_GetRenderTarget(r);
    render::DrawOptions o;
    o.clear[0] = o.clear[1] = o.clear[2] = 0, o.clear[3] = 1;
    o.repeated = true;
    o.sort_blended = true;
    o.gt3_lighting = true;
    SDL_SetRenderTarget(r, stage_target_);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    stage_view_->draw(r, SDL_FRect{0, 0, float(stage_target_->w), float(stage_target_->h)}, view, proj, eye, backdrops, o);
    // The soft focus: a copy at half the frame's size, a running average over 7 pixels across and then down
    // (weights exp(-j^2 / (2 x 2.5^2)), each shifted copy laid over the last at its part of the sum so far, in 128ths),
    // put over the picture at 0.6.
    auto over = [r](SDL_Texture* from, SDL_Texture* to, float dx, float dy, float alpha, bool first = false) {
        SDL_SetRenderTarget(r, to);
        SDL_SetTextureBlendMode(from, first ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaModFloat(from, std::floor(alpha * 128) / 128);
        const SDL_FRect src{std::max(-dx, 0.0f), std::max(-dy, 0.0f), kW - std::fabs(dx), kH - std::fabs(dy)};
        const SDL_FRect dst{std::max(dx, 0.0f), std::max(dy, 0.0f), src.w, src.h};
        if (first) SDL_RenderTexture(r, from, nullptr, nullptr);
        else SDL_RenderTexture(r, from, &src, &dst);
    };
    over(stage_target_, focus_targets_[0], 0, 0, 1, true);
    for (int pass = 0; pass < 2; ++pass) {
        SDL_Texture *from = focus_targets_[pass], *to = focus_targets_[1 - pass];
        float sum = 0;
        for (int j = 0; j < 7; ++j) { // 0 .. 3 to the right, then 1 .. 3 to the left
            const int step = j <= 3 ? j : -(j - 3);
            const float w = std::exp(-float(step * step) / (2 * 2.5f * 2.5f));
            sum += w;
            over(from, to, pass == 0 ? float(step) : 0.0f, pass == 0 ? 0.0f : float(step), w / sum, j == 0);
        }
    }
    SDL_SetRenderTarget(r, before);
    for (SDL_Texture* t : {stage_target_, focus_targets_[0]}) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaModFloat(t, t == stage_target_ ? opacity : opacity * 76.0f / 128);
        SDL_RenderTexture(r, t, nullptr, &page_rect);
    }
}

void MenuCars::draw(const menu::LiveWidget& face, menu::Project* project, const std::string& page, const menu::Rect& at, const SDL_FRect& dst, float opacity) {
    if (!view_) return;
    menu::CarFaceSettings s = menu::car_face_settings(face.data);
    if (s.car_code.empty()) return;
    if (s.advanced) {
        load_stage(), step_stage();
        if (stage_.empty()) s.advanced = false; // (a disc without the stage, Prologue's: its car faces know no such action)
    }
    Car* c = car(s.car_code, s.car_color);
    if (!c) {
        // (The stage stands there while a car loads.)
        Mat4 view, proj;
        Vec3 eye;
        if (s.advanced && !stage_.empty() && stage_camera(s.view, view, proj, eye))
            draw_backdrops(backdrops(), view, proj, eye, menu_face3d::on_screen({0, 0, 640, 480}, at, dst), opacity);
        return;
    }

    // The car model the scripts move (face.car_model), made when they first ask for it.
    menu::LiveWidget& live = const_cast<menu::LiveWidget&>(face);
    std::shared_ptr<menu::CarModelObject> model;
    if (const auto it = live.extras.find("car_model"); it != live.extras.end())
        if (auto cell = std::dynamic_pointer_cast<script::Variable>(it->second)) model = cell->value.as<menu::CarModelObject>();
    if (!model) {
        model = menu::make_car_model_object([this] { return frame_; });
        live.extras["car_model"] = std::make_shared<script::Variable>(menu::object_value(model));
    }
    Face& f = faces_[&face];
    const std::string key = s.car_code + "#" + std::to_string(s.car_color);
    if (model->m.placed_car != s.car_code) {
        // A car set on the face (by its code): placed where the layout says and faded in.
        model->m.set_car(s.trans, s.rotate);
        model->m.placed_car = s.car_code;
    }
    f.widget = live.weak_from_this();
    f.car = key;
    f.seen = true;
    menu::CarMotion& m = model->m;
    // One step a picture of the menus, however often the screen is drawn.
    if (f.stepped == 0 || f.stepped > frame_) f.stepped = frame_ ? frame_ - 1 : 0;
    for (int n = 0; f.stepped < frame_ && n < 10; ++n, ++f.stepped) m.step();
    f.stepped = frame_;

    // The camera's picture covers the whole page.
    const menu::Rect page_part{0, 0, 640, 480};
    const SDL_FRect page_rect = menu_face3d::on_screen(page_part, at, dst);
    const menu::FaceView& v = s.view;
    Mat4 proj = menu_face3d::projection_of(v, at, page_part);
    Mat4 view = menu_face3d::view_of(v);
    Vec3 eye{v.camera_trans[0], v.camera_trans[1], v.camera_trans[2]};
    // The "advanced" stage takes the car at (x, y, -z) turned half round (0x1D5B88).
    const float z = s.advanced ? -m.cur[2] : m.cur[2];
    const float yaw = s.advanced ? m.yaw + 180 : m.yaw;
    if (s.advanced) stage_camera(v, view, proj, eye);
    const menu::StageMotion* om = s.advanced || s.option_motion.empty() ? nullptr : option_motion(project, page, s.option_motion);
    // The page's camera motion (loadModelOptionMotion) at its clock's time (0 until the page plays
    // it): its eye, the point looked at, its up and field of view; and its named values: `fade`
    // the opacity of the model sets (not drawn under 0.02), `lift` the car raised on a jack,
    // `shadowOpacity` the car's shadow (0x156F18).
    float fade = 1, lift = 0, shadow_opacity = -1;
    if (om) {
        const float t = m.clock.time(frame_, om->length(), m.option_loop_start);
        fade = std::clamp(om->named_at("fade", t, 1.0f), 0.0f, 1.0f);
        lift = std::max(om->named_at("lift", t, 0.0f), 0.0f);
        if (om->named.count("shadowOpacity")) shadow_opacity = std::clamp(om->named_at("shadowOpacity", t, 1.0f), 0.0f, 1.0f);
        const auto& c = *om->camera;
        auto at_t = [&](int i) { return om->at(c[size_t(i)], t); };
        eye = {at_t(0), at_t(1), at_t(2)};
        view = render::look_at(eye, {at_t(3), at_t(4), at_t(5)}, {at_t(6), at_t(7), at_t(8)});
        menu::FaceView sv = v;
        if (om->fov) sv.view_angle = om->at(*om->fov, t) / kDeg;
        sv.far = std::max(sv.far, 100.0f);
        proj = menu_face3d::projection_of(sv, at, page_part);
    }
    const Mat4 placed = render::translate({m.cur[0], m.cur[1], z}) * render::rotate_z(m.cur_rz * kDeg) * render::rotate_x(m.cur_rx * kDeg) *
                        render::rotate_y(yaw * kDeg);
    // The model's origin is on the body's underside: the tires stand on y = 0 with the body
    // raised by its ride height (❓ judged from the pictures: the program's ShowRoomCar is not read).
    const float ride = (c->spec.ride_height[0] + c->spec.ride_height[1]) / 2;
    // On a jack (`lift`) the body is raised and each wheel hangs below it by the smaller of the lift
    // and its axle's travel (0x156950; the showroom car's suspension, 0x364B30): the wheels stay on
    // the ground until the lift exceeds their travel.
    std::array<float, 2> hang{0, 0};
    if (lift > 0) {
        const std::array<float, 2> travel = wheel_droop(s.car_code);
        for (size_t a = 0; a < 2; ++a) hang[a] = std::clamp(travel[a], 0.0f, lift);
    }
    const race::CarState state = rest_state(placed * render::translate({0, ride + lift, 0}), c->spec, hang);

    render::DrawOptions o;
    for (float& k : o.clear) k = 0;
    o.composite_alpha = std::clamp(m.cur_alpha * opacity, 0.0f, 1.0f);
    o.repeated = true;
    o.sort_blended = true;
    o.gt3_lighting = true;
    // The stage's lights from light.mot at its time (CustumLightPosition 0x1D6C98): each one's place
    // as the direction toward it, in the world, with its ambient, diffuse and specular; the global
    // ambient 0. (Light 3, all zero, lights nothing. The file's ColorScale is not read by the
    // program.) Light 0 throws the car's shadow: shadowDarkness0 its strength, shadowBlur0 how far
    // its layers spread; bottomShadow is the shadow under the car.
    menu::FaceView lit = v;
    float stage_dark = 0, stage_blur = 0;
    race::CarVisual::Shadow stage_shadow;
    render::Vec3 shear{0, 0, 0};
    const bool stage_lights = s.advanced && light_motion_ && !light_motion_->lights.empty();
    if (stage_lights) {
        for (int i = 0; i < 3; ++i) {
            const auto it = light_motion_->lights.find(i);
            menu::FaceLight& L = lit.lights[i];
            L.on = it != light_motion_->lights.end();
            if (!L.on) continue;
            const menu::StageMotion::Light& ml = it->second;
            for (int k = 0; k < 3; ++k) {
                L.pos[k] = light_motion_->at(ml.position[size_t(k)], stage_time_);
                L.amb[k] = light_motion_->at(ml.ambient[size_t(k)], stage_time_);
                L.dif[k] = light_motion_->at(ml.diffuse[size_t(k)], stage_time_);
                L.spe[k] = light_motion_->at(ml.specular[size_t(k)], stage_time_);
            }
            L.pos[3] = 0;
        }
        for (float& a : lit.world_ambient) a = 0;
        const render::Vec3 l0 = render::normalize({lit.lights[0].pos[0], lit.lights[0].pos[1], lit.lights[0].pos[2]});
        if (l0.y > 0.2f) shear = {-l0.x / l0.y, 0, -l0.z / l0.y};
        const float bottom = std::clamp(light_motion_->named_at("bottomShadow", stage_time_, 0.7f), 0.01f, 1.0f);
        const float dark = std::clamp(light_motion_->named_at("shadowDarkness0", stage_time_, 0.6f), 0.0f, 1.0f);
        stage_shadow.on = true;
        stage_shadow.alpha = std::round(bottom * 64) / 64;
        stage_shadow.light_factor = 1 / stage_shadow.alpha; // (the thrown shape whole: its layers weigh it)
        stage_dark = dark, stage_blur = std::max(light_motion_->named_at("shadowBlur0", stage_time_, 0.0f), 0.0f);
    }
    menu_face3d::light(lit, o);
    SDL_GPUTexture* env = s.advanced ? stage_environment() : nullptr;
    if (!env) env = env_texture(project, page, s.env_image);
    if (env) {
        o.env_sphere = env;
        // The reflection's factors, C0 ([0x6238A0]) + C1 ([0x62389C]) min(V.N, 0): the stage's 1.5 and 0.8 (0x1D5C00), a car
        // face's 1.3 and 1.1 (0x156820).
        o.reflection_strength = s.advanced ? 1.5f : 1.3f;
        o.reflection_fresnel = s.advanced ? 0.8f / 1.5f : 1.1f / 1.3f;
    }
    // The car's own shadow under it, in light 0's shadow colour.
    race::CarVisual::Shadow sh;
    sh.on = true;
    for (int i = 0; i < 3; ++i) sh.rgb[i] = v.lights[0].sdw[i];
    sh.alpha = shadow_opacity >= 0 ? shadow_opacity : v.lights[0].sdw[3];
    sh.light_factor = 0; // (a car face asks for no thrown shadow, only the car's own under it: 0x451D18)
    if (stage_lights) sh = stage_shadow;
    // (set_shadow makes the shadows' models anew: only when the look changes; the light's
    // direction is set apart, every picture.)
    if (std::memcmp(&sh, &c->shadow, sizeof sh) != 0 || !c->shadow_set) c->visual->set_shadow(sh), c->shadow = sh, c->shadow_set = true;
    c->visual->set_shadow_shear(shear);
    items_.clear();
    // The backdrops' picture stands under the car's, whose own alpha fades only the car and its shadow (0x1D6378).
    // (A disc without the stage's files, Prologue's, has the page's own behind.)
    if (s.advanced && !stage_.empty()) draw_backdrops(backdrops(), view, proj, eye, page_rect, opacity);
    // The page's other models (loadModelSet: a mechanic, a shadow), moved by their motions.
    std::vector<render::DrawItem> sets;
    if (!s.advanced && fade >= 0.02f) add_model_motions(f, s, m, project, page, placed, c->spec, fade, sets);
    // `mirror` (normal mode): first the car and those models turned upside down about the floor
    // they stand on, without the car's shadow, its picture added to the page at 0.3 of its alpha
    // (0x156820; the composite's ALPHA 0x68, Cs x FIX + Cd, 0x2175D4); then the car.
    if (s.mirror && !s.advanced) {
        std::vector<render::DrawItem> mirrored = sets;
        c->visual->add_items(mirrored, state, eye);
        render::DrawOptions mo = o;
        mo.mirrored = true;
        mo.composite_alpha = 1;
        const Mat4 flip = render::scale({1, -1, 1});
        const Vec3 under{eye.x, -eye.y, eye.z};
        // Its own view: a view's picture is read when the frame is submitted, so one view
        // drawn twice a frame would show its last picture twice.
        if (!mirror_view_) mirror_view_ = std::make_unique<render::View3D>(*app_.gpu());
        SDL_Renderer* r = app_.renderer();
        const int tw = std::max(1, int(page_rect.w + 0.5f)), th = std::max(1, int(page_rect.h + 0.5f));
        SDL_Texture*& target = mirror_target_;
        if (target && (target->w != tw || target->h != th)) SDL_DestroyTexture(target), target = nullptr;
        if (!target) target = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
        SDL_Texture* before = SDL_GetRenderTarget(r);
        if (target && SDL_SetRenderTarget(r, target)) {
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            mirror_view_->draw(r, SDL_FRect{0, 0, float(tw), float(th)}, view * flip, proj, under, mirrored, mo);
            SDL_SetRenderTarget(r, before);
            SDL_SetTextureBlendMode(target, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaModFloat(target, o.composite_alpha * 0.3f);
            SDL_RenderTexture(r, target, nullptr, &page_rect);
        }
    }
    items_.insert(items_.end(), sets.begin(), sets.end());
    if (!stage_lights) c->visual->add_shadow_items(items_, state);
    else {
        // The stage's shadow (0x1D6378, 0x1D6968): per sample, five throws of the shape along the
        // light tilted sideways by the blur, weighing 1/2, 1/4 .. 1/32 (the alpha bits they
        // write), all the samples averaged. Here each is a layer over the last, at the alpha that
        // leaves the sum where they all cover; each is the body's shape and the four tires' (tire.mdl at each wheel's width
        // and radius, 0x452808), no part of it twice: the tires first, a hair above the body's, which the depth test keeps
        // off them. ❓ Not the half resolution it is made at.
        std::vector<render::DrawItem> thrown;
        c->visual->set_shadow_shear(shear);
        c->visual->add_shadow_items(thrown, state);
        if (thrown.size() < 2) items_.insert(items_.end(), thrown.begin(), thrown.end());
        else {
            const float lx = lit.lights[0].pos[0], ly = lit.lights[0].pos[1], lz = lit.lights[0].pos[2];
            const render::Vec3 n = render::normalize({lx, ly, lz}), r2 = render::normalize({-n.z, 0, n.x});
            constexpr int kSamples = 4;
            static const float kTurn[6] = {-1, 1, -2, 2, -3, 3}; // (0x6956B8)
            float before = 0;
            int layer = 0;
            for (int p = 1; p < 6; ++p)
                for (int i = 0; i < kSamples; ++i) {
                    const float angle = stage_blur * float(10 + p) / 16 * kDeg, phi = (float(i) + 0.125f) / kSamples + kTurn[p] / 8;
                    const float k = std::tan(angle) * std::sin(3.14159265f * phi);
                    const render::Vec3 l{n.x + r2.x * k, n.y, n.z + r2.z * k};
                    if (l.y < 0.1f) continue;
                    const float w = stage_dark * float(1 << (7 - p)) / 128 / kSamples;
                    c->visual->set_shadow_shear({-l.x / l.y, 0, -l.z / l.y});
                    std::vector<render::DrawItem> one;
                    c->visual->add_shadow_items(one, state);
                    one[0].alpha = w / (1 - before);
                    one[0].in_order = true;
                    const float height = 0.0005f * float(layer++); // (each layer over the last)
                    if (stage_tire_) {
                        // Onto the floor along the light.
                        Mat4 flat;
                        flat.m = {1, 0, 0, 0, -l.x / l.y, 0, -l.z / l.y, 0, 0, 0, 1, 0, 0, height + 0.00025f, 0, 1};
                        for (int k4 = 0; k4 < 4; ++k4) {
                            const int axle = k4 / 2;
                            const float width = c->spec.tire_width[axle], radius = c->spec.tire_radius[axle];
                            const course::Vec3 &right = state.right, &up = state.up, &ahead = state.forward, &at_w = state.wheels[k4].center;
                            Mat4 wheel;
                            wheel.m = {right.x * width,  right.y * width,  right.z * width,  0, up.x * radius, up.y * radius, up.z * radius, 0,
                                       -ahead.x * radius, -ahead.y * radius, -ahead.z * radius, 0, at_w.x - right.x * width / 2,
                                       at_w.y - right.y * width / 2, at_w.z - right.z * width / 2, 1};
                            render::DrawItem tire{stage_tire_.get(), flat * wheel, 0};
                            tire.alpha = one[0].alpha, tire.in_order = true;
                            items_.push_back(tire);
                        }
                        one[0].transform = render::translate({0, height - 0.02f, 0}) * one[0].transform;
                    }
                    items_.push_back(one[0]);
                    before += w;
                }
            items_.insert(items_.end(), thrown.begin() + 1, thrown.end());
        }
    }
    c->visual->add_items(items_, state, eye);
    view_->draw(app_.renderer(), page_rect, view, proj, eye, items_, o);
}

} // namespace gt4r
