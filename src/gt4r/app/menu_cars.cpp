#include "gt4r/app/menu_cars.h"

#include "base/log.h"
#include "gt4r/app/app.h"
#include "gt4r/app/menu_face3d.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/stage_motion.h"
#include "gt4r/race/car_spec.h"
#include "render/gpu_context.h"

#include <cmath>

namespace gt4r {

namespace render = gt3r::render;
using render::Mat4;
using render::Vec3;

namespace {

using menu_face3d::kDeg;

course::Vec3 cv(const Vec3& v) { return {v.x, v.y, v.z}; }
Vec3 apply(const Mat4& m, const Vec3& v, float w) {
    return {m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z + m.at(0, 3) * w, m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z + m.at(1, 3) * w,
            m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z + m.at(2, 3) * w};
}

// The car standing in the model's frame `model`: its axes and its wheels at rest, so that the
// body is drawn with its origin (the middle of the wheelbase on its underside) at the frame's.
race::CarState rest_state(const Mat4& model, const race::CarSpec& spec) {
    race::CarState s;
    s.position = cv(apply(model, {0, 0, 0}, 1));
    s.right = cv(render::normalize(apply(model, {1, 0, 0}, 0)));
    s.up = cv(render::normalize(apply(model, {0, 1, 0}, 0)));
    s.forward = cv(render::normalize(apply(model, {0, 0, -1}, 0))); // the front is at -z
    for (int w = 0; w < 4; ++w) {
        const int axle = w / 2;
        const float x = (w & 1) ? spec.half_track[axle] : -spec.half_track[axle];
        const float z = axle == 0 ? -spec.wheelbase / 2 : spec.wheelbase / 2;
        s.wheels[w].center = cv(apply(model, {x, spec.tire_radius[axle] - spec.ride_height[axle], z}, 1));
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
    if (app_.gpu())
        for (auto& [k, t] : env_)
            if (t) app_.gpu()->release_texture(t);
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
    if (c) return c->failed ? nullptr : c.get();
    c = std::make_unique<Car>();
    const Uint64 t0 = SDL_GetTicksNS();
    struct Took {
        Uint64 t0;
        const std::string& code;
        ~Took() { LOG_INFO("Menu car %s made in %.1f ms", code.c_str(), double(SDL_GetTicksNS() - t0) / 1e6); }
    } took{t0, code};
    db::SpecDb* db = app_.db();
    if (!db || !app_.gpu()) return c->failed = true, nullptr;
    if (models_.empty())
        for (const db::CarEntry& e : db->cars()) models_[e.label] = e.model;
    const auto model = models_.find(code);
    try {
        if (model == models_.end()) throw FormatError("no such car in the database");
        const std::string path = "car/menu/" + model->second;
        const std::optional<Bytes> file = app_.vfs()->read(path);
        if (!file || !car::CarFile::is_car(*file)) throw FormatError("no showroom model " + path);
        const std::optional<Bytes> pat = app_.vfs()->read(path + ".pat");
        c->spec = race::read_car_spec(*db, code);
        const std::vector<u32> paints = race::car_paints(*db, code);
        race::CarVisual::Options o;
        const u32 pick = u32(std::max(color, 0));
        o.paint = paints.empty() ? pick : paints[pick % paints.size()];
        c->visual = std::make_unique<race::CarVisual>(*app_.gpu(), *file, pat ? ByteSpan(*pat) : ByteSpan{}, c->spec, o, code);
    } catch (const std::exception& e) {
        LOG_WARN("Menu car %s: %s", code.c_str(), e.what());
        c->failed = true;
        return nullptr;
    }
    return c.get();
}

SDL_GPUTexture* MenuCars::env_texture(menu::Project* project, const std::string& page, const std::string& path) {
    const gt3r::Tex1::Image* img = project && !path.empty() ? project->image(page, path) : nullptr;
    if (!img || !img->width || !img->height || !app_.gpu()) return nullptr;
    SDL_GPUTexture*& t = env_[img];
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
                stage_.push_back(std::make_unique<render::GpuModel>(*app_.gpu(), bg));
            }
        }
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/camera.mot"))
            camera_motion_ = std::make_unique<menu::StageMotion>(menu::StageMotion::parse(*f));
        if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/light.mot"))
            light_motion_ = std::make_unique<menu::StageMotion>(menu::StageMotion::parse(*f));
    } catch (const std::exception& e) {
        LOG_WARN("Car stage: %s", e.what());
    }
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

const std::vector<std::unique_ptr<gt3r::render::GpuModel>>* MenuCars::model_set(menu::Project* project, const std::string& page, const std::string& path) {
    auto [it, fresh] = model_sets_.try_emplace(page + ":" + path);
    if (fresh && project && app_.gpu()) try {
            if (const std::optional<Bytes> f = project->entry(page, path); f && f->size() > 0x18) {
                const u32 count = rd16(*f, 0x16);
                for (u32 i = 0; i < count && i < 16; ++i) {
                    mdls::Options mo;
                    mo.model = int(i);
                    gt3r::scene::Model m = mdls::build_model(*f, mo, path + "#" + std::to_string(i));
                    it->second.push_back(std::make_unique<render::GpuModel>(*app_.gpu(), m));
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Car face models %s: %s", path.c_str(), e.what());
        }
    return &it->second;
}

void MenuCars::draw(const menu::LiveWidget& face, menu::Project* project, const std::string& page, const menu::Rect& at, const SDL_FRect& dst, float opacity) {
    if (!view_) return;
    const menu::CarFaceSettings s = menu::car_face_settings(face.data);
    if (s.car_code.empty()) return;
    Car* c = car(s.car_code, s.car_color);
    if (!c) return;

    // The car model the scripts move (face.car_model), made when they first ask for it.
    menu::LiveWidget& live = const_cast<menu::LiveWidget&>(face);
    std::shared_ptr<menu::CarModelObject> model;
    if (const auto it = live.extras.find("car_model"); it != live.extras.end())
        if (auto cell = std::dynamic_pointer_cast<script::Variable>(it->second)) model = cell->value.as<menu::CarModelObject>();
    if (!model) {
        model = menu::make_car_model_object();
        live.extras["car_model"] = std::make_shared<script::Variable>(menu::object_value(model));
    }
    Face& f = faces_[&face];
    const std::string key = s.car_code + "#" + std::to_string(s.car_color);
    if (f.widget.expired() || f.car.substr(0, f.car.find('#')) != s.car_code) {
        // A car set on the face: placed where the layout says and faded in.
        model->m.set_car(s.trans, s.rotate);
    }
    f.widget = live.weak_from_this();
    f.car = key;
    f.seen = true;
    menu::CarMotion& m = model->m;
    // One step a picture of the menus, however often the screen is drawn.
    if (f.stepped == 0 || f.stepped > frame_) f.stepped = frame_ ? frame_ - 1 : 0;
    const u64 steps = frame_ > f.stepped ? frame_ - f.stepped : 0;
    for (int n = 0; f.stepped < frame_ && n < 10; ++n, ++f.stepped) m.step();
    f.stepped = frame_;
    // The stage's time: 1/60 s a picture, round the light motion's length (0x1D5B88).
    if (s.advanced) {
        stage_time_ += float(std::min<u64>(steps, 10)) / 60.0f;
        const float len = light_motion_ ? light_motion_->length() : 0;
        if (len > 0) stage_time_ = std::fmod(stage_time_, len);
    }

    // The camera's picture covers the whole page.
    const menu::Rect page_part{0, 0, 640, 480};
    const SDL_FRect page_rect = menu_face3d::on_screen(page_part, at, dst);
    const menu::FaceView& v = s.view;
    Mat4 proj = menu_face3d::projection_of(v, at, page_part);
    Mat4 view = menu_face3d::view_of(v);
    Vec3 eye{v.camera_trans[0], v.camera_trans[1], v.camera_trans[2]};
    std::vector<float> weights; // the stage's backdrops
    if (s.advanced) load_stage();
    // The "advanced" stage takes the car at (x, y, -z) turned half round (0x1D5B88).
    const float z = s.advanced ? -m.cur[2] : m.cur[2];
    const float yaw = s.advanced ? m.yaw + 180 : m.yaw;
    if (s.advanced && camera_motion_ && camera_motion_->camera) {
        // The stage's camera (menu/select/camera.mot at the stage's time): its eye, the point
        // it looks at and its up; its field of view across the page's height (0x1D5C00).
        const auto& c = *camera_motion_->camera;
        auto at_t = [&](int i) { return camera_motion_->at(c[size_t(i)], stage_time_); };
        eye = {at_t(0), at_t(1), at_t(2)};
        view = render::look_at(eye, {at_t(3), at_t(4), at_t(5)}, {at_t(6), at_t(7), at_t(8)});
        menu::FaceView sv = v;
        sv.view_angle = (camera_motion_->fov ? camera_motion_->at(*camera_motion_->fov, stage_time_) : 0.45f) / kDeg;
        sv.near = 0.1f, sv.far = 100;
        proj = menu_face3d::projection_of(sv, page_part, page_part);
    }
    const menu::StageMotion* om = s.advanced || s.option_motion.empty() ? nullptr : option_motion(project, page, s.option_motion);
    if (om) {
        // The page's camera motion (loadModelOptionMotion) from the picture it was given at,
        // held at its end: its eye, the point looked at, its up and field of view.
        const float len = om->length();
        const float t = std::clamp(float(s64(frame_) - s64(s.option_motion_frame)) / 60.0f, 0.0f, len > 0 ? len : 0.0f);
        const auto& c = *om->camera;
        auto at_t = [&](int i) { return om->at(c[size_t(i)], t); };
        eye = {at_t(0), at_t(1), at_t(2)};
        view = render::look_at(eye, {at_t(3), at_t(4), at_t(5)}, {at_t(6), at_t(7), at_t(8)});
        menu::FaceView sv = v;
        if (om->fov) sv.view_angle = om->at(*om->fov, t) / kDeg;
        sv.far = std::max(sv.far, 100.0f);
        proj = menu_face3d::projection_of(sv, at, page_part);
    }
    if (s.advanced && light_motion_) {
        // The backdrops' weights at the stage's time (light.mot's Alpha0 .. Alpha3).
        for (size_t i = 0; i < stage_.size(); ++i) weights.push_back(std::clamp(light_motion_->named_at("Alpha" + std::to_string(i), stage_time_, i == 0 ? 1.0f : 0.0f), 0.0f, 1.0f));
    }
    const Mat4 placed = render::translate({m.cur[0], m.cur[1], z}) * render::rotate_z(m.cur_rz * kDeg) * render::rotate_x(m.cur_rx * kDeg) *
                        render::rotate_y(yaw * kDeg);
    // The model's origin is on the body's underside: the tires stand on y = 0 with the body
    // raised by its ride height (❓ judged from the pictures: the program's ShowRoomCar is not read).
    const float ride = (c->spec.ride_height[0] + c->spec.ride_height[1]) / 2;
    const race::CarState state = rest_state(placed * render::translate({0, ride, 0}), c->spec);

    render::DrawOptions o;
    for (float& k : o.clear) k = 0;
    o.composite_alpha = std::clamp(m.cur_alpha * opacity, 0.0f, 1.0f);
    o.repeated = true;
    o.sort_blended = true;
    o.gt3_lighting = true;
    menu_face3d::light(v, o);
    if (SDL_GPUTexture* env = env_texture(project, page, s.env_image)) {
        o.env_sphere = env;
        o.reflection_strength = 1.0f; // ❓ the program's reflection parameters (1.3, 1.1) are not read
    }
    // The car's own shadow under it, in light 0's shadow colour.
    race::CarVisual::Shadow sh;
    sh.on = true;
    for (int i = 0; i < 3; ++i) sh.rgb[i] = v.lights[0].sdw[i];
    sh.alpha = v.lights[0].sdw[3];
    c->visual->set_shadow(sh);
    items_.clear();
    if (s.advanced)
        // Each backdrop with a weight, cross-faded over black (0x1D5588).
        for (size_t i = 0; i < stage_.size() && i < weights.size(); ++i)
            if (stage_[i] && weights[i] > 0.004f) {
                render::DrawItem it{stage_[i].get(), Mat4::identity(), -2};
                it.alpha = weights[i];
                items_.push_back(it);
            }
    // `mirror` (normal mode): first the car turned upside down about the floor it stands on,
    // without its shadow, at 0.3 of its alpha (0x156820, 0x217058); then the car.
    if (s.mirror && !s.advanced) {
        std::vector<render::DrawItem> mirrored;
        c->visual->add_items(mirrored, state, eye);
        render::DrawOptions mo = o;
        mo.mirrored = true;
        mo.composite_alpha = o.composite_alpha * 0.3f;
        const Mat4 flip = render::scale({1, -1, 1});
        const Vec3 under{eye.x, -eye.y, eye.z};
        // Its own view: a view's picture is read when the frame is submitted, so one view
        // drawn twice a frame would show its last picture twice.
        if (!mirror_view_) mirror_view_ = std::make_unique<render::View3D>(*app_.gpu());
        mirror_view_->draw(app_.renderer(), page_rect, view * flip, proj, under, mirrored, mo);
    }
    // The page's other models (loadModelSet: a mechanic, a shadow) where their files put them.
    if (!s.advanced)
        for (const std::string& path : s.model_sets)
            if (const auto* set = model_set(project, page, path))
                for (const auto& m : *set) items_.push_back(render::DrawItem{m.get(), Mat4::identity(), 0});
    c->visual->add_shadow_items(items_, state);
    c->visual->add_items(items_, state, eye);
    view_->draw(app_.renderer(), page_rect, view, proj, eye, items_, o);
}

} // namespace gt4r
