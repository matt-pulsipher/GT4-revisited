#include "gt4r/menu/faces_model.h"

#include <cmath>

namespace gt4r::menu {

ModelFaceSettings model_face_settings(const Widget& w) {
    ModelFaceSettings s;
    s.view = face_view(w);
    s.model_set_path = w.text("model_set_path");
    s.from_disk = w.number("model_set_from_disk") != 0;
    s.env_image_path = w.text("env_image_path");
    face_floats(w, "model_translate", s.translate, 3);
    face_floats(w, "model_rotate", s.rotate, 3);
    face_floats(w, "model_rotate_v", s.rotate_v, 3);
    return s;
}

void ModelSpin::reset(const float home_rotate[3]) {
    for (int i = 0; i < 3; ++i) rotate[i] = home[i] = home_rotate[i];
}

void ModelSpin::step() {
    if (target_mode) {
        for (int i = 0; i < 3; ++i) {
            float diff = std::fmod(home[i] - rotate[i], 360.0f);
            if (diff > 180) diff -= 360;
            if (diff < -180) diff += 360;
            rotate[i] += diff * 0.05f;
        }
        return;
    }
    bool spinning = false, near_home = true;
    for (int i = 0; i < 3; ++i) {
        if (rotate_v[i] == 0) continue;
        spinning = true;
        rotate[i] += rotate_v[i];
        while (rotate[i] < 0) rotate[i] += 360;
        while (rotate[i] > 360) rotate[i] -= 360;
        if (std::fabs(home[i] - rotate[i]) > 1.0f) near_home = false;
    }
    if (stop_at_home && (!spinning || near_home)) {
        for (int i = 0; i < 3; ++i)
            if (rotate_v[i] != 0) rotate[i] = home[i], rotate_v[i] = 0;
        stop_at_home = false;
    }
}

} // namespace gt4r::menu
