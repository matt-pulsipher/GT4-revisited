#pragma once

// The car faces of the running menus (MCarFace): the car's showroom model (car/menu/<model>) in
// its paint, placed and turned as its car model says, lit by the face's lights, reflecting the
// face's picture, drawn over the page with the face's camera. docs/formats/menu.md, "Cars".

#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/stage_motion.h"
#include "gt4r/race/car_visual.h"
#include "render/view3d.h"

#include <SDL3/SDL_render.h>

#include <map>
#include <memory>
#include <string>

namespace gt4r {

class App;

class MenuCars {
public:
    explicit MenuCars(App& app);
    ~MenuCars();

    // `frame`: the menus' picture count (Runtime::frame()): the motions step once a picture.
    void begin_frame(u64 frame);
    void end_frame();
    // Draws a car face (PageDrawer::draw_live): `at` its rectangle in the page, `dst` on the screen.
    void draw(const menu::LiveWidget& face, menu::Project* project, const std::string& page, const menu::Rect& at, const SDL_FRect& dst, float opacity);
    size_t shown() const { return faces_.size(); }

private:
    struct Car {
        std::unique_ptr<race::CarVisual> visual;
        race::CarSpec spec;
        bool failed = false;
    };
    struct Face {
        std::weak_ptr<script::Heap> widget;
        std::string car; // car_code#paint
        bool seen = false;
        u64 stepped = 0; // the picture its motion was last stepped to
    };
    Car* car(const std::string& code, int color);
    SDL_GPUTexture* env_texture(menu::Project* project, const std::string& page, const std::string& path);

    App& app_;
    std::unique_ptr<gt3r::render::View3D> view_;
    std::unique_ptr<gt3r::render::View3D> mirror_view_; // the cars' mirrored copies (`mirror`)
    std::map<std::string, std::unique_ptr<Car>> cars_; // by code#paint
    std::map<const menu::LiveWidget*, Face> faces_;
    u64 frame_ = 0;
    std::map<std::string, std::string> models_; // car label -> model file name
    std::map<u64, SDL_GPUTexture*> env_; // by image_key
    std::vector<gt3r::render::DrawItem> items_;
    void load_stage();
    // The car-selection stage: its backdrops (menu/select/bg.mdl's models), camera and light
    // motions, and its time.
    std::vector<std::unique_ptr<gt3r::render::GpuModel>> stage_;
    std::unique_ptr<menu::StageMotion> camera_motion_, light_motion_;
    float stage_time_ = 0;
    bool stage_tried_ = false;
    // A face's camera motion and model sets (loadModelOptionMotion, loadModelSet), read from the
    // page's packs once (null: not there).
    const menu::StageMotion* option_motion(menu::Project* project, const std::string& page, const std::string& path);
    const std::vector<std::unique_ptr<gt3r::render::GpuModel>>* model_set(menu::Project* project, const std::string& page, const std::string& path);
    std::map<std::string, std::unique_ptr<menu::StageMotion>> option_motions_;
    std::map<std::string, std::vector<std::unique_ptr<gt3r::render::GpuModel>>> model_sets_;
};

} // namespace gt4r
