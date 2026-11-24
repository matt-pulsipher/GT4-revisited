#include "gt4r/menu/faces_car.h"

#include <algorithm>
#include <cmath>

namespace gt4r::menu {

namespace {

} // namespace

// A list of numbers as the layout writes it (`[3] { digit {0} ... }`) or a script set it.
int face_floats(const Widget& w, const char* name, float* out, int n) {
    const Property* p = w.find(name);
    if (!p) return 0;
    int i = 0;
    if (p->values.empty())
        for (const Property& item : p->items) {
            if (i >= n) break;
            if (!item.values.empty()) out[i++] = std::strtof(item.values[0].c_str(), nullptr);
        }
    else
        for (const std::string& v : p->values) {
            if (i >= n) break;
            out[i++] = std::strtof(v.c_str(), nullptr);
        }
    return i;
}

namespace {
void read_floats(const Widget& w, const char* name, float* out, int n) { face_floats(w, name, out, n); }
// A colour of the layout (0..255) as the program keeps it (0..1). ❓ The reader's exact scaling.
void read_color(const Widget& w, const std::string& name, float* out, int n) {
    if (const std::optional<Rgba> c = w.color(name)) {
        const float v[4] = {c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, c->a / 255.0f};
        for (int i = 0; i < n; ++i) out[i] = v[i];
    }
}

} // namespace

FaceView face_view(const Widget& w) {
    FaceView v;
    v.view_angle = w.number("view_angle", v.view_angle);
    v.near = w.number("near", v.near), v.far = w.number("far", v.far);
    read_floats(w, "camera_trans", v.camera_trans, 3);
    read_floats(w, "camera_rotate", v.camera_rotate, 3);
    read_color(w, "world_ambient", v.world_ambient, 3);
    v.perfect_dark = w.number("perfect_dark") != 0;
    for (int i = 0; i < 3; ++i) {
        FaceLight& l = v.lights[i];
        const std::string p = "l" + std::to_string(i) + "_";
        l.on = w.number(p + "enable", i == 0 ? 1.0f : 0.0f) != 0;
        read_floats(w, (p + "pos").c_str(), l.pos, 4);
        read_color(w, p + "amb", l.amb, 3);
        read_color(w, p + "dif", l.dif, 3);
        read_color(w, p + "spe", l.spe, 3);
        read_color(w, p + "sdw", l.sdw, 4);
    }
    return v;
}

CarFaceSettings car_face_settings(const Widget& w) {
    CarFaceSettings s;
    s.view = face_view(w);
    s.env_image = w.text("image_path");
    s.quality_level = int(w.number("quality_level", 1));
    read_floats(w, "trans", s.trans, 3);
    read_floats(w, "rotate", s.rotate, 3);
    s.mirror = w.number("mirror", 1) != 0;
    // The layout gives a number, the scripts "normal" / "advanced" (the table at 0x618890).
    const std::string action = w.text("action_type");
    s.advanced = action == "advanced" || (action != "normal" && w.number("action_type", 1) != 0);
    s.car_code = w.text("car_code");
    s.car_color = int(w.number("car_color", -1));
    s.option_motion = w.text("option_motion");
    s.option_motion_frame = (long long)w.number("option_motion_frame", 0);
    const std::string sets = w.text("model_sets");
    for (size_t at = 0; at < sets.size();) {
        const size_t end = std::min(sets.find('|', at), sets.size());
        if (end > at) s.model_sets.push_back(sets.substr(at, end - at));
        at = end + 1;
    }
    return s;
}

FaceProjection FaceProjection::of(float view_angle, const Rect& face) {
    FaceProjection p;
    p.cx = face.x + face.w / 2, p.cy = face.y + face.h / 2;
    const float d = face.h / 2 / std::tan(view_angle * 3.14159265f / 360.0f);
    p.fx = d * 448.0f / 480.0f; // a framebuffer pixel is a page unit across, 480/448 of one down
    p.fy = d / 1.098f;          // the NTSC pixel aspect
    return p;
}

void FaceProjection::tangents(float& left, float& right, float& down, float& up) const {
    left = -cx / fx, right = (640 - cx) / fx;
    up = cy / fy, down = (cy - 480) / fy;
}

void CarMotion::set_car(const float trans[3], const float rotate[3]) {
    for (int i = 0; i < 3; ++i) cur[i] = trans[i];
    cur_rx = rotate[0], yaw = rotate[1], cur_rz = rotate[2];
    spin_force = spin_velocity = 0;
    cur_alpha = 0, alpha = 1, alpha_speed = 0, alpha_step = 1.0f / 120;
}

void CarMotion::warp() {
    for (int i = 0; i < 3; ++i) cur[i] = t[i];
    cur_rx = r[0], cur_rz = r[2], yaw = r[1];
    spin_force = spin_velocity = 0;
}

void CarMotion::fadein(bool in) {
    alpha = in ? 1.0f : 0.0f;
    alpha_step = in ? 1.0f / 120 : -1.0f / 120;
    alpha_speed = 0;
}

void CarMotion::transparent_ratio(float a) { cur_alpha = alpha = a; }

void CarMotion::step() {
    cur_alpha += (alpha - cur_alpha) * alpha_speed;
    cur_alpha = std::clamp(cur_alpha + alpha_step, 0.0f, 1.0f);
    if (motion == 0) return;
    if (motion == 1) {
        // The turntable: a constant push against a drag, settling at 18.67 degrees a second.
        spin_force += 80;
        spin_velocity += spin_force / 60;
        spin_force = 0;
        spin_velocity -= spin_velocity * 4 / 60;
        yaw = std::fmod(yaw + spin_velocity / 60, 360.0f);
        if (yaw < 0) yaw += 360;
    } else if (motion == 2) yaw += (r[1] - yaw) * 0.2f;
    for (int i = 0; i < 3; ++i) cur[i] += (t[i] - cur[i]) * pos_speed;
    cur_rx += (r[0] - cur_rx) * rot_speed;
    cur_rz += (r[2] - cur_rz) * rot_speed;
}

CarModelObject::CarModelObject() : Object("MCarModel") {}

std::shared_ptr<CarModelObject> make_car_model_object() {
    auto o = std::make_shared<CarModelObject>();
    CarModelObject* self = o.get();
    using script::Value;
    auto number = [](const Value& v, float fallback) { return v.is_number() ? v.as_float() : fallback; };
    auto bind = [&](const char* name, float* field) {
        o->property(name, [field] { return Value::number(*field); }, [field, number](const Value& v) { *field = number(v, *field); });
    };
    CarMotion& m = o->m;
    o->property("motion", [&m] { return Value::integer(m.motion); }, [&m, number](const Value& v) { m.motion = int(number(v, float(m.motion))); });
    bind("tx", &m.t[0]), bind("ty", &m.t[1]), bind("tz", &m.t[2]);
    bind("rx", &m.r[0]), bind("ry", &m.r[1]), bind("rz", &m.r[2]);
    bind("alpha", &m.alpha);
    bind("posInterpolateSpeed", &m.pos_speed);
    bind("additionalModelScale", &m.model_scale);
    // The program's getter reads the rotation speed and its setter writes the position's.
    o->property("rotInterpolateSpeed", [&m] { return Value::number(m.rot_speed); }, [&m, number](const Value& v) { m.pos_speed = number(v, m.pos_speed); });
    o->property("alphaInterpolateSpeed", [&m] { return Value::number(m.alpha_speed); }, [&m, number](const Value& v) {
        m.alpha_speed = number(v, m.alpha_speed);
        m.alpha_step = 0;
    });
    o->property("independent", [&m] { return Value::integer(m.independent); }, [&m](const Value& v) { m.independent = v.truthy(); });
    o->property("fadein", [] { return Value{}; }, [&m](const Value& v) { m.fadein(v.truthy()); });
    o->property("transparentRatio", [] { return Value{}; }, [&m, number](const Value& v) { m.transparent_ratio(number(v, 1)); });
    o->method("warp", [self](script::Vm&, const Value&, script::Args) {
        self->m.warp();
        return Value{};
    });
    for (const char* f : {"playModelMotion", "stopModelMotion", "setModelMotionLoop"}) o->method(f, [](script::Vm&, const Value&, script::Args) { return Value{}; });
    o->method("isPlayingModelMotion", [](script::Vm&, const Value&, script::Args) { return Value::integer(0); });
    return o;
}

} // namespace gt4r::menu
