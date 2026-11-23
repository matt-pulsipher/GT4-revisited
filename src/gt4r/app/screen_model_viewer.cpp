// 3D viewer: a list of models on the left, the selected one drawn with an orbit camera.
// GT4R_VIEWER="yaw=0.6,pitch=0.3,zoom=1,spin=0,full=1,lod=0,grid=0,wire=0,item=2" sets it up for
// pictures.

#include "gt4r/app/app.h"

#include "base/log.h"
#include "render/camera.h"
#include "render/test_models.h"
#include "render/view3d.h"

#include <algorithm>
#include <optional>

namespace gt4r {

namespace {

namespace render = gt3r::render;
namespace scene = gt3r::scene;

constexpr float kListW = 150, kTop = 26, kBottom = Ui::kHeight - 22, kRowH = 12;

class ModelViewerScreen : public Screen {
public:
    ModelViewerScreen(std::string title, std::vector<std::string> items, ModelLoader load)
        : title_(std::move(title)), items_(std::move(items)), load_(std::move(load)) {
        apply_debug_state();
    }
    ~ModelViewerScreen() override {
        model_.reset();
        grid_.reset();
        view_.reset();
    }

    void update(App& app, double dt) override {
        const MenuInput& m = app.menu();
        const int n = int(items_.size());
        gt3r::PadState pad = app.input().pad(0);
        if (!fullscreen_) {
            if (n > 0) {
                const int prev = sel_;
                sel_ = std::clamp(sel_ + m.vertical(), 0, n - 1);
                if (sel_ != prev) settle_ = 0;
            }
            using gt3r::PadAxis;
            pad.buttons = u16(pad.buttons & ~((1u << unsigned(PadButton::Up)) | (1u << unsigned(PadButton::Down)) |
                                              (1u << unsigned(PadButton::Left)) | (1u << unsigned(PadButton::Right))));
            pad.axes[size_t(PadAxis::LX)] = pad.axes[size_t(PadAxis::LY)] = 128;
            if (m.pressed(PadButton::Cross) && n > 0) {
                if (loaded_ != sel_) load(app, sel_);
                fullscreen_ = true;
            }
            if (m.pressed(PadButton::Triangle)) {
                app.pop();
                return;
            }
        } else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
            fullscreen_ = false;
        } else if (m.pressed(PadButton::Cross)) {
            camera_.turntable = !camera_.turntable;
        }
        settle_ += dt;
        if (n > 0 && loaded_ != sel_ && settle_ > 0.25) load(app, sel_);
        if (m.pressed(PadButton::Square)) options_.wire = render::WireMode((int(options_.wire) + 1) % 3);
        if (m.pressed(PadButton::Select)) {
            options_.lod = options_.lod >= max_lod_ ? -1 : options_.lod + 1;
            update_stats();
        }
        if (m.pressed(PadButton::L3)) show_grid_ = !show_grid_;
        camera_.update(pad, dt);
    }

    void on_event(App&, const SDL_Event& e) override {
        float x = 0, y = 0;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) x = e.button.x, y = e.button.y;
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) x = e.wheel.mouse_x, y = e.wheel.mouse_y;
        const bool starts_here = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL;
        const SDL_FRect r = view_rect();
        if (starts_here && !(x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)) return;
        camera_.handle_event(e);
    }

    void render(App& app, Ui& ui) override {
        if (app.gpu() && !view_) view_ = std::make_unique<render::View3D>(*app.gpu());
        if (clean_) { // only the model, over the whole window
            if (view_ && model_) {
                std::vector<render::DrawItem> draw{{model_.get(), {}, -2}};
                view_->draw(ui.renderer(), {0, 0, float(Ui::kWidth), float(Ui::kHeight)}, camera_, draw, options_);
            }
            return;
        }
        ui.header("3D VIEWER: " + title_, items_.empty() ? "" : items_[size_t(std::max(loaded_, 0))]);
        const SDL_FRect vr = view_rect();
        if (view_ && model_) {
            std::vector<render::DrawItem> draw{{model_.get(), {}, -2}};
            if (show_grid_ && grid_) draw.push_back({grid_.get(), {}, -1, true});
            view_->draw(ui.renderer(), vr, camera_, draw, options_);
        } else {
            ui.fill(vr.x, vr.y, vr.w, vr.h, colors::kPanel);
            const char* msg = !app.gpu() ? "3D drawing is not available on this system" : items_.empty() ? "Nothing to show" : "Loading...";
            if (!error_.empty()) msg = "";
            ui.text_center_in(vr.x, vr.w, vr.y + vr.h / 2 - 4, msg, colors::kDim);
        }
        if (!error_.empty()) ui.text(vr.x + 6, vr.y + 6, Ui::clip(error_, vr.w - 12), colors::kBad);
        if (!fullscreen_) draw_list(ui);
        draw_stats(ui, vr);
        ui.footer(fullscreen_ ? "Stick: orbit  {L1}/{R1}: zoom  {Cross}: spin  {Square}: wireframe  {Select}: detail level  {Triangle}: back"
                              : "{Up/Down}: select  {Cross}: view  {Square}: wireframe  {L1}/{R1}: zoom  {Triangle}: back");
    }

