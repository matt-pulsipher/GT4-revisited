#include "gt4r/app/menu_models.h"

#include "base/log.h"
#include "gt4r/app/app.h"
#include "gt4r/app/menu_image_key.h"
#include "gt4r/app/menu_face3d.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/runtime.h"
#include "render/gpu_context.h"

#include <cmath>
#include <cstring>

namespace gt4r {

namespace render = gt3r::render;
using menu_face3d::kDeg;

MenuModels::MenuModels(App& app) : app_(app) {
    if (app.gpu()) view_ = std::make_unique<render::View3D>(*app.gpu());
}

MenuModels::~MenuModels() {
    models_.clear();
    views_.clear();
    view_.reset();
    if (app_.gpu())
        for (auto& [k, t] : env_)
            if (t) app_.gpu()->release_texture(t);
}

void MenuModels::begin_frame(u64 frame) {
    frame_ = frame;
    for (auto& [w, f] : faces_) f.seen = false;
}

void MenuModels::end_frame() {
    for (auto it = faces_.begin(); it != faces_.end();)
        if (!it->second.seen || it->second.widget.expired()) views_.erase(it->first), it = faces_.erase(it);
        else ++it;
}

const render::GpuModel* MenuModels::model(menu::Project* project, const std::string& page, const menu::ModelFaceSettings& s) {
    const std::string key = (s.from_disk ? std::string() : page) + "|" + s.model_set_path;
    if (const auto it = models_.find(key); it != models_.end()) return it->second.get();
    std::unique_ptr<render::GpuModel>& m = models_[key];
    try {
        // From the page's picture packs by the entry's name, or (model_set_from_disk) the disc.
        std::optional<Bytes> file;
        if (s.from_disk) {
            std::string path = s.model_set_path;
            while (!path.empty() && path[0] == '/') path.erase(0, 1);
            file = app_.vfs()->read(path);
        } else if (project)
            file = project->entry(page, s.model_set_path);
        if (!file) throw FormatError("not found");
        // A model's reflection passes look up the set's own pictures by the normal in view (the prize
        // pages' pouch and cups), added to what is drawn.
        gt3r::gtm1::Options mo;
        mo.static_env_maps = true;
        const gt3r::scene::Model built = mdls::build_model(*file, mo, mdls::Scene{}, s.model_set_path);
        m = std::make_unique<render::GpuModel>(*app_.gpu(), built);
    } catch (const std::exception& e) {
        LOG_WARN("Menu model %s: %s", s.model_set_path.c_str(), e.what());
    }
    return m.get();
}

SDL_GPUTexture* MenuModels::env_texture(menu::Project* project, const std::string& page, const std::string& path) {
    const gt3r::Tex1::Image* img = project && !path.empty() ? project->image(page, path, true) : nullptr;
    if (!img || !img->width || !img->height) return nullptr;
    SDL_GPUTexture*& t = env_[image_key(*img)];
    if (!t) {
        render::Uploader up(app_.gpu()->device());
        t = up.texture_rgba(img->width, img->height, reinterpret_cast<const u32*>(img->rgba.data()), false);
        up.submit();
    }
    return t;
}

void MenuModels::draw(const menu::LiveWidget& face, menu::Project* project, const std::string& page, const menu::Rect& at, const SDL_FRect& dst, float opacity) {
    if (!view_) return;
    const menu::ModelFaceSettings s = menu::model_face_settings(face.data);
    if (s.model_set_path.empty()) return;
    const render::GpuModel* m = model(project, page, s);
    if (!m) return;

    Face& f = faces_[&face];
    menu::LiveWidget& live = const_cast<menu::LiveWidget&>(face);
    if (f.widget.expired() || f.path != s.model_set_path) {
        // Fresh from the layout: the home rotation is the layout's.
        f.spin = {};
        f.spin.reset(s.rotate);
        std::memcpy(f.spin.rotate_v, s.rotate_v, sizeof s.rotate_v);
        float v[3] = {};
        menu::face_floats(face.data, "rotate_v", v, 3);
        std::memcpy(f.seen_v[0], v, sizeof v);
        std::memcpy(f.seen_v[1], s.rotate_v, sizeof v);
    } else {
        // The scripts set the speeds by either name: the one that changed counts.
        float v[3] = {};
        if (menu::face_floats(face.data, "rotate_v", v, 3) == 3 && std::memcmp(v, f.seen_v[0], sizeof v) != 0)
            std::memcpy(f.seen_v[0], v, sizeof v), std::memcpy(f.spin.rotate_v, v, sizeof v);
        if (std::memcmp(s.rotate_v, f.seen_v[1], sizeof v) != 0) std::memcpy(f.seen_v[1], s.rotate_v, sizeof v), std::memcpy(f.spin.rotate_v, s.rotate_v, sizeof v);
    }
    f.spin.target_mode = face.data.number("target_mode") != 0;
    f.widget = live.weak_from_this();
    f.path = s.model_set_path;
    f.seen = true;
    // One step a picture of the menus, however often the screen is drawn.
    if (f.stepped == 0 || f.stepped > frame_) f.stepped = frame_ ? frame_ - 1 : 0;
    for (int n = 0; f.stepped < frame_ && n < 10; ++n, ++f.stepped) f.spin.step();
    f.stepped = frame_;

    // The program draws over the whole page; here a part of it three times the face's size
    // around it (a face's model stays near its rectangle), so many small faces stay cheap.
    menu::Rect part{at.x - at.w, at.y - at.h, at.w * 3, at.h * 3};
    const float x0 = std::max(part.x, 0.0f), y0 = std::max(part.y, 0.0f);
    part = {x0, y0, std::min(part.x + part.w, 640.0f) - x0, std::min(part.y + part.h, 480.0f) - y0};
    if (part.w <= 0 || part.h <= 0) return;
    const menu::FaceView& v = s.view;
    const render::Mat4 proj = menu_face3d::projection_of(v, at, part);
    const render::Mat4 view = menu_face3d::view_of(v);
    const render::Vec3 eye{v.camera_trans[0], v.camera_trans[1], v.camera_trans[2]};
    // Model = T(translate) Rz Rx Ry (0x21AB38).
    const render::Mat4 placed = render::translate({s.translate[0], s.translate[1], s.translate[2]}) * render::rotate_z(f.spin.rotate[2] * kDeg) *
                                render::rotate_x(f.spin.rotate[0] * kDeg) * render::rotate_y(f.spin.rotate[1] * kDeg);
    render::DrawOptions o;
    for (float& k : o.clear) k = 0;
    o.composite_alpha = std::clamp(opacity, 0.0f, 1.0f);
    o.repeated = true;
    o.sort_blended = true;
    o.lod = 0;
    menu_face3d::light(v, o);
    if (SDL_GPUTexture* env = env_texture(project, page, s.env_image_path)) o.env_sphere = env, o.reflection_strength = 1.0f;
    items_.clear();
    items_.push_back({m, placed, 0});
    items_.back().sphere_map = true;
    const SDL_FRect shown = menu_face3d::on_screen(part, at, dst);
    std::unique_ptr<render::View3D>& v3 = views_[&face];
    if (!v3) v3 = std::make_unique<render::View3D>(*app_.gpu());
    if (const SDL_Point px = app_.menu_model_size(shown); px.x > 0) o.fixed_width = px.x, o.fixed_height = px.y;
    v3->draw(app_.renderer(), shown, view, proj, eye, items_, o);
}

} // namespace gt4r
