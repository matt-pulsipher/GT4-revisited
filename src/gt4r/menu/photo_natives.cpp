#include "gt4r/menu/photo_natives.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/jpeg.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

namespace {

std::shared_ptr<script::Reference> attribute(std::function<Value()> read, std::function<void(const Value&)> write = {}) {
    auto a = std::make_shared<Accessor>();
    a->read = std::move(read), a->write = std::move(write);
    return a;
}
std::shared_ptr<script::Reference> method(const std::string& name, script::NativeFunction f) {
    return std::make_shared<script::Variable>(Vm::native(name, std::move(f)));
}
float num(const Value& v) { return v.is_number() ? v.as_float() : 0.0f; }

const char* find_type(const LiveWidget& w, const char* type) { return w.is(type) ? type : nullptr; }

std::shared_ptr<LiveWidget> find_in(const std::shared_ptr<LiveWidget>& w, const char* type) {
    if (find_type(*w, type)) return w;
    for (const auto& c : w->children)
        if (auto f = find_in(c, type)) return f;
    return nullptr;
}

std::shared_ptr<script::Reference> map_member(Runtime& rt, const std::string& n) {
    Runtime* r = &rt;
    // (Without a shoot, a page only looked at: the attributes of a shoot of its own that nothing shows.)
    static PhotoShoot idle{photo::PhotoCourse{}, {}};
    PhotoShoot* s = rt.photo ? rt.photo.get() : &idle;
    auto shoot = [r] { return r->photo ? r->photo.get() : &idle; };
    (void)s;
    if (n == "operate_mode")
        return attribute([shoot] { return Value::string(photo::mode_name(shoot()->mode)); }, [shoot](const Value& v) { shoot()->mode = photo::mode_by_name(v.to_string()); });
    if (n == "view_angle") return attribute([shoot] { return Value::number(shoot()->view_angle); }, [shoot](const Value& v) { shoot()->view_angle = num(v); });
    if (n == "aperture") return attribute([shoot] { return Value::number(shoot()->aperture); }, [shoot](const Value& v) { shoot()->aperture = num(v); });
    if (n == "stand") return attribute([shoot] { return Value::integer(shoot()->stand); }, [shoot](const Value& v) { shoot()->stand = v.truthy(); });
    if (n == "wheel_angle") return attribute([shoot] { return Value::number(shoot()->wheel_angle); }, [shoot](const Value& v) { shoot()->wheel_angle = num(v); });
    if (n == "lightness") return attribute([shoot] { return Value::number(shoot()->filters.lightness); }, [shoot](const Value& v) { shoot()->filters.lightness = num(v); });
    if (n == "color_filter")
        return attribute([shoot] { return Value::string(photo::filter_name(shoot()->filters.color)); },
                         [shoot](const Value& v) { shoot()->filters.color = photo::filter_by_name(v.to_string()); });
    if (n == "brightness") return attribute([shoot] { return Value::integer(shoot()->filters.brightness); }, [shoot](const Value& v) { shoot()->filters.brightness = int(num(v)); });
    if (n == "contrast") return attribute([shoot] { return Value::integer(shoot()->filters.contrast); }, [shoot](const Value& v) { shoot()->filters.contrast = int(num(v)); });
    if (n == "whiteBalance")
        return attribute([shoot] { return Value::integer(shoot()->filters.white_balance); }, [shoot](const Value& v) { shoot()->filters.white_balance = int(num(v)); });
    // (Nothing writes it but the widget's making: always 1.)
    if (n == "can_shoot") return attribute([] { return Value::integer(1); }, [](const Value&) {});
    if (n == "setBaseLen") return method(n, [](Vm&, const Value&, Args) { return Value{}; }); // (0x194190: nothing)
    // getAperture(context, i): the program's i-th f-number (1 .. 16), set on the lens too (0x195270).
    if (n == "getAperture")
        return method(n, [shoot](Vm&, const Value&, Args a) {
            PhotoShoot& p = *shoot();
            const int i = std::clamp(arg_int(a, 1), 1, 16);
            float f = 1.4f * std::pow(2.0f, float(i - 1) * 4.0f / 15.0f); // (❓ a program whose table is not known)
            if (p.apertures.size() == 16) f = p.apertures[size_t(i - 1)];
            p.camera.lens.set_aperture(f);
            return Value::number(p.camera.lens.f_number());
        });
    // doFocus(context): the focus on what the picture's middle shows (0x1952D0).
    if (n == "doFocus")
        return method(n, [shoot](Vm&, const Value&, Args) {
            PhotoShoot& p = *shoot();
            p.camera.lens.focus = std::max(p.depth_at_middle, 0.01f);
            return Value{};
        });
    return nullptr;
}

// USB memory: the photos as plain JPEG files, img0001.jpg on, in the data folder's photos/ (the program's /GT4/img%04d.jpg).
Value out_usb(Runtime& rt) {
    if (!rt.photo_picture || rt.photo_picture->jpeg.empty() || rt.data_dir().empty()) return Value::string("ERROR");
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(rt.data_dir()) / "photos";
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int i = 1; i < 10000; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "img%04d.jpg", i);
        if (fs::exists(dir / name, ec)) continue;
        std::ofstream out(dir / name, std::ios::binary);
        const Bytes& j = rt.photo_picture->jpeg;
        out.write(reinterpret_cast<const char*>(j.data()), std::streamsize(j.size()));
        return Value::string(out ? "OK" : "ERROR");
    }
    return Value::string("ERROR");
}

