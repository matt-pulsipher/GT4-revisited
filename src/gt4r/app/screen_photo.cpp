// The photo shoot (Photo Travel's sequence "photo_shoot"): the car on a photo location, the
// project photo_shoot's pages over it with the program's three faces drawn (the map, the finder,
// the taken photo), and the photo taken. docs/formats/photo.md.

#include "gt4r/app/app.h"
#include "gt4r/app/menu_cursor.h"
#include "gt4r/app/menu_page_drawer.h"
#include "gt4r/app/menu_painter.h"
#include "gt4r/app/photo_faces.h"
#include "gt4r/app/race_world.h"

#include "base/log.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/model/mdls.h"
#include "gt4r/menu/photo_natives.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"
#include "input/input_manager.h"
#include "race/occluder.h"
#include "render/gpu_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>

namespace gt4r {

namespace {

namespace render = gt3r::render;
using RVec = render::Vec3;

constexpr float kPi = 3.14159274f;
constexpr int kSlotPhoto = 51; // the location's block (CRPM)

class PhotoScreen : public Screen {
public:
    PhotoScreen(App& app, menu::Runtime& menus, const menu::RaceRequest& request) : app_(app), rt_(menus), drawer_(app), faces_(app) {
        try {
            set_up(request);
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Photo shoot: %s", e.what());
        }
    }
    ~PhotoScreen() override {
        rt_.photo.reset();
        rt_.photo_picture.reset();
        menu::text_measure().measure = measure_before_;
        if (target_) SDL_DestroyTexture(target_);
    }

    void update(App& app, double dt) override {
        const MenuInput& m = app.menu();
        if (!error_.empty()) {
            if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Triangle)) leave();
            return;
        }
        if (m.held(PadButton::Select) && m.pressed(PadButton::Start)) return leave();
        try {
            time_ += dt;
            const int steps = std::min(int(time_ * 60.0), 4);
            time_ = steps == 4 ? 0.0 : time_ - steps / 60.0;
            send_keys(m);
            const menu::Runtime::Pad pad = menu_runtime_pad(app.menu_pad());
            for (int i = 0; i < steps; ++i) {
                rt_.set_pad(pad);
                step();
                rt_.update();
                cursor_.step(rt_);
            }
            if (rt_.finished()) {
                menu::photo_day(rt_, rt_.photo ? rt_.photo->course : std::string());
                app.pop();
            }
        } catch (const std::exception& e) {
            rt_.note(std::string("stopped: ") + e.what());
            LOG_WARN("Photo shoot: %s", e.what());
        }
    }