private:
    void apply_debug_state() {
        const char* env = SDL_getenv("GT4R_VIEWER");
        if (!env) return;
        const std::string s = env;
        for (size_t pos = 0; pos < s.size();) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            const std::string kv = s.substr(pos, end - pos);
            pos = end + 1;
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq);
            const float v = std::strtof(kv.c_str() + eq + 1, nullptr);
            if (k == "clean") clean_ = v != 0, fullscreen_ = fullscreen_ || clean_;
            else if (k == "item") sel_ = std::clamp(int(v), 0, std::max(0, int(items_.size()) - 1));
            else if (k == "wire") options_.wire = render::WireMode(std::clamp(int(v), 0, 2));
            else if (k == "lod") debug_lod_ = int(v);
            else if (k == "full") fullscreen_ = v != 0;
            else if (k == "yaw") debug_yaw_ = v;
            else if (k == "pitch") debug_pitch_ = v;
            else if (k == "zoom") debug_zoom_ = v;
            else if (k == "spin") camera_.turntable = v != 0;
            else if (k == "grid") show_grid_ = v != 0;
        }
    }

    SDL_FRect view_rect() const {
        if (fullscreen_) return {0, 20, float(Ui::kWidth), kBottom - 20 + 2};
        return {kListW + 6, kTop - 2, Ui::kWidth - kListW - 10, kBottom - kTop};
    }

    void draw_list(Ui& ui) {
        const int n = int(items_.size());
        const int rows = int((kBottom - kTop) / kRowH);
        const int first = ui.list_window(n, sel_, rows);
        ui.fill(0, kTop - 4, kListW + 2, kBottom - kTop + 4, colors::kBackground);
        for (int i = 0; i < rows && first + i < n; ++i) {
            const int idx = first + i;
            const float y = kTop + i * kRowH;
            if (idx == sel_) ui.fill(4, y - 2, kListW - 4, kRowH, colors::kHighlight);
            ui.text(8, y, Ui::clip(items_[size_t(idx)], kListW - 12), idx == loaded_ ? colors::kAccent : colors::kText);
        }
    }

    void draw_stats(Ui& ui, const SDL_FRect& vr) {
        char line[160];
        char lod[16];
        if (options_.lod < 0) std::snprintf(lod, sizeof(lod), "all");
        else std::snprintf(lod, sizeof(lod), "%d of %d", options_.lod, max_lod_);
        std::snprintf(line, sizeof(line), "%zu meshes  %zu vertices  %zu triangles  %zu textures  detail level %s", stats_.meshes,
                      stats_.vertices, stats_.triangles, stats_.textures, lod);
        const float y = vr.y + vr.h - 12;
        ui.fill(vr.x, y - 4, vr.w, 16, Color{10, 11, 14, 170});
        ui.text(vr.x + 6, y, line, colors::kText);
    }

    void update_stats() {
        const size_t i = size_t(options_.lod + 1);
        stats_ = i < lod_stats_.size() ? lod_stats_[i] : render::ModelStats{};
    }

    void load(App& app, int index) {
        loaded_ = index;
        settle_ = 0;
        error_.clear();
        model_.reset();
        grid_.reset();
        stats_ = {};
        lod_stats_.clear();
        try {
            scene::Model model = load_(app, size_t(index));
            model.compute_bounds();
            lod_stats_.push_back(render::model_stats(model, -1));
            max_lod_ = lod_stats_[0].max_lod;
            for (int l = 0; l <= max_lod_; ++l) lod_stats_.push_back(render::model_stats(model, l));
            if (options_.lod > max_lod_) options_.lod = 0;
            camera_.frame(model.bounds_min, model.bounds_max);
            if (debug_lod_ != -2) options_.lod = std::min(debug_lod_, max_lod_);
            if (debug_yaw_) camera_.yaw = *debug_yaw_;
            if (debug_pitch_) camera_.pitch = *debug_pitch_;
            if (debug_zoom_) camera_.distance *= *debug_zoom_;
            update_stats();
            if (app.gpu()) {
                model_ = std::make_unique<render::GpuModel>(*app.gpu(), model);
                const scene::Model grid = render::make_ground_grid(model.bounds_min, model.bounds_max);
                grid_ = std::make_unique<render::GpuModel>(*app.gpu(), grid);
            }
            LOG_INFO("Viewer: loaded '%s' (%zu meshes, %zu vertices, %zu triangles, %zu textures)", items_[size_t(index)].c_str(),
                     stats_.meshes, stats_.vertices, stats_.triangles, stats_.textures);
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Viewer: could not load '%s': %s", items_[size_t(index)].c_str(), e.what());
        }
    }

    std::string title_;
    std::vector<std::string> items_;
    ModelLoader load_;
    int sel_ = 0, loaded_ = -1, max_lod_ = 0;
    double settle_ = 1.0; // load the first item at once
    bool fullscreen_ = false, show_grid_ = true, clean_ = false;
    std::string error_;
    int debug_lod_ = -2;
    std::optional<float> debug_yaw_, debug_pitch_, debug_zoom_;
    render::ModelStats stats_;
    std::vector<render::ModelStats> lod_stats_; // [0] = all levels, [1 + n] = level n
    render::OrbitCamera camera_;
    render::DrawOptions options_;
    std::unique_ptr<render::View3D> view_;
    std::unique_ptr<render::GpuModel> model_, grid_;
};

} // namespace

std::unique_ptr<Screen> make_model_viewer(std::string title, std::vector<std::string> items, ModelLoader load) {
    return std::make_unique<ModelViewerScreen>(std::move(title), std::move(items), std::move(load));
}

} // namespace gt4r