std::shared_ptr<script::Reference> render_member(Runtime& rt, const std::string& n) {
    Runtime* r = &rt;
    auto picture = [r]() -> PhotoPicture& {
        if (!r->photo_picture) r->photo_picture = std::make_shared<PhotoPicture>();
        return *r->photo_picture;
    };
    if (n == "imageWidth") return attribute([picture] { return Value::integer(picture().width); });
    if (n == "imageHeight") return attribute([picture] { return Value::integer(picture().height); });
    if (n == "portrait") return attribute([] { return Value::integer(0); }); // (the pictures here are upright)
    if (n == "imageScale") return attribute([picture] { return Value::number(picture().scale); }, [picture](const Value& v) { picture().scale = num(v); });
    if (n == "ofs_x") return attribute([picture] { return Value::number(picture().ofs_x); }, [picture](const Value& v) { picture().ofs_x = num(v); });
    if (n == "ofs_y") return attribute([picture] { return Value::number(picture().ofs_y); }, [picture](const Value& v) { picture().ofs_y = num(v); });
    if (n == "isRendering" || n == "rendering") return method(n, [r](Vm&, const Value&, Args) { return Value::integer(r->photo && r->photo->rendering); });
    if (n == "allocateImageBuffer")
        return method(n, [r](Vm&, const Value&, Args) {
            r->photo_picture = std::make_shared<PhotoPicture>();
            return Value{};
        });
    // setPhotoFile(file): the picture the file's load read (game_photos.cpp keeps it on the file object).
    if (n == "setPhotoFile")
        return method(n, [r](Vm&, const Value&, Args a) {
            const auto file = a.empty() ? nullptr : a[0].as<Object>();
            if (!file) return Value{};
            photo_file_picture(*r, *file);
            return Value{};
        });
    if (n == "outUSBStorage") return method(n, [r](Vm&, const Value&, Args) { return out_usb(*r); });
    // A printer: none is connected (the page says so).
    if (n == "scanPrinter") return method(n, [](Vm&, const Value&, Args) { return Value::integer(0); });
    if (n == "printout" || n == "cleaning" || n == "nozzleCheck") return method(n, [](Vm&, const Value&, Args) { return Value::string("no_printer"); });
    if (n == "getPrinterProgress" || n == "getSupportPaper" || n == "getSupportMedia" || n == "isFuchinashi")
        return method(n, [](Vm&, const Value&, Args) { return Value::integer(0); });
    if (n == "getPrinterName") return method(n, [](Vm&, const Value&, Args) { return Value::string(""); });
    for (const char* nothing : {"printCancel", "setPrinterNotifyResult", "endPrinter", "setPaper", "setMedia", "setMargin", "setTriming", "setHighQuality", "setCopy"})
        if (n == nothing) return method(n, [](Vm&, const Value&, Args) { return Value{}; });
    return nullptr;
}

// The place a mark takes on the map, its middle at (x, y).
void put(LiveWidget& w, float x, float y) {
    const Rect r = w.data.rect("geometry").value_or(Rect{});
    w.set_property("x", Value::number(x - r.w * 0.5f)), w.set_property("y", Value::number(y - r.h * 0.5f));
}
void turn(const std::shared_ptr<LiveWidget>& w, float deg) {
    if (w) w->set_property("rotate", Value::number(deg));
}