    void render(App&, Ui& ui) override {
        ui.fill(0, 0, Ui::kWidth, Ui::kHeight, {0, 0, 0});
        if (!error_.empty()) {
            ui.text_center(Ui::kHeight / 2 - 10, "The photo shoot could not start: " + error_, colors::kBad);
            ui.footer("{Cross}: back");
            return;
        }
        if (rt_.photo && rt_.photo->rendering) expose(ui);
        const float scale = float(Ui::kHeight) / 480.0f, x = (Ui::kWidth - 640 * scale) / 2;
        ui_ = &ui;
        const menu::TransitionPicture tp = menu::transition_picture(rt_.vm().attribute(rt_.context(), "transition"));
        drawer_.menu_frame = rt_.frame();
        for (const auto& page : rt_.open_pages())
            if (menu::Project* project = rt_.project_of(*page)) drawer_.draw(*project, *page, x, 0, scale, 0, {}, tp.page_opacity);
        cursor_.draw(rt_, drawer_, x, scale);
        if (const float cover = rt_.fade_cover(); cover > 0) ui.fill(x, 0, 640 * scale, 480 * scale, Color{0, 0, 0, u8(std::clamp(cover, 0.0f, 1.0f) * 255.0f)});
        ui_ = nullptr;
    }

private:
    void set_up(const menu::RaceRequest& q) {
        std::string course = q.course;
        const std::optional<Bytes> file = app_.vfs()->read("crs/" + course);
        if (!file) throw FormatError("There is no course file crs/" + course + ".");
        const ByteSpan section = course::CourseFile::parse(*file).section(*file, kSlotPhoto);
        if (section.empty()) throw FormatError("This place has no photo location.");
        const photo::PhotoCourse place = photo::PhotoCourse::parse(section);
        WorldSetup ws;
        ws.course = course;
        WorldEntry e;
        e.label = q.car, e.paint = u32(std::max(0, q.paint));
        if (q.garage_slot >= 0) {
            e.equipment = menu::race_equipment(rt_.game(), q.garage_slot);
            e.condition = menu::race_condition(rt_.game(), q.garage_slot);
        }
        ws.entries.push_back(e);
        ws.own_hold = true;
        ws.options = rt_.game().options;
        world_ = std::make_unique<RaceWorld>(app_, ws);
        world_->quiet();
        const course::Runway* road = &world_->course().road();
        auto shoot = std::make_shared<menu::PhotoShoot>(place, [road](float x, float z, float from, float& y) { return road->height_at(x, z, from, y); });
        shoot->course = course, shoot->car_label = q.car;
        shoot->apertures = menu::photo_apertures(rt_);
        rt_.photo = shoot;
        rt_.photo_picture = std::make_shared<menu::PhotoPicture>();
        stand_car();
        // The map's model set.
        if (place.map_offset) try {
                mdls::Options o;
                const gt3r::scene::Model m = mdls::build_model(section.subspan(place.map_offset), o, course + "/map");
                map_ = std::make_unique<render::GpuModel>(*app_.gpu(), m);
            } catch (const std::exception& ex) {
                LOG_WARN("Photo map: %s", ex.what());
            }
        map_view_ = std::make_unique<render::View3D>(*app_.gpu());
        faces_.picture = [this]() { return rt_.photo_picture && !(rt_.photo && rt_.photo->rendering) ? rt_.photo_picture.get() : nullptr; };
        drawer_.draw_live = [this](const menu::LiveWidget& face, menu::Project*, const std::string&, const menu::Rect& at, const SDL_FRect& dst, float opacity) {
            if (face.is("PhotoViewFace")) draw_finder(dst);
            else if (face.is("PhotoMapWindow")) draw_map(dst);
            else if (face.is("PhotoRenderFace")) faces_.draw(at, dst, opacity);
        };
        measure_before_ = menu::text_measure().measure;
        menu::text_measure().measure = [this](const menu::LiveWidget& face, float& w, float& h) { drawer_.measure(face, w, h); };
        rt_.game().next_sequence.clear();
        rt_.start_project("photo_shoot");
    }

    void leave() {
        // (Select and Start, or a shoot that could not start: the menus go on where they came from.)
        menu::GameState& g = rt_.game();
        if (g.next_menu_project.empty()) g.next_menu_project = g.major_menu_project.empty() ? std::string("gtmode") : g.major_menu_project;
        g.next_menu_arg = g.major_menu_page;
        g.next_sequence = "menu";
        rt_.finish();
        app_.pop();
    }

    void send_keys(const MenuInput& m) {
        static const std::pair<PadButton, menu::Key> keys[] = {
            {PadButton::Cross, menu::Key::Accept}, {PadButton::Triangle, menu::Key::Cancel}, {PadButton::Start, menu::Key::Start}, {PadButton::Select, menu::Key::Select},
            {PadButton::L1, menu::Key::L1},        {PadButton::R1, menu::Key::R1},           {PadButton::L2, menu::Key::L2},       {PadButton::R2, menu::Key::R2},
            {PadButton::Square, menu::Key::Cancel},
        };
        // (The photo page's own modes take the directions: the page's script keeps them from the focus.)
        const bool map_mode = rt_.photo && rt_.photo->mode != photo::Mode::None && rt_.photo->mode != photo::Mode::MapZoom;
        send_menu_directions(rt_, m, !map_mode);
        for (const auto& [button, key] : keys)
            if (m.pressed(button)) rt_.key(key);
    }

    // The car where the shoot has it.
    void stand_car() {
        menu::PhotoShoot& s = *rt_.photo;
        world_->stand_car(0, s.camera.car.x, s.camera.car.z, s.camera.car_heading * kPi / 180.0f);
        const auto cars = world_->cars();
        cars_.assign(cars.begin(), cars.end());
        if (cars_.empty()) return;
        stood_ = {s.camera.car.x, s.camera.car.z, s.camera.car_heading};
        photo::CarBox& box = s.camera.car_box;
        const race::CarVisual& v = world_->visual(0);
        const render::Mat4 body = v.body_matrix(cars_[0]);
        box.at = body.transform_point({0, 0, 0});
        box.right = body.transform_point({1, 0, 0}) - box.at, box.up = body.transform_point({0, 1, 0}) - box.at;
        box.forward = body.transform_point({0, 0, -1}) - box.at;
        box.front = -v.bounds_min().z, box.rear = v.bounds_max().z;
        box.width = v.bounds_max().x - v.bounds_min().x, box.height = v.bounds_max().y;
    }

