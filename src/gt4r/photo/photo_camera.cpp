#include "gt4r/photo/photo_camera.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

namespace gt4r::photo {

namespace {

using gt3r::render::Vec3;

constexpr float kPi = 3.14159274f;
constexpr const char* kModes[] = {"none", "car", "camera", "pan", "roll", "orbit", "move", "climb", "map_zoom"};
constexpr float kZoomStep = 1.07f;
constexpr int kRamp = 24;

} // namespace

Mode mode_by_name(const std::string& name) {
    for (size_t i = 0; i < std::size(kModes); ++i)
        if (name == kModes[i]) return Mode(i);
    return Mode::None;
}
const char* mode_name(Mode m) { return kModes[size_t(m)]; }

float stick(int byte) {
    const float v = float(byte) - 128.0f;
    if (v < 0) return std::clamp((v + 8.0f) / 120.0f, -1.0f, 0.0f);
    return std::clamp((v - 8.0f) / 120.0f, 0.0f, 1.0f);
}

float Lens::angle() const { return std::atan(film_h / (2.0f * float(focal) / 10.0f)) * 114.591560f; }

bool CarBox::holds(const Vec3& p, float side, float top, float ends) const {
    const float reach = std::max({side, top, ends});
    const Vec3 d = p - at;
    if (gt3r::render::length(d) - reach > 5.0f) return false;
    const float x = gt3r::render::dot(d, right), y = gt3r::render::dot(d, up), z = gt3r::render::dot(d, forward);
    return std::fabs(x) <= width * 0.5f + side && y <= height + top && y >= 0 && z <= front + ends && z >= -(rear + ends);
}

PhotoCamera::PhotoCamera(const PhotoCourse& course, Ground ground) : course_(course), ground_(std::move(ground)) {
    Vec2 p = course_.camera_start;
    const int at = course_.cell_at(p, 0);
    if (at >= 0) stand(at, p, true);
    else eye = {p.x, 100, p.z};
    point({eye.x, eye.z}, at);
    extent_x_ = course_.view[2] - course_.view[0], extent_z_ = course_.view[3] - course_.view[1];
    if (extent_x_ <= 1) extent_x_ = 500;
    if (extent_z_ <= 1) extent_z_ = 500;
    base_length = std::min(extent_x_, extent_z_);
    car = course_.car_start, car_heading = course_.car_heading;
    car_shape = car_shape_saved_ = course_.car_shape_at(car, cell);
    car_saved_ = car, car_heading_saved_ = car_heading;
}

void PhotoCamera::set_window(float w, float h) {
    w_ = w, h_ = h;
    base_length = most_length();
}

float PhotoCamera::most_length() const { return extent_z_ / h_ < extent_x_ / w_ ? extent_z_ : extent_x_ * h_ / w_; }

float PhotoCamera::map_x(float x) const { return w_ * 0.5f + w_ * (x - center_x) / (base_length * w_ / h_); }
float PhotoCamera::map_y(float z) const { return h_ * 0.5f + h_ * (z - center_z) / base_length; }

void PhotoCamera::point(const Vec2& p, int at) { drag_ = pointer_ = p, pointed_cell_ = at; }

// The eye's height over the ground under it, between the cell's limits.
void PhotoCamera::place_height(int at) {
    float least_height = 0.1f, top = 500;
    if (at >= 0) least_height = std::max(course_.cells[size_t(at)].eye_low, 0.1f), top = course_.cells[size_t(at)].eye_high;
    float g = 0;
    if (!ground_ || !ground_(eye.x, eye.z, top + 3.0f, g)) g = 0;
    float y = g + height;
    if (top < y) y = top, height = top - g;
    if (y < g + least_height) y = g + least_height, height = least_height;
    eye.y = y;
    low = least_height, high = top, ground = g, cell = at;
}

void PhotoCamera::stand(int at, const Vec2& p, bool turn) {
    float a = lens.angle();
    eye = {p.x, 100, p.z};
    place_height(at);
    track_.reset(p);
    const Cell& c = course_.cells[size_t(at)];
    if (!c.targets.empty() || turn) yaw = wrap360(angle_deg(c.look.x - p.x, c.look.z - p.z) + 90.0f), pitch = roll = 0;
    clamp_yaw(a);
    angle = a;
}

// The heading kept so that the view stays between the cell's targets; the angle no wider than they are apart.
void PhotoCamera::clamp_yaw(float& a) {
    if (cell < 0) return;
    const auto [to, lo, hi] = course_.limits(cell, {eye.x, eye.z});
    a = std::min(a, std::max(hi - lo, 0.001f));
    const float half = a * 0.5f;
    auto within = [lo, hi](float v) { return lo <= v && v <= hi; };
    if (yaw < to && !within(yaw)) yaw += 360;
    if (to < yaw && !within(yaw)) yaw -= 360;
    if (yaw - half < lo) yaw = lo + half;
    if (hi < yaw + half) yaw = hi - half;
}

bool PhotoCamera::fit_angle(float heading, float& a, bool& cut) {
    cut = false;
    if (cell < 0) return true;
    const auto [to, lo, hi] = course_.limits(cell, {eye.x, eye.z});
    auto within = [lo, hi](float v) { return lo <= v && v <= hi; };
    if (heading < to && !within(heading)) heading += 360;
    if (to < heading && !within(heading)) heading -= 360;
    if (!within(heading)) heading = wrap360(to) + turn_deg(wrap360(heading), wrap360(to));
    if (heading - a * 0.5f < lo) a = (heading - lo) * 2, cut = true;
    if (hi < heading + a * 0.5f) a = (hi - heading) * 2, cut = true;
    bool ok = !(heading <= lo) && !(hi <= heading);
    if (a < 0.001f) ok = false;
    if (!ok) a = 0.001f;
    limited = course_.cells[size_t(cell)].targets.size() >= 2;
    least = lo, greatest = hi;
    return ok;
}

void PhotoCamera::apply_lens() {
    float a = lens.angle();
    bool cut = false;
    fit_angle(yaw, a, cut);
    angle = a;
}

float PhotoCamera::follow_axis(bool x, float center, float at) const {
    const float size = x ? w_ : h_;
    auto metres = [this, x](float px) { return x ? pixels_x(px) : pixels_z(px); };
    const float off = (x ? map_x(at) : map_y(at)) - size * 0.5f, margin = size * 0.2f;
    if (margin < std::fabs(off)) {
        if (std::fabs(off) - margin < 0.8f) center = off > 0 ? at - metres(margin) : at + metres(margin);
        else center += metres(off > 0 ? off - margin : off + margin) * 0.3f;
    }
    const float half = metres(size) * 0.5f, lo = course_.view[x ? 0 : 1], hi = course_.view[x ? 2 : 3];
    if (hi < center + half) center = hi - half;
    if (center - half < lo) {
        center = lo + half;
        if (hi < center + half) center = (lo + hi) * 0.5f;
    }
    return center;
}

void PhotoCamera::follow(float x, float z) {
    center_x = follow_axis(true, center_x, x);
    center_z = follow_axis(false, center_z, z);
}

float PhotoCamera::ramp(bool moving) {
    if (!moving) return ramp_ = 0, 0.0f;
    if (ramp_ < kRamp) ++ramp_;
    return float(ramp_) / float(kRamp);
}

void PhotoCamera::zoom(int dir) {
    float v = base_length;
    if (dir > 0) v *= kZoomStep;
    if (dir < 0) v /= kZoomStep;
    v = std::clamp(v, 20.0f, 1000.0f);
    base_length = std::min(v, most_length());
}

// The pointer by the pad: 6 pixels a picture, sped up over 24 pictures, inside the location's bounds.
void PhotoCamera::drag_by(const Pad& pad) {
    float x0 = course_.drag[0], z0 = course_.drag[1], x1 = course_.drag[2], z1 = course_.drag[3];
    if (x1 <= x0 || z1 <= z0) x0 = z0 = -500, x1 = z1 = 500;
    x0 = std::max(x0, course_.view[0]), x1 = std::min(x1, course_.view[2]);
    z0 = std::max(z0, course_.view[1]), z1 = std::min(z1, course_.view[3]);
    const float speed = ramp(pad.x != 0 || pad.y != 0);
    drag_.x += pad.x * speed * pixels_x(6);
    drag_.z += pad.y * speed * pixels_z(6);
    drag_.x = std::min(std::max(drag_.x, x0), x1);
    drag_.z = std::min(std::max(drag_.z, z0), z1);
    follow(drag_.x, drag_.z);
    pointer_ = drag_;
}

bool PhotoCamera::car_allowed() const { return car_shape >= 0 && !car_box.holds(eye, 0.04f, 0.04f, 0.05f); }

bool PhotoCamera::inside_car() const {
    const float zoom = std::tan(angle * 0.5f * kPi / 180.0f);
    const float near = 0.1f * (0.700208f / std::max(zoom, 1e-3f)); // (the near plane of a lens, camera.md)
    const float m = near * (1.0f + zoom) + 0.05f;
    return car_box.holds(eye, m, m, m);
}

bool PhotoCamera::drag_step(Mode mode, bool entered, const Pad& pad) {
    if (entered) {
        drag_state_ = 1;
        ramp_ = 0;
        if (mode == Mode::Camera) point({eye.x, eye.z}, cell);
        else {
            point(car, cell);
            car_saved_ = car, car_heading_saved_ = car_heading, car_shape_saved_ = car_shape;
        }
    }
    bool still = false, done = false;
    if (drag_state_ == 1) {
        if (pad.confirm) {
            still = true;
            if (mode == Mode::Camera) {
                if (camera_allowed()) stand(pointed_cell_, pointer_, false), drag_state_ = 2;
            } else if (car_allowed()) drag_state_ = 2;
        }
        if (pad.cancel) {
            still = true;
            drag_state_ = 3;
            if (mode == Mode::Camera) point({eye.x, eye.z}, cell);
            else {
                car = car_saved_, car_heading = car_heading_saved_, car_shape = car_shape_saved_;
                point(car, cell);
            }
        }
    } else if (drag_state_ > 0) {
        still = true;
        if (!pad.confirm_held && !pad.cancel_held) drag_state_ = 0, done = true;
    }
    if (still) {
        follow(drag_.x, drag_.z);
        return done;
    }
    if (mode == Mode::Camera) {
        zoom(pad.zoom);
        drag_by(pad);
        Vec2 p = pointer_;
        pointed_cell_ = course_.cell_at(p, pixels_z(24));
        pointer_ = p;
    } else {
        zoom(pad.zoom);
        car_heading = wrap360(car_heading + pad.turn * 2);
        drag_by(pad);
        car = pointer_;
        car_shape = course_.car_shape_at(car, cell);
    }
    return done;
}

void PhotoCamera::move(const Pad& pad) {
    const float a = yaw * kPi / 180.0f, c = std::cos(a), s = std::sin(a);
    const float side = pad.x * 0.133333f, back = pad.y * 0.133333f;
    const Vec2 from{eye.x, eye.z}, to{from.x + c * side - s * back, from.z + s * side + c * back};
    const Vec3 was = eye;
    const float full = lens.angle();
    const int at = cell;
    float fov = full;
    const Vec2 p = course_.move(at, from, to, track_);
    eye.x = p.x, eye.z = p.z;
    place_height(at);
    bool cut = false;
    if (!fit_angle(yaw, fov, cut) || cut) { // (a place the view would not fit from: not taken)
        eye = was;
        place_height(at);
        fov = full;
        fit_angle(yaw, fov, cut);
    }
    angle = fov;
    drag_ = {eye.x, eye.z};
    follow(drag_.x, drag_.z);
}

void PhotoCamera::pan(const Pad& pad) {
    const float k = angle / 45.0f, dx = pad.x * 2 * k, dy = pad.y * 2 * k;
    const float r = -roll * kPi / 180.0f, s = std::sin(r), c = std::cos(r);
    // The stick's way turned with the finder.
    float up = dx * -s + dy * c, across = dx * c + dy * s;
    if (up == 0 && across == 0) pan_ramp_ = 0;
    else {
        if (pan_ramp_ < 22) ++pan_ramp_;
        const float f = float(pan_ramp_) / 22.0f;
        across *= f, up *= f;
    }
    yaw = wrap360(yaw + across);
    pitch = std::clamp(pitch + up, -30.0f, 45.0f);
    float a = angle;
    clamp_yaw(a);
    drag_ = {eye.x, eye.z};
    follow(drag_.x, drag_.z);
}

void PhotoCamera::roll_by(const Pad& pad) {
    const float d = pad.x * 2;
    if (d == 0) roll_rest_ = 0;
    else if (roll == 0) { // level: it stays there for nine pictures
        if (roll_rest_ > 0) --roll_rest_;
        else roll = std::clamp(d, -90.0f, 90.0f);
    } else {
        const float now = std::clamp(roll + d, -90.0f, 90.0f);
        if (now < 0 ? roll > 0 : now > 0 ? roll < 0 : true) roll = 0, roll_rest_ = 9;
        else roll = now, roll_rest_ = 0;
    }
    drag_ = {eye.x, eye.z};
    follow(drag_.x, drag_.z);
}

void PhotoCamera::climb(const Pad& pad) {
    height -= pad.y * 0.0666667f;
    place_height(cell);
    drag_ = {eye.x, eye.z};
    follow(drag_.x, drag_.z);
}

bool PhotoCamera::step(Mode mode, bool entered, const Pad& pad) {
    switch (mode) {
    case Mode::Car:
    case Mode::Camera: return drag_step(mode, entered, pad);
    case Mode::Pan:
        if (entered) pan_ramp_ = 0;
        pan(pad);
        break;
    case Mode::Roll:
        if (entered) roll_rest_ = 0;
        roll_by(pad);
        break;
    case Mode::Orbit:
    case Mode::Climb: climb(pad); break;
    case Mode::Move: move(pad); break;
    default: follow(drag_.x, drag_.z); break;
    }
    if (mode == Mode::MapZoom && pad.zoom) zoom(pad.zoom);
    return false;
}

gt3r::render::Mat4 PhotoCamera::view() const {
    const float d = kPi / 180.0f;
    return gt3r::render::rotate_z(roll * d) * gt3r::render::rotate_x(pitch * d) * gt3r::render::rotate_y(yaw * d) * gt3r::render::translate(-eye);
}

} // namespace gt4r::photo