// A mark past the map's edge is kept 16 pixels inside it and shows as an arrow (0x1941D8): the angle toward it.
bool keep_inside(float& x, float& y, float w, float h, float& angle) {
    float dx = x - w * 0.5f, dy = y - h * 0.5f;
    angle = photo::angle_deg(dx, dy);
    bool out = false;
    const float kx = std::fabs(dx / (w * 0.5f - 16));
    if (1 < kx) dx /= kx, dy /= kx, out = true;
    const float ky = std::fabs(dy / (h * 0.5f - 16));
    if (1 < ky) dx /= ky, dy /= ky, out = true;
    x = dx + w * 0.5f, y = dy + h * 0.5f;
    return out;
}

} // namespace

std::shared_ptr<script::Reference> photo_member(Runtime& rt, const std::shared_ptr<LiveWidget>& w, const std::string& name) {
    if (w->is("PhotoMapWindow")) return map_member(rt, name);
    if (w->is("PhotoRenderFace")) return render_member(rt, name);
    return nullptr;
}

std::shared_ptr<LiveWidget> photo_map_window(Runtime& rt) {
    for (const auto& p : rt.open_pages())
        if (auto w = find_in(p, "PhotoMapWindow")) return w;
    return nullptr;
}
std::shared_ptr<LiveWidget> photo_render_face(Runtime& rt) {
    for (const auto& p : rt.open_pages())
        if (auto w = find_in(p, "PhotoRenderFace")) return w;
    return nullptr;
}

void apply_photo_lens(PhotoShoot& s) {
    s.camera.lens.set_focal(s.view_angle);
    s.camera.lens.set_aperture(s.aperture);
    s.camera.apply_lens();
}