    void step() {
        menu::PhotoShoot& s = *rt_.photo;
        menu::step_photo_map(rt_, s);
        const std::array<float, 3> at{s.camera.car.x, s.camera.car.z, s.camera.car_heading};
        if (at != stood_) stand_car();
        if (!cars_.empty())
            for (int w = 0; w < 2; ++w) cars_[0].wheels[w].steer = s.wheel_angle * kPi / 180.0f;
        // The preview page opens: the photo is taken (the render face's making, 0x19B558); closed: it is gone.
        const bool preview = menu::photo_render_face(rt_) != nullptr;
        if (preview && !preview_) begin_exposure();
        if (!preview && preview_) s.rendering = false, rt_.photo_picture = std::make_shared<menu::PhotoPicture>();
        preview_ = preview;
        s.depth_at_middle = middle_depth();
    }

    // What the finder's middle shows, metres along the camera's axis: the car or the scenery.
    float middle_depth() const {
        const photo::PhotoCamera& c = rt_.photo->camera;
        const render::Mat4 v = c.view();
        const RVec ahead{-v.at(2, 0), -v.at(2, 1), -v.at(2, 2)};
        float near = 0, far = 500;
        const gt3r::race::Occluder& o = world_->course().occluder();
        if (!o.empty() && o.segment_blocked(c.eye, c.eye + ahead * far)) {
            for (int i = 0; i < 24; ++i) {
                const float mid = (near + far) * 0.5f;
                (o.segment_blocked(c.eye, c.eye + ahead * mid) ? far : near) = mid;
            }
        }
        // The car's box on the way.
        const photo::CarBox& b = c.car_box;
        const RVec d = c.eye - b.at;
        float t0 = 0, t1 = far;
        auto slab = [&](const RVec& axis, float lo, float hi) {
            const float p = render::dot(d, axis), q = render::dot(ahead, axis);
            if (std::fabs(q) < 1e-5f) {
                if (p < lo || p > hi) t1 = -1;
                return;
            }
            float a = (lo - p) / q, e = (hi - p) / q;
            if (a > e) std::swap(a, e);
            t0 = std::max(t0, a), t1 = std::min(t1, e);
        };
        slab(b.right, -b.width * 0.5f, b.width * 0.5f), slab(b.up, 0, b.height), slab(b.forward, -b.rear, b.front);
        return t0 <= t1 && t1 > 0 ? std::max(t0, 0.1f) : far;
    }

    // The projection's scales for the angle of view: over the picture's height when it is wide, its width when it is tall (0x371F88).
    static float picture_ratio(const menu::PhotoShoot& s) { return s.stand ? 1.0f / s.aspect.ratio : s.aspect.ratio; }
    static std::array<float, 2> scales(const menu::PhotoShoot& s) {
        const float ratio = picture_ratio(s), cot = 1.0f / std::tan(s.camera.angle * 0.5f * kPi / 180.0f);
        return {ratio >= 1 ? cot / ratio : cot, ratio >= 1 ? cot : cot * ratio};
    }
    race::ViewPose pose(const menu::PhotoShoot& s) const {
        race::ViewPose p;
        p.eye = s.camera.eye, p.view = s.camera.view();
        const float py = scales(s)[1];
        p.fov = 2 * std::atan(1.0f / (py * 0.75f)); // (ViewPose's is across a 4:3 picture's width)
        return p;
    }
    // The car's nearest point before the camera, metres; and whether any of its box is in the picture.
    bool car_in_view(const menu::PhotoShoot& s, float& nearest) const {
        const photo::CarBox& b = s.camera.car_box;
        const render::Mat4 v = s.camera.view();
        const auto [px, py] = scales(s);
        nearest = 1e9f;
        int out[5] = {};
        for (int i = 0; i < 8; ++i) {
            const RVec p = b.at + b.right * ((i & 1 ? 0.5f : -0.5f) * b.width) + b.up * (i & 2 ? b.height : 0.0f) + b.forward * (i & 4 ? b.front : -b.rear);
            const RVec e = v.transform_point(p);
            const float w = -e.z;
            nearest = std::min(nearest, w);
            out[0] += e.x * px < -w, out[1] += e.x * px > w, out[2] += e.y * py < -w, out[3] += e.y * py > w, out[4] += w <= 0;
        }
        for (int n : out)
            if (n == 8) return false;
        return true;
    }
    // The focus and the f-number of a picture: on the car unless the focus is set; a picture without the car is sharp.
    struct Focus {
        float distance = 10, f_number = 500;
    };
    Focus focus(const menu::PhotoShoot& s) const {
        Focus f;
        float nearest = 0;
        if (!car_in_view(s, nearest)) return f;
        f.f_number = s.camera.lens.f_number();
        f.distance = s.camera.lens.focus > 0 ? s.camera.lens.focus : std::max(photo::auto_focus(std::max(nearest, 0.1f), f.f_number, scales(s)[1]), 1.0f);
        return f;
    }

