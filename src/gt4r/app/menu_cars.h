#pragma once

// The car faces of the running menus (MCarFace): the car's showroom model (car/menu/<model>) in
// its paint, placed and turned as its car model says, lit by the face's lights, reflecting the
// face's picture, drawn over the page with the face's camera. docs/formats/menu.md, "Cars".

#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/project.h"
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
    std::map<std::string, std::unique_ptr<Car>> cars_; // by code#paint
    std::map<const menu::LiveWidget*, Face> faces_;
    u64 frame_ = 0;
    std::map<std::string, std::string> models_; // car label -> model file name
    std::map<const void*, SDL_GPUTexture*> env_;
    std::vector<gt3r::render::DrawItem> items_;
    // The car-selection stage's backdrop (menu/select/bg.mdl, its first variant).
    std::unique_ptr<gt3r::render::GpuModel> stage_;
    bool stage_tried_ = false;
};

} // namespace gt4r
