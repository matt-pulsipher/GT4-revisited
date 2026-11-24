#pragma once

// The model faces of the running menus (MModelFace): each one's model set drawn with its camera
// and lights where its rectangle is, turning as the scripts ask. docs/formats/menu.md, "Models".

#include "gt4r/menu/faces_model.h"
#include "gt4r/menu/project.h"
#include "render/gpu_model.h"
#include "render/view3d.h"

#include <SDL3/SDL_render.h>

#include <map>
#include <memory>
#include <string>

namespace gt4r {

class App;

class MenuModels {
public:
    explicit MenuModels(App& app);
    ~MenuModels();

    // `frame`: the menus' picture count (Runtime::frame()): the motions step once a picture.
    void begin_frame(u64 frame);
    void end_frame();
    void draw(const menu::LiveWidget& face, menu::Project* project, const std::string& page, const menu::Rect& at, const SDL_FRect& dst, float opacity);
    size_t shown() const { return faces_.size(); }

private:
    struct Face {
        std::weak_ptr<script::Heap> widget;
        std::string path;
        menu::ModelSpin spin;
        float seen_v[2][3] = {}; // rotate_v and model_rotate_v as last read: the newer one counts
        bool seen = false;
        u64 stepped = 0; // the picture its motion was last stepped to
    };
    const gt3r::render::GpuModel* model(menu::Project* project, const std::string& page, const menu::ModelFaceSettings& s);
    SDL_GPUTexture* env_texture(menu::Project* project, const std::string& page, const std::string& path);

    App& app_;
    std::unique_ptr<gt3r::render::View3D> view_;
    std::map<std::string, std::unique_ptr<gt3r::render::GpuModel>> models_; // by page and path (null: did not load)
    std::map<const menu::LiveWidget*, Face> faces_;
    u64 frame_ = 0;
    std::map<u64, SDL_GPUTexture*> env_; // by image_key
    std::vector<gt3r::render::DrawItem> items_;
};

} // namespace gt4r