    // The finder (PhotoViewFace, 0x1BD438): the scene through the lens, its filters and vignette.
    void draw_finder(const SDL_FRect& dst) {
        if (!ui_ || !world_ || dst.w <= 1 || dst.h <= 1 || !rt_.photo) return;
        menu::PhotoShoot& s = *rt_.photo;
        SDL_Renderer* r = ui_->renderer();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, &dst);
        if (s.mode == photo::Mode::Camera && !s.camera.camera_allowed()) return; // (the page's View is insensitive then)
        RaceWorld::Lens lens;
        const Focus f = focus(s);
        float nearest = 0;
        const bool car = car_in_view(s, nearest);
        lens.dof.on = true;
        lens.dof.f_number = car ? f.f_number : 200.0f, lens.dof.focal_mm = 35, lens.dof.focus = car ? f.distance : 10.0f;
        const gt3r::race::SunParams& env = world_->course().env();
        lens.dof.far = env.has_env && env.far_clip > 0 ? env.far_clip : 5000.0f;
        lens.full_detail = false;
        world_->set_lens(lens);
        const photo::Correction c = s.correction();
        gt3r::render::DrawOptions::PostGrade g;
        g.on = !c.plain();
        g.black = c.black, g.white = c.white, g.saturation = c.saturation;
        // (The grade's curve is a quadratic: the gamma's value at the middle.)
        for (int i = 0; i < 3; ++i) g.curve[i] = std::clamp(2 * std::pow(0.5f, 1.0f / std::max(c.gamma[i], 1e-3f)) - 0.5f, 0.0f, 1.0f);
        world_->set_grade(g);
        world_->render(*ui_, dst, cars_, pose(s), 0, -1);
        world_->set_lens(std::nullopt);
        world_->set_grade({});
        draw_vignette(r, dst, course_vignette());
        draw_vignette(r, dst, photo::vignette(s.filters));
        if (s.mode == photo::Mode::Car || s.camera.inside_car()) draw_car_box(r, dst, s);
    }

    // The course's vignette, as the replays' picture has it.
    photo::Vignette course_vignette() const {
        const gt3r::race::SunParams& env = world_->course().env();
        if (!env.vignette) return {};
        return {env.vignette_outer_rgba[3], env.vignette_inner, env.vignette_outer};
    }
    // The vignette over a picture: a grid of quads, each corner its own darkness.
    void draw_vignette(SDL_Renderer* r, const SDL_FRect& dst, const photo::Vignette& f) {
        if (f.strength <= 0) return;
        constexpr int kX = 16, kY = 12;
        std::vector<SDL_Vertex> v;
        std::vector<int> index;
        for (int y = 0; y <= kY; ++y)
            for (int x = 0; x <= kX; ++x) {
                const float u = float(x) / kX, w = float(y) / kY;
                SDL_Vertex p{};
                p.position = {dst.x + dst.w * u, dst.y + dst.h * w};
                p.color = {0, 0, 0, f.alpha(u * 2 - 1, w * 2 - 1)};
                v.push_back(p);
            }
        for (int y = 0; y < kY; ++y)
            for (int x = 0; x < kX; ++x) {
                const int a = y * (kX + 1) + x;
                for (int i : {a, a + 1, a + kX + 1, a + 1, a + kX + 2, a + kX + 1}) index.push_back(i);
            }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r, nullptr, v.data(), int(v.size()), index.data(), int(index.size()));
    }

    // The car's box, drawn while it is moved or the camera stands in it (0x3C73C0): yellow-gray lines.
    void draw_car_box(SDL_Renderer* r, const SDL_FRect& dst, const menu::PhotoShoot& s) {
        const photo::CarBox& b = s.camera.car_box;
        const render::Mat4 v = s.camera.view();
        const auto [px, py] = scales(s);
        auto eye = [&](int i) {
            return v.transform_point(b.at + b.right * ((i & 1 ? 0.5f : -0.5f) * b.width) + b.up * (i & 2 ? b.height : 0.0f) + b.forward * (i & 4 ? b.front : -b.rear));
        };
        SDL_Rect clip_before{};
        const bool had = SDL_GetRenderClipRect(r, &clip_before) && clip_before.w > 0;
        const SDL_Rect clip{int(dst.x), int(dst.y), int(dst.w), int(dst.h)};
        SDL_SetRenderClipRect(r, &clip);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
        SDL_SetRenderDrawColor(r, 204, 204, 128, 255);
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 3; ++k) {
                const int j = i ^ (1 << k);
                if (j < i) continue;
                RVec p = eye(i), q = eye(j);
                constexpr float kNear = -0.1f;
                if (p.z > kNear && q.z > kNear) continue;
                if (p.z > kNear) std::swap(p, q);
                if (q.z > kNear) q = p + (q - p) * ((kNear - p.z) / (q.z - p.z));
                auto on = [&](const RVec& e) { return SDL_FPoint{dst.x + dst.w * 0.5f * (1 + e.x * px / -e.z), dst.y + dst.h * 0.5f * (1 - e.y * py / -e.z)}; };
                const SDL_FPoint a = on(p), c = on(q);
                SDL_RenderLine(r, a.x, a.y, c.x, c.y);
            }
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderClipRect(r, had ? &clip_before : nullptr);
    }

    // The map (PhotoMapWindow, 0x194AD0): the location's model from above, the places the camera and the car may take, the view.
    void draw_map(const SDL_FRect& dst) {
        if (!ui_ || dst.w <= 1 || dst.h <= 1 || !rt_.photo) return;
        menu::PhotoShoot& s = *rt_.photo;
        photo::PhotoCamera& c = s.camera;
        SDL_Renderer* r = ui_->renderer();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderFillRect(r, &dst);
        const float length = std::max(c.base_length, 1.0f), across = c.span_x();
        if (map_) {
            render::Mat4 proj;
            proj.m.fill(0);
            // x across, z down the window, every height on one plane.
            proj.at(0, 0) = 2 / across, proj.at(0, 3) = -2 * c.center_x / across;
            proj.at(1, 2) = -2 / length, proj.at(1, 3) = 2 * c.center_z / length;
            proj.at(2, 3) = 0.5f, proj.at(3, 3) = 1;
            render::DrawOptions o;
            for (float& k : o.clear) k = 0;
            o.clear[3] = 1;
            o.lod = 0;
            o.repeated = true;
            items_.clear();
            items_.push_back({map_.get(), render::Mat4::identity(), 0});
            items_.back().in_order = true;
            items_.back().background = true;
            map_view_->draw(r, dst, render::Mat4::identity(), proj, {c.center_x, 100, c.center_z}, items_, o);
        }
        const float kx = dst.w / c.window_w(), ky = dst.h / c.window_h();
        auto on = [&](float x, float z) { return SDL_FPoint{dst.x + c.map_x(x) * kx, dst.y + c.map_y(z) * ky}; };
        SDL_Rect clip_before{};
        const bool had = SDL_GetRenderClipRect(r, &clip_before) && clip_before.w > 0;
        const SDL_Rect clip{int(dst.x), int(dst.y), int(dst.w), int(dst.h)};
        SDL_SetRenderClipRect(r, &clip);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        // The cells; of the camera's, where the car may stand, blinking while it is moved (the colors: 0x618A98, 0x618AB8).
        const photo::PhotoCourse& p = c.course();
        int at = c.cell;
        if (s.mode == photo::Mode::Camera) at = c.camera_allowed() ? pointed_cell(c) : -1;
        for (size_t i = 0; i < p.cells.size(); ++i) {
            const photo::Cell& cell = p.cells[i];
            const bool here = int(i) == at;
            if (here)
                for (u16 k : cell.car_shapes) {
                    float rgba[4] = {0.845f, 0.196f, 0.1333f, 0.43f};
                    if (s.mode == photo::Mode::Car) {
                        const float b = std::max((s.blink - 0.5f) * 2, 0.0f), t = std::min(b * b, 1.0f);
                        for (float& v : rgba) v = std::min(v + (1 - v) * t, 1.0f);
                    }
                    if (k < p.shapes.size()) fill_shape(r, p.shapes[k], on, clip, rgba);
                }
            if (!cell.spot) {
                const float rgba[2][4] = {{0.196f, 0.3921f, 0.8313f, 0.3f}, {0.3921f, 0.549f, 0.7843f, 0.3f}};
                fill_shape(r, p.shapes[size_t(cell.shape)], on, clip, rgba[here]);
            }
        }
        // The view: a wedge from the camera as wide as the picture; its limits.
        if (s.mode != photo::Mode::Camera) {
            const float ratio = picture_ratio(s), wide = c.angle / (ratio >= 1 ? 1.0f / ratio : 1.0f), reach = 2 * c.most_length();
            auto ray = [&](float deg) {
                const float a = deg * kPi / 180.0f;
                return on(c.eye.x + reach * std::cos(a), c.eye.z + reach * std::sin(a));
            };
            SDL_Vertex v[3] = {};
            const SDL_FPoint corners[3] = {on(c.eye.x, c.eye.z), ray(c.yaw - 90 - wide * 0.5f), ray(c.yaw - 90 + wide * 0.5f)};
            for (int i = 0; i < 3; ++i) v[i].position = corners[i], v[i].color = {1, 1, 1, float(0x23) / 128.0f};
            SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
            if (c.limited) {
                SDL_SetRenderDrawColor(r, 255, 255, 255, u8(255.0f * float(0x50) / 128.0f));
                for (float deg : {c.least, c.greatest}) {
                    const SDL_FPoint e = ray(deg - 90);
                    SDL_RenderLine(r, corners[0].x, corners[0].y, e.x, e.y);
                }
            }
        }
        SDL_SetRenderClipRect(r, had ? &clip_before : nullptr);
    }
    static int pointed_cell(const photo::PhotoCamera& c) {
        photo::Vec2 p = c.pointer();
        return c.course().cell_at(p, 0.5f);
    }

    // A shape's inside (a point with an odd number of outlines round it), a row of the screen at a time.
    template <class On>
    void fill_shape(SDL_Renderer* r, const photo::Shape& shape, const On& on, const SDL_Rect& clip, const float rgba[4]) {
        rects_.clear();
        std::vector<std::vector<SDL_FPoint>> lines;
        float top = 1e9f, bottom = -1e9f;
        for (const auto& o : shape.outlines) {
            std::vector<SDL_FPoint> l;
            for (const photo::Vec2& p : o) l.push_back(on(p.x, p.z)), top = std::min(top, l.back().y), bottom = std::max(bottom, l.back().y);
            lines.push_back(std::move(l));
        }
        std::vector<float> cuts;
        for (int y = std::max(clip.y, int(std::floor(top))); y < std::min(clip.y + clip.h, int(std::ceil(bottom)) + 1); ++y) {
            const float at = float(y) + 0.5f;
            cuts.clear();
            for (const auto& l : lines)
                for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++) {
                    const SDL_FPoint &a = l[j], &b = l[i];
                    if ((a.y <= at) == (b.y <= at)) continue;
                    cuts.push_back(a.x + (b.x - a.x) * (at - a.y) / (b.y - a.y));
                }
            std::sort(cuts.begin(), cuts.end());
            for (size_t i = 0; i + 1 < cuts.size(); i += 2) rects_.push_back({cuts[i], float(y), cuts[i + 1] - cuts[i], 1});
        }
        SDL_SetRenderDrawColorFloat(r, rgba[0], rgba[1], rgba[2], rgba[3]);
        if (!rects_.empty()) SDL_RenderFillRects(r, rects_.data(), int(rects_.size()));
    }

    // ----- the photo: 32 pictures through the lens' points, added up (0x333A00)

    void begin_exposure() {
        menu::PhotoShoot& s = *rt_.photo;
        s.rendering = true;
        sample_ = 0;
        const bool stand = s.stand;
        const int w = s.aspect.w * photo::kTiles, h = s.aspect.h * photo::kTiles;
        width_ = stand ? h : w, height_ = stand ? w : h;
        sum_.assign(size_t(width_) * size_t(height_) * 3, 0);
        dark_.resize(size_t(width_) * size_t(height_) * 2);
        const photo::Vignette rings[2] = {course_vignette(), photo::vignette(s.filters)};
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                for (int k = 0; k < 2; ++k)
                    dark_[(size_t(y) * size_t(width_) + size_t(x)) * 2 + size_t(k)] = u8(rings[k].alpha((float(x) + 0.5f) / float(width_) * 2 - 1, (float(y) + 0.5f) / float(height_) * 2 - 1) * 128);
        samples_ = photo::samples(photo::kSamples);
        const photo::Correction c = s.correction();
        for (int i = 0; i < 256; ++i) {
            photo::Correction one = c;
            one.saturation = 1;
            const auto v = one.apply(u8(i), u8(i), u8(i));
            for (int k = 0; k < 3; ++k) lut_[k][i] = v[size_t(k)];
        }
        rt_.photo_picture = std::make_shared<menu::PhotoPicture>();
    }

    void expose(Ui& ui) {
        menu::PhotoShoot& s = *rt_.photo;
        SDL_Renderer* r = ui.renderer();
        if (!target_ || target_w_ != width_ || target_h_ != height_) {
            if (target_) SDL_DestroyTexture(target_);
            target_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width_, height_);
            target_w_ = width_, target_h_ = height_;
        }
        if (!target_) return void(s.rendering = false);
        SDL_Texture* before = SDL_GetRenderTarget(r);
        if (!SDL_SetRenderTarget(r, target_)) return void(s.rendering = false);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        const SDL_FRect all{0, 0, float(width_), float(height_)};
        const photo::Sample& sample = samples_[size_t(sample_)];
        const auto [px, py] = scales(s);
        const Focus f = focus(s);
        const photo::LensShift shift = photo::lens_shift(sample, f.distance, f.f_number, 32, picture_ratio(s), px, py, width_, height_);
        RaceWorld::Lens lens;
        lens.shear_x = shift.shear_x, lens.shear_y = shift.shear_y;
        world_->set_lens(lens);
        world_->set_grade({});
        race::ViewPose p = pose(s);
        p.view = render::translate({shift.move_x, shift.move_y, 0}) * p.view;
        const RVec right{p.view.at(0, 0), p.view.at(0, 1), p.view.at(0, 2)}, up{p.view.at(1, 0), p.view.at(1, 1), p.view.at(1, 2)};
        p.eye = p.eye - right * shift.move_x - up * shift.move_y;
        world_->render(ui, all, cars_, p, 0, -1);
        world_->set_lens(std::nullopt);
        if (SDL_Surface* shot = SDL_RenderReadPixels(r, nullptr)) {
            SDL_Surface* rgba = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(shot);
            if (rgba) {
                add(*rgba, s);
                SDL_DestroySurface(rgba);
            }
        }
        SDL_SetRenderTarget(r, before);
        if (++sample_ >= photo::kSamples) develop(s);
    }

    // A picture onto the average: corrected, the vignettes over it, blended (photo::expose).
    void add(const SDL_Surface& rgba, const menu::PhotoShoot& s) {
        const float sat = s.correction().saturation;
        const int weight = photo::exposure_weight(sample_);
        const int w = std::min(rgba.w, width_), h = std::min(rgba.h, height_);
        for (int y = 0; y < h; ++y) {
            const u8* p = static_cast<const u8*>(rgba.pixels) + size_t(y) * size_t(rgba.pitch);
            u8* out = &sum_[size_t(y) * size_t(width_) * 3];
            const u8* dark = &dark_[size_t(y) * size_t(width_) * 2];
            for (int x = 0; x < w; ++x, p += 4, out += 3, dark += 2) {
                int c[3] = {lut_[0][p[0]], lut_[1][p[1]], lut_[2][p[2]]};
                if (sat != 1) {
                    const float gray = 0.3f * float(c[0]) + 0.59f * float(c[1]) + 0.11f * float(c[2]);
                    for (int& v : c) v = std::clamp(int(sat * float(v) + (1 - sat) * gray), 0, 255);
                }
                for (int k = 0; k < 3; ++k) {
                    for (int ring = 0; ring < 2; ++ring)
                        if (dark[ring]) c[k] = ((-c[k] * dark[ring]) >> 7) + c[k];
                    out[k] = photo::expose(out[k], c[k], weight);
                }
            }
        }
    }

    void develop(menu::PhotoShoot& s) {
        s.rendering = false;
        auto picture = std::make_shared<menu::PhotoPicture>();
        picture->width = width_, picture->height = height_, picture->version = ++version_;
        picture->rgba.resize(size_t(width_) * size_t(height_));
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                const size_t i = size_t(y) * size_t(width_) + size_t(x);
                u32 px = 0xFF000000u;
                for (int k = 0; k < 3; ++k) px |= u32(sum_[i * 3 + size_t(k)]) << (8 * k);
                picture->rgba[i] = px;
            }
        picture->focus = focus_record(s);
        picture->jpeg = photo::encode_jpeg(sum_, width_, height_, photo::jpeg_quality(rt_.game().option_text("photo_quality")), picture->focus);
        rt_.photo_picture = std::move(picture);
        sum_.clear();
    }

    // Where the car is in the picture (0x3785D0): a box round it, a little longer and taller, in -1 .. 1.
    std::optional<menu::JpegPicture::Focus> focus_record(const menu::PhotoShoot& s) const {
        const photo::CarBox& b = s.camera.car_box;
        const render::Mat4 v = s.camera.view();
        const auto [px, py] = scales(s);
        float x0 = 10, x1 = -10, y0 = 10, y1 = -10;
        for (int i = 0; i < 8; ++i) {
            const RVec e = v.transform_point(b.at + b.right * ((i & 1 ? 0.5f : -0.5f) * b.width) + b.up * (b.height * (i & 2 ? 1.4f : -0.15f)) +
                                             b.forward * (i & 4 ? b.front * 1.6f : -b.rear));
            const float w = -e.z;
            float x = e.x * px, y = e.y * py;
            if (w < 0.001f) x = x < 0 ? -1.0f : x > 0 ? 1.0f : x, y = y < 0 ? -1.0f : y > 0 ? 1.0f : y;
            else x /= w, y /= w;
            x = std::clamp(x, -10.0f, 10.0f), y = std::clamp(y, -10.0f, 10.0f);
            x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
        }
        auto outside = [](float lo, float hi) { return hi <= -1 || 1 <= lo; };
        if (outside(x0, x1) || outside(y0, y1)) return std::nullopt;
        menu::JpegPicture::Focus f;
        f.x0 = std::max(x0, -1.0f), f.x1 = std::min(x1, 1.0f), f.y0 = std::max(y0, -1.0f), f.y1 = std::min(y1, 1.0f);
        f.x = (f.x0 + f.x1) * 0.5f, f.y = (f.y0 + f.y1) * 0.5f;
        return f;
    }

    App& app_;
    menu::Runtime& rt_;
    PageDrawer drawer_;
    PhotoFaces faces_;
    MenuCursor cursor_;
    Ui* ui_ = nullptr; // while a frame is drawn
    std::unique_ptr<RaceWorld> world_;
    std::vector<race::CarState> cars_;
    std::array<float, 3> stood_{};
    std::unique_ptr<render::GpuModel> map_;
    std::unique_ptr<render::View3D> map_view_;
    std::vector<render::DrawItem> items_;
    std::vector<SDL_FRect> rects_;
    std::function<void(const menu::LiveWidget&, float&, float&)> measure_before_;
    double time_ = 0;
    bool preview_ = false;
    std::string error_;
    // The photo being taken.
    int sample_ = 0, width_ = 0, height_ = 0, target_w_ = 0, target_h_ = 0;
    u64 version_ = 0;
    std::vector<photo::Sample> samples_;
    std::vector<u8> sum_, dark_; // the average so far; the vignettes' alpha, 128 whole
    u8 lut_[3][256] = {};
    SDL_Texture* target_ = nullptr;
};

} // namespace

std::unique_ptr<Screen> make_photo_screen(App& app, menu::Runtime& menus, const menu::RaceRequest& request) { return std::make_unique<PhotoScreen>(app, menus, request); }

} // namespace gt4r
