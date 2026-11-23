#include "gt4r/app/menu_cars.h"

#include "base/log.h"
#include "gt4r/app/app.h"
#include "gt4r/app/menu_face3d.h"
#include "gt4r/formats/model/car_model.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/race/car_spec.h"
#include "render/gpu_context.h"

#include <cmath>

namespace gt4r {

namespace render = gt3r::render;
using render::Mat4;
using render::Vec3;

namespace {

using menu_face3d::kDeg;
constexpr float kStageAzimuth = 60; // ❓ judged: degrees from straight ahead of the stage toward the car's left

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
    if (app_.gpu())
        for (auto& [k, t] : env_)
            if (t) app_.gpu()->release_texture(t);
}

void MenuCars::begin_frame() {
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
    m.step();

    // The camera's picture covers the whole page.
    const menu::Rect page_part{0, 0, 640, 480};
    const SDL_FRect page_rect = menu_face3d::on_screen(page_part, at, dst);
    const menu::FaceView& v = s.view;
    const Mat4 proj = menu_face3d::projection_of(v, at, page_part);
    Mat4 view = menu_face3d::view_of(v);
    Vec3 eye{v.camera_trans[0], v.camera_trans[1], v.camera_trans[2]};
    // The "advanced" stage takes the car at (x, y, -z) turned half round (0x1D5B88).
    const float z = s.advanced ? -m.cur[2] : m.cur[2];
    const float yaw = s.advanced ? m.yaw + 180 : m.yaw;
    if (s.advanced) {
        // ❓ The stage's camera (menu/select/camera.mot) is not read: the layout camera's distance
        // and height, seen from ahead and to the car's left, as the original's colour page shows it.
        const float dist = std::sqrt(v.camera_trans[0] * v.camera_trans[0] + v.camera_trans[2] * v.camera_trans[2]);
        const float az = kStageAzimuth * kDeg;
        eye = {-dist * std::sin(az), v.camera_trans[1], -dist * std::cos(az)};
        const float aim = v.camera_trans[1] + dist * std::tan(v.camera_rotate[0] * kDeg);
        view = render::look_at(eye, {0, aim, 0}, {0, 1, 0});
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
    if (s.advanced) {
        if (!stage_tried_) {
            stage_tried_ = true;
            try {
                if (const std::optional<Bytes> f = app_.vfs()->read("menu/select/bg.mdl")) {
                    mdls::Options mo;
                    mo.model = 0;
                    gt3r::scene::Model bg = mdls::build_model(*f, mo, "bg");
                    bg.compute_bounds();
                    LOG_INFO("Car stage bg.mdl: %zu meshes, (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)", bg.meshes.size(), bg.bounds_min[0], bg.bounds_min[1], bg.bounds_min[2],
                             bg.bounds_max[0], bg.bounds_max[1], bg.bounds_max[2]);
                    stage_ = std::make_unique<render::GpuModel>(*app_.gpu(), bg);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Car stage: %s", e.what());
            }
        }
        if (stage_) items_.push_back({stage_.get(), Mat4::identity(), -2});
    }
    c->visual->add_shadow_items(items_, state);
    c->visual->add_items(items_, state, eye);
    view_->draw(app_.renderer(), page_rect, view, proj, eye, items_, o);
}

} // namespace gt4r
