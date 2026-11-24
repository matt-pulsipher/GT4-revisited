#pragma once

// A car face (the program's MCarFace) and its car model (MCarModel): the view the layout gives
// it and how the car moves on it. The drawing is the app's (app/menu_cars.*);
// docs/formats/menu.md, "Cars".

#include "gt4r/menu/layout.h"
#include "gt4r/menu/native.h"

#include <memory>
#include <string>
#include <vector>

namespace gt4r::menu {

struct LiveWidget;

// A list of numbers of a face (`camera_trans [3] { digit {0} ... }`, or as a script set it):
// how many were read into `out`.
int face_floats(const Widget& w, const char* name, float* out, int n);

// The layout's view of a car face (and of a model face: the camera and lights are the same
// readers, 0x201FA8 and 0x210988 of the US program), with the program's defaults.
struct FaceLight {
    bool on = false;
    float pos[4] = {0, 1, 0, 0}; // w 0: a direction toward the light, world space
    float amb[3] = {0.3f, 0.3f, 0.3f}, dif[3] = {0.5f, 0.5f, 0.5f}, spe[3] = {0.5f, 0.5f, 0.5f};
    float sdw[4] = {0, 0, 0, 1}; // light 0's: the colour of the car's shadow
};
struct FaceView {
    float view_angle = 50; // degrees across the face's height
    float near = 0.1f, far = 32;
    float camera_trans[3] = {0, 0, 4};
    float camera_rotate[3] = {0, 0, 0}; // degrees
    float world_ambient[3] = {0, 0, 0};
    FaceLight lights[3];
    bool perfect_dark = false; // no light at all
};
FaceView face_view(const Widget& face);

struct CarFaceSettings {
    FaceView view;
    std::string env_image; // image_path: what the car reflects
    int quality_level = 1;
    float trans[3] = {0, 0, 0}, rotate[3] = {0, 0, 0}; // where a car set on it starts
    bool mirror = true;    // its mirrored copy under it
    bool advanced = true;  // action_type "advanced" (the car-selection stage) or "normal"
    std::string car_code;
    int car_color = -1;
    // loadModelOptionMotion(path): a camera motion of the page's packs (a Mot1 file like the
    // stage's camera.mot) that places the camera from the picture it was given at;
    // loadModelSet(path): more models of the packs drawn with the car (a mechanic, a shadow).
    std::string option_motion;
    long long option_motion_frame = 0;
    std::vector<std::string> model_sets;
};
CarFaceSettings car_face_settings(const Widget& face);

// Where the face's camera puts a point of the eye's space on the page (640 x 480 units): the
// program's projection (0x217058) for the face's rectangle. The picture's middle is the
// rectangle's middle; the whole page is the viewport.
struct FaceProjection {
    float cx = 320, cy = 240; // the rectangle's middle
    float fx = 1, fy = 1;     // page units per unit of x / -z and y / -z
    // NTSC: the framebuffer's 448 lines shown over the 480 units and its pixel aspect 1/1.098.
    static FaceProjection of(float view_angle, const Rect& face);
    // The tangents of the page's edges as seen from the eye (left, right, down, up).
    void tangents(float& left, float& right, float& down, float& up) const;
};

// The car's placement and motion (MCarModel; the per-picture update at 0x153BB8).
struct CarMotion {
    int motion = 0;                    // 0 still, 1 turntable, 2 follow ry
    float t[3] = {0, 0, 0}, r[3] = {0, 0, 0}; // targets: metres, degrees
    float cur[3] = {0, 0, 0};
    float cur_rx = 0, cur_rz = 0;
    float yaw = 0, spin_force = 0, spin_velocity = 0; // degrees, the turntable's
    float alpha = 1, cur_alpha = 0, alpha_speed = 0, alpha_step = 1.0f / 120;
    float pos_speed = 0.1f;
    float rot_speed = 0.1f; // the program's setter of rotInterpolateSpeed writes pos_speed
    float model_scale = 1;
    bool independent = false;

    // A car set on the face: placed where the layout says, faded in over 120 pictures.
    void set_car(const float trans[3], const float rotate[3]);
    void warp();
    void fadein(bool in);
    void transparent_ratio(float a);
    // One picture (1/60 s).
    void step();
};

// The script object `face.car_model` around a CarMotion.
struct CarModelObject : Object {
    CarModelObject();
    CarMotion m;
};
std::shared_ptr<CarModelObject> make_car_model_object();

} // namespace gt4r::menu
