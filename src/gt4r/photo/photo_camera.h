#pragma once

// The photo shoot's state: the map's view, the camera and its lens, the car's place, and what the
// pad does to them in each of the page's modes (the program's PhotoModeStatus and
// PhotoCameraManager). docs/formats/photo.md "The camera".

#include "gt4r/photo/photo_course.h"
#include "render/math3d.h"

#include <functional>

namespace gt4r::photo {

// What the page's map window does (its `operate_mode`, in the program's order).
enum class Mode : int { None, Car, Camera, Pan, Roll, Orbit, Move, Climb, MapZoom };
// By its script name ("none", "car", ...); None for any other.
Mode mode_by_name(const std::string& name);
const char* mode_name(Mode m);

// The pad as the photo modes read it.
struct Pad {
    float x = 0, y = 0;  // the directions (-1, 0, 1) plus twice the left stick (right and down +)
    float turn = 0;      // the car's turn: the shoulder buttons' (-1, 0, 1) less twice the right stick's x
    int zoom = 0;        // +1 wider, -1 closer
    bool confirm = false, cancel = false;             // newly pressed
    bool confirm_held = false, cancel_held = false;
};
// A stick's byte (0 .. 255) as -1 .. 1 (0x4271F8).
float stick(int byte);

// The lens, as the program keeps it (status +0x48): whole steps.
struct Lens {
    int focal = 447;     // the focal length in tenths of a millimetre
    int aperture = 140;  // the f-number x 100
    float focus = -1;    // metres; not above 0: focused on the car by itself
    float film_w = 36, film_h = 24; // millimetres
    void set_focal(float mm) { focal = int(mm * 10.0f); }
    void set_aperture(float f) { aperture = int(f * 100.0f); }
    float f_number() const { return float(aperture) / 100.0f; }
    // The angle of view over the film's height, degrees (0x36F4E0).
    float angle() const;
};

// The car as the camera meets it: its place, axes and box.
struct CarBox {
    gt3r::render::Vec3 at;             // the body's origin
    gt3r::render::Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, -1};
    float front = 2, rear = 2, width = 1.8f, height = 1.3f; // metres from the origin ahead and behind; the whole width and height
    // Whether `p` is inside the box grown by the margins (0x3FDD60).
    bool holds(const gt3r::render::Vec3& p, float side, float top, float ends) const;
};

class PhotoCamera {
public:
    // The height of the ground under (x, z), looked for below `from`; false: none there.
    using Ground = std::function<bool(float x, float z, float from, float& y)>;

    // A new shoot on a location (0x36FAE8): the camera and the car where it puts them.
    PhotoCamera(const PhotoCourse& course, Ground ground);

    // The map window's size in page pixels (0x377F30): its base length becomes the one that fits.
    void set_window(float w, float h);
    // One picture (1/60 s) of a mode (0x377F60); `entered`: the mode was just taken up. True when a drag of the car or the
    // camera ended (confirmed or taken back).
    bool step(Mode mode, bool entered, const Pad& pad);
    // The lens as the page has it: the angle of view it gives here (0x370D08: a cell that limits the view narrows it).
    void apply_lens();

    const PhotoCourse& course() const { return course_; }
    Lens lens;
    CarBox car_box; // where the car stands now, as the caller places it
    // The camera.
    gt3r::render::Vec3 eye;
    float yaw = 0, pitch = 0, roll = 0; // degrees
    float angle = 60;                   // the view's, degrees over the picture's height
    int cell = -1;
    bool limited = false;               // the cell has targets: the headings the view is kept between
    float least = -360, greatest = 720;
    float ground = 0, low = 0, high = 100; // the ground under the eye, the eye's least height above it, its highest place
    float height = 1.3f;                // the eye above the ground
    gt3r::render::Mat4 view() const;    // course space -> the camera's
    // The car.
    Vec2 car;
    float car_heading = 0; // degrees
    int car_shape = -1;
    // Whether the car may be left where it is (0x370CC0), the camera where its pointer is (0x370950).
    bool car_allowed() const;
    bool camera_allowed() const { return pointed_cell_ >= 0; }
    // The camera is inside the car (0x36FE40): the car's box is drawn in the finder.
    bool inside_car() const;

    // The map: how much ground it shows, its middle; course places in its pixels.
    float base_length = 100;
    float least_length() const { return 20; }
    float most_length() const;           // 0x36FD98
    void set_base_length(float v) { base_length = v; }
    float center_x = 0, center_z = 0;
    float map_x(float x) const;          // 0x36FFC0
    float map_y(float z) const;          // 0x370010
    float window_w() const { return w_; }
    float window_h() const { return h_; }
    float span_x() const { return base_length * w_ / h_; } // metres the window shows across
    Vec2 pointer() const { return pointer_; } // where the dragged thing would be put
    Vec2 drag() const { return drag_; }       // the drag itself

private:
    void point(const Vec2& p, int cell);                // 0x370548
    void stand(int cell, const Vec2& p, bool turn);     // 0x371220
    void place_height(int cell);                        // 0x370DD0
    bool fit_angle(float yaw, float& angle, bool& cut); // 0x370EF8
    void clamp_yaw(float& angle);                       // 0x3710D8
    void follow(float x, float z);                      // 0x3704D0
    float follow_axis(bool x, float center, float at) const;
    float pixels_x(float n) const { return base_length * w_ / h_ * n / w_; } // metres of n pixels across (0x36FF38)
    float pixels_z(float n) const { return base_length * n / h_; }
    float ramp(bool moving);
    void zoom(int dir);
    void drag_by(const Pad& pad);
    void move(const Pad& pad);
    void pan(const Pad& pad);
    void roll_by(const Pad& pad);
    void climb(const Pad& pad);
    bool drag_step(Mode mode, bool entered, const Pad& pad); // 0x378778

    PhotoCourse course_;
    Ground ground_;
    float w_ = 128, h_ = 128, extent_x_ = 500, extent_z_ = 500;
    Vec2 pointer_, drag_;
    int pointed_cell_ = -1;
    int ramp_ = 0;
    int pan_ramp_ = 0, roll_rest_ = 0;
    int drag_state_ = 0; // 1 dragging, 2 put, 3 taken back (until the buttons are let go)
    BorderTrack track_;
    Vec2 car_saved_;
    float car_heading_saved_ = 0;
    int car_shape_saved_ = -1;
};

} // namespace gt4r::photo
