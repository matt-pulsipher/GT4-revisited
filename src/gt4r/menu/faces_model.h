#pragma once

// A model face (the program's MModelFace): a model set of the page's pack (or of the disc) drawn
// in the face's rectangle with its camera and lights, turning as the scripts ask.
// docs/formats/menu.md, "Models".

#include "gt4r/menu/faces_car.h"

namespace gt4r::menu {

struct ModelFaceSettings {
    FaceView view;
    std::string model_set_path; // an entry of the page's pack, or a file of the disc
    bool from_disk = false;
    std::string env_image_path;
    float translate[3] = {0, 0, 0}, rotate[3] = {0, 0, 0}, rotate_v[3] = {0, 0, 0};
};
ModelFaceSettings model_face_settings(const Widget& face);

// The model's turning (the update at 0x2BD2E8): free spin by rotate_v degrees a picture, kept in
// 0..360, or with target_mode an ease back to the home rotation, 5 % of the shorter way a picture.
struct ModelSpin {
    float rotate[3] = {0, 0, 0}, home[3] = {0, 0, 0}, rotate_v[3] = {0, 0, 0};
    bool target_mode = false;
    bool stop_at_home = false; // stopRotate()
    void reset(const float home_rotate[3]);
    void step();
};

} // namespace gt4r::menu
