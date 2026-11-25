#pragma once

// The car faces of the running menus (MCarFace): the car's showroom model (car/menu/<model>) in
// its paint, placed and turned as its car model says, lit by the face's lights, reflecting the
// face's picture, drawn over the page with the face's camera. docs/formats/menu.md, "Cars".

#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/menu/faces_car.h"
#include "gt4r/menu/model_motion.h"
#include "gt4r/menu/project.h"
#include "gt4r/menu/stage_motion.h"
#include "gt4r/race/car_visual.h"
#include "render/view3d.h"

#include <SDL3/SDL_render.h>

#include <future>
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
        std::future<std::unique_ptr<race::CarVisual>> loading; // being built on a worker
        std::unique_ptr<race::CarVisual> building;              // built, being sent to the GPU
        u64 asked = 0;                                          // when it was asked for (ns)
        race::CarVisual::Shadow shadow;                         // the shadow's look last set
        bool shadow_set = false;
    };
    struct Face {
        std::weak_ptr<script::Heap> widget;
        std::string car; // car_code#paint
        bool seen = false;
        u64 stepped = 0; // the picture its motion was last stepped to
        // The posed copies of its skinned sets, one per model motion (two motions of one set are
        // two copies in a picture).
        std::vector<std::unique_ptr<gt3r::render::GpuModel>> posed;
    };
    // A model set of a page's packs (loadModelSet): its models as stored, and for a skinned set
    // its bones and the vertices' weights.
    struct ModelSet {
        std::vector<std::unique_ptr<gt3r::render::GpuModel>> models; // by model index
        std::vector<gt3r::scene::Model> stored;                     // by model index (skinned sets)
        std::vector<mdls::Bone> bones;
        std::vector<mdls::Symbol> symbols;
        mdls::SkinTable skin;
        int skinned_model = -1; // the model drawn with the bones (symbol SKINNING)
    };
    Car* car(const std::string& code, int color);
    SDL_GPUTexture* env_texture(menu::Project* project, const std::string& page, const std::string& path);

    App& app_;
    std::unique_ptr<gt3r::render::View3D> view_;
    std::unique_ptr<gt3r::render::View3D> mirror_view_; // the cars' mirrored copies (`mirror`)
    SDL_Texture* mirror_target_ = nullptr; // its picture, added to the page
    std::map<std::string, std::unique_ptr<Car>> cars_; // by code#paint
    std::map<const menu::LiveWidget*, Face> faces_;
    u64 frame_ = 0;
    std::map<std::string, std::string> models_; // car label -> model file name
    std::map<u64, SDL_GPUTexture*> env_; // by image_key
    std::vector<gt3r::render::DrawItem> items_;
    void load_stage();
    // The stage's backdrops at their weights over the page, softened (0x1D5588, 0x1D5620, 0x1D56B0).
    void draw_backdrops(const std::vector<gt3r::render::DrawItem>& backdrops, const gt3r::render::Mat4& view, const gt3r::render::Mat4& proj,
                        const gt3r::render::Vec3& eye, const SDL_FRect& page_rect, float opacity);
    std::unique_ptr<gt3r::render::View3D> stage_view_;
    // The stage's reflection picture (menu/select/env.mdl) at its time: each model's sphere map made once on a worker, added
    // at the backdrops' weights (0x1D5AE8). Null until made.
    SDL_GPUTexture* stage_environment();
    using SphereMap = std::vector<std::array<float, 3>>;
    std::future<std::vector<SphereMap>> env_loading_;
    std::vector<SphereMap> env_maps_;
    SDL_GPUTexture* env_picture_ = nullptr;
    u64 env_made_ = 0; // the picture it was last made in
    SDL_Texture* stage_target_ = nullptr;         // the backdrops' picture
    SDL_Texture* focus_targets_[2] = {};          // its blurred half-size copies
    // The car-selection stage: its backdrops (menu/select/bg.mdl's models), camera and light
    // motions, and its time.
    std::vector<std::unique_ptr<gt3r::render::GpuModel>> stage_;
    std::unique_ptr<menu::StageMotion> camera_motion_, light_motion_;
    float stage_time_ = 0;
    u64 stage_stepped_ = 0; // the picture the stage's time was last stepped to
    void step_stage();
    // The stage's camera at its time (menu/select/camera.mot); false without one.
    bool stage_camera(const menu::FaceView& v, gt3r::render::Mat4& view, gt3r::render::Mat4& proj, gt3r::render::Vec3& eye) const;
    std::vector<gt3r::render::DrawItem> backdrops() const;
    bool stage_tried_ = false;
    // A face's camera motion and model sets (loadModelOptionMotion, loadModelSet), read from the
    // page's packs once (null: not there).
    const menu::StageMotion* option_motion(menu::Project* project, const std::string& page, const std::string& path);
    const ModelSet* model_set(menu::Project* project, const std::string& page, const std::string& path);
    const menu::SkeletonMotion* skeleton_motion(menu::Project* project, const std::string& page, const std::string& path);
    // The model sets drawn by their motions (loadModelMotion) into `out` (0x156AA0); `fade` their
    // opacity.
    void add_model_motions(Face& f, const menu::CarFaceSettings& s, const menu::CarMotion& m, menu::Project* project, const std::string& page,
                           const gt3r::render::Mat4& car, const race::CarSpec& spec, float fade, std::vector<gt3r::render::DrawItem>& out);
    std::map<std::string, std::unique_ptr<menu::StageMotion>> option_motions_;
    std::map<std::string, std::unique_ptr<ModelSet>> model_sets_;
    std::map<std::string, std::unique_ptr<menu::SkeletonMotion>> skeleton_motions_;
    std::array<float, 2> body_length(const std::string& code, const race::CarSpec& spec);
    std::map<std::string, std::array<float, 2>> body_lengths_; // car code -> metres ahead of and behind its middle
    // How far each axle's wheels can hang below the body while a jack raises it (metres; the car as
    // sold built as the program's showroom car, sim/showroom.h): {0, 0} when unknown.
    std::array<float, 2> wheel_droop(const std::string& code);
    std::map<std::string, std::array<float, 2>> droops_;
    struct Sim; // the program and the showroom car's builder (made when a lift first needs them)
    std::unique_ptr<Sim> sim_;
};

} // namespace gt4r