void step_photo_map(Runtime& rt, PhotoShoot& s) {
    const std::shared_ptr<LiveWidget> map = photo_map_window(rt);
    if (!map) return;
    photo::PhotoCamera& cam = s.camera;
    const Rect size = map->data.rect("geometry").value_or(Rect{0, 0, 254, 382});
    const std::shared_ptr<LiveWidget> zoom = map->child("zoom"), car = map->child("car"), camera = map->child("camera"), cursor = map->child("cursor"), spot = map->child("hotspot");
    auto set_bar = [](LiveWidget& bar, const char* name, float v) {
        bar.set_property(name, Value::number(v));
    };
    if (!s.window_set) {
        s.window_set = true;
        cam.set_window(size.w, size.h);
        if (zoom) {
            // The slider runs over the lengths the map may show (0x194148), at the one it shows (0x194198).
            for (const char* lo : {"min", "display_min"}) set_bar(*zoom, lo, cam.least_length());
            for (const char* hi : {"max", "display_max"}) set_bar(*zoom, hi, cam.most_length());
            for (const char* v : {"value", "current_value", "repeat_start"}) set_bar(*zoom, v, cam.base_length);
        }
        s.zoom_seen = cam.base_length;
        // The program draws the camera's mark, then the car's, the slider and the pointer (0x195488).
        if (camera && car) map->append(camera, car.get());
        // A mark on every spot: copies of the layout's.
        if (spot) {
            for (size_t i = 0; i < cam.course().cells.size(); ++i) {
                if (!cam.course().cells[i].spot) continue;
                const std::shared_ptr<LiveWidget> mark = spot->copy();
                mark->rename("hotspot" + std::to_string(i));
                map->append(mark, camera ? camera.get() : car.get());
            }
            spot->set_property("visible", Value::integer(0));
        }
    }
    s.aspect = photo::aspect_by_name(rt.game().option_text("photo_aspect"));

    // The pad, as the photo modes' input reads it (0x4272A0): not the menus' keys.
    const Runtime::Pad& p = rt.pad();
    const Runtime::Pad& was = rt.last_pad();
    auto held = [&p](u32 bit) { return (p.buttons & bit) != 0; };
    auto pressed = [&p, &was](u32 bits) { return (p.buttons & ~was.buttons & bits) != 0; };
    photo::Pad in;
    in.x = float(held(Runtime::Pad::kRight)) - float(held(Runtime::Pad::kLeft)) + 2 * p.sticks[2];
    in.y = float(held(Runtime::Pad::kDown)) - float(held(Runtime::Pad::kUp)) + 2 * p.sticks[3];
    in.turn = float(held(Runtime::Pad::kL2)) - float(held(Runtime::Pad::kR2)) - 2 * p.sticks[0];
    in.zoom = int(held(Runtime::Pad::kL1)) - int(held(Runtime::Pad::kR1));
    constexpr u32 kYes = Runtime::Pad::kCross | Runtime::Pad::kCircle, kNo = Runtime::Pad::kTriangle | Runtime::Pad::kSquare;
    in.confirm = pressed(kYes), in.cancel = pressed(kNo);
    in.confirm_held = held(kYes), in.cancel_held = held(kNo);

    const bool entered = s.mode != s.last_mode;
    const bool was_allowed = cam.camera_allowed();
    const float before = cam.base_length;
    const bool on_page = rt.current_page() == map->page();
    apply_photo_lens(s);
    const bool done = cam.step(on_page ? s.mode : photo::Mode::None, entered, in);
    if (zoom) {
        // The slider after the map, or the map after the slider (0x194470).
        const float v = zoom->data.number("value");
        if (std::fabs(cam.base_length - before) > 0.0001f) {
            zoom->set_property("value", Value::number(cam.base_length));
            s.zoom_seen = cam.base_length;
        } else if (std::fabs(v - s.zoom_seen) > 0.0001f) {
            cam.set_base_length(std::clamp(v, cam.least_length(), cam.most_length()));
            s.zoom_seen = v;
        }
    }
    // The finder off while the camera's pointer is where it may not stand.
    if (was_allowed != cam.camera_allowed()) rt.queue_call(*map, "onPhotoViewDisable", {rt.context(), Value::integer(!cam.camera_allowed())});

    // The marks: the car and the camera, dragged or standing; past the edge, arrows.
    float car_x = cam.map_x(cam.car.x), car_y = cam.map_y(cam.car.z), cam_x = cam.map_x(cam.eye.x), cam_y = cam.map_y(cam.eye.z);
    if (s.mode == photo::Mode::Car) {
        car_x = cam.map_x(cam.drag().x), car_y = cam.map_y(cam.drag().z);
        if (car) car->set_state(kInsensitive, !cam.car_allowed());
    } else if (s.mode == photo::Mode::Camera) {
        cam_x = cam.map_x(cam.drag().x), cam_y = cam.map_y(cam.drag().z);
        if (camera) camera->set_state(kInsensitive, !cam.camera_allowed());
    }
    auto mark = [&](const std::shared_ptr<LiveWidget>& w, float x, float y, float heading, bool dragged, const char* arrow) {
        if (!w) return;
        float toward = 0;
        const bool out = keep_inside(x, y, size.w, size.h, toward) && !dragged;
        const std::string flag = std::string("arrow_") + w->name;
        if (int(map->data.number(flag)) != int(out)) {
            map->set_property(flag, Value::integer(out));
            rt.queue_call(*map, arrow, {rt.context(), Value::integer(out)});
        }
        put(*w, x, y);
        const float deg = out ? toward + 90.0f : heading;
        turn(w->child("body"), deg), turn(w->child("shadow"), deg);
    };
    mark(car, car_x, car_y, -cam.car_heading, s.mode == photo::Mode::Car, "setCarArrow");
    mark(camera, cam_x, cam_y, cam.yaw, s.mode == photo::Mode::Camera, "setCameraArrow");
    if (cursor) {
        put(*cursor, cam.map_x(cam.pointer().x), cam.map_y(cam.pointer().z));
        cursor->set_property("visible", Value::integer(s.mode == photo::Mode::Car || s.mode == photo::Mode::Camera));
    }
    for (size_t i = 0; i < cam.course().cells.size(); ++i)
        if (const auto m = map->child("hotspot" + std::to_string(i))) {
            const photo::Vec2 at = cam.course().shapes[size_t(cam.course().cells[i].shape)].middle();
            put(*m, cam.map_x(at.x), cam.map_y(at.z));
            turn(m, cam.course().spot_heading(int(i)));
        }
    if (done) {
        if (car) car->set_state(kInsensitive, false);
        if (camera) camera->set_state(kInsensitive, false);
        rt.queue_call(*map, "onFocusOff", {rt.context()});
    }
    s.blink -= 0.03f;
    if (s.blink < 0) s.blink = 1;
    s.last_mode = s.mode;
}

} // namespace gt4r::menu
