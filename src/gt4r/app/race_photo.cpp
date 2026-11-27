#include "gt4r/app/race_photo.h"

#include "base/config.h"
#include "base/log.h"
#include "gt4r/app/race_hud.h"
#include "gt4r/audio/app_sound.h"
#include "gt4r/menu/game.h"
#include "gt4r/race/strobe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r {

namespace render = gt3r::render;
using RVec = render::Vec3;

namespace {

constexpr float kPi = 3.14159274f;
constexpr float kAspect = 4.0f / 3.0f; // the manager's screen in the pause (vf25 1.33333)
// The finder on the 640 x 480 picture (0x3C5AB8): 410 x 308 at (20, 20); upright, turned a quarter about (328, 20).
constexpr SDL_FRect kFinder{20, 20, 410, 308}, kFinderUpright{20, 20, 308, 410};

} // namespace

RacePhotoPause::RacePhotoPause(App& app, RaceHud* hud, Race race, const race::photo::ShotScene& scene, const menu::GameState* game)
    : app_(app), hud_(hud), race_(std::move(race)) {
    shots_ = race::photo::make_shots(scene);
    camera_.begin(shots_[0], kAspect);
    if (game) {
        saturation_option_ = float(game->option_int("photo_saturation"));
        wide_ = game->option_int("wide_mode") != 0;
        quality_ = photo::jpeg_quality(game->option_text("photo_quality"));
        if (audio::AppSound* sound = app_.sound()) sound->library_ptr()->set_shutter(gt3r::to_lower(game->option_text("photo_shutter")));
    }
    LOG_INFO("Photo pause: %zu cameras (%zu roadside), the replay's first", shots_.size(), scene.points.size());
    open_movie();
}

namespace {

// The step of a setting's table nearest a value.
size_t nearest(const float* values, size_t n, float v) {
    size_t best = 0;
    for (size_t i = 1; i < n; ++i)
        if (std::fabs(values[i] - v) < std::fabs(values[best] - v)) best = i;
    return best;
}

} // namespace

RacePhotoPause::RacePhotoPause(App& app, RaceHud* hud, Race race, const photo::film::Film& film, const menu::GameState* game)
    : app_(app), hud_(hud), race_(std::move(race)), develop_(true) {
    if (game) quality_ = photo::jpeg_quality(game->option_text("photo_quality"));
    const photo::film::Photo& ph = film.photo;
    // The camera of the film's last frame: its eye, the way to its target, the roll its up gives; the angle over the picture's
    // height from the projection's (2 atan(tan(fov / 2) x aspect)); the settings by their steps.
    auto shot_of = [&](const photo::film::Camera& c) {
        race::photo::Shot s;
        s.eye = {c.eye.x, c.eye.y, c.eye.z};
        const RVec d{c.target.x - c.eye.x, c.target.y - c.eye.y, c.target.z - c.eye.z};
        s.yaw = race::photo::yaw_of(d), s.pitch = race::photo::pitch_of(d);
        const float k = kPi / 180.0f;
        const render::Mat4 v = render::rotate_x(s.pitch * k) * render::rotate_y(s.yaw * k);
        const RVec up = v.transform_point({c.up.x, c.up.y, c.up.z});
        s.turn_roll = std::atan2(up.x, up.y) / k;
        s.fov = 2 * std::atan(std::tan(c.fov * 0.5f * k) * c.aspect) / k;
        s.fov_min = 0.1f, s.fov_max = 179, s.fov_min_set = true;
        s.distance = s.focus = std::max(render::length(d), 0.1f);
        float steps[21];
        for (size_t i = 0; i < race::photo::kApertures.size(); ++i) steps[i] = float(race::photo::kApertures[i]) / 100.0f;
        s.aperture = int(nearest(steps, race::photo::kApertures.size(), c.f_number));
        for (size_t i = 0; i < race::photo::kShutters.size(); ++i) steps[i] = race::photo::shutter_seconds(race::photo::kShutters[i]);
        s.shutter = int(nearest(steps, race::photo::kShutters.size(), c.shutter));
        s.exposure = int(nearest(race::photo::kExposures.data(), race::photo::kExposures.size(), ph.exposure));
        s.balance = int(nearest(race::photo::kBalances.data(), race::photo::kBalances.size(), ph.kelvin));
        s.portrait = ph.upright;
        return s;
    };
    shots_ = {shot_of(film.frames.back().camera)};
    camera_.begin(shots_[0], kAspect);
    if (film.frames.size() > 1) {
        const race::photo::Shot first = shot_of(film.frames.front().camera);
        pan_from_ = Pan{first.eye, first.yaw + first.turn_yaw, first.pitch + first.turn_pitch, first.fov};
    }
    filter_ = ph.filter;
    saturation_option_ = (ph.saturation - 1) * 10;
    if (rd32(ByteSpan(ph.frame.data(), ph.frame.size()), 4) == 3) {
        photo::film::Reader r(ByteSpan(ph.frame.data(), ph.frame.size()), 8);
        menu::JpegPicture::Focus q;
        q.x = r.f32_(), q.y = r.f32_(), q.x0 = r.f32_(), q.y0 = r.f32_(), q.x1 = r.f32_(), q.y1 = r.f32_();
        develop_frame_ = q;
    }
    const int w = int(ph.tile_w) * int(ph.tiles_x), h = int(ph.tile_h) * int(ph.tiles_y);
    exposure_.begin(ph.upright ? h : w, ph.upright ? w : h, correction());
    samples_ = photo::samples(photo::kSamples); // (the film's 32)
    state_ = State::Exposing;
    LOG_INFO("Photo: a film developed (%s, %zu cars, %zu frames): f = %d mm, F%.1f, 1/%d s, %+.1f EV, %.0f K, filter %d", film.course.c_str(), film.cars.size(),
             film.frames.size(), camera_.focal_mm(), double(camera_.f_number()), int(1.0f / camera_.shutter_seconds() + 0.5f), double(camera_.exposure()),
             double(camera_.kelvin()), filter_);
}

RacePhotoPause::~RacePhotoPause() = default;

void RacePhotoPause::play(const char* sound) {
    if (audio::AppSound* s = app_.sound()) s->menu().play(sound);
}

// The movie of the pause's words and values (0x3C4640 loads it, 0x3C46C8 plays it without the pad: its +0x10 0); what it asks of
// the program (0x3C40C0).
void RacePhotoPause::open_movie() {
    if (!movie_) {
        try {
            if (const std::optional<Bytes> file = app_.vfs()->read("menu/common/racephoto.strb"); file && race::StrobeMovie::is_movie(*file))
                movie_ = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(*file));
        } catch (const std::exception& e) {
            LOG_WARN("Photo pause: menu/common/racephoto.strb: %s", e.what());
        }
    }
    if (!movie_) return;
    auto host = [this](const std::string& name, const std::vector<race::StrobePlayer::Value>&) {
        using V = race::StrobePlayer::Value;
        std::string n = name;
        for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
        char buf[48];
        if (n == "GETZOOM") return std::snprintf(buf, sizeof buf, "f = %dmm", camera_.focal_mm()), V::string(buf);
        if (n == "GETIRIS") return std::snprintf(buf, sizeof buf, "F %.1f", double(camera_.f_number())), V::string(buf);
        if (n == "GETSHUTTER") return std::snprintf(buf, sizeof buf, "1/%d''", int(1.0f / camera_.shutter_seconds() + 0.5f)), V::string(buf);
        if (n == "GETEXPOSURE") {
            const float ev = camera_.exposure();
            std::snprintf(buf, sizeof buf, "%c%.1f EV", ev < 0 ? '-' : '+', double(std::fabs(ev)));
            return V::string(buf);
        }
        if (n == "GETCURCAM") return V::string(std::to_string(index_ + 1));
        if (n == "GETNCAM") return V::string(std::to_string(shots_.size()));
        if (n == "ISPORTRAIT") return V::number(camera_.portrait() ? 1 : 0);
        if (n == "ISFOCUSTRIGGER") {
            if (!focus_told_) return V::number(0);
            focus_told_ = false; // (the sound once for the press)
            play("zoom");
            return V::number(1);
        }
        if (n == "GETFILTER") { // (the white balance's side of 5,500 K)
            const float k = camera_.kelvin();
            return V::number(k < 5490 ? 0 : 5510 < k ? 2 : 1);
        }
        if (n == "GETMF") return V::number(camera_.manual_focus() ? 1 : 0);
        if (n == "ISWIDE") return V::number(0); // (the canvas is 4:3 here whatever the wide_mode option)
        if (n == "GETBLINK") return V::number(blink_);
        return V{};
    };
    player_ = std::make_unique<race::StrobePlayer>(movie_, host);
}

void RacePhotoPause::update(const MenuInput& m, const gt3r::PadState& pad, double dt) {
    if (state_ != State::Finder) return;
    race::photo::PadButtons b;
    b.l1 = m.held(PadButton::L1), b.l2 = m.held(PadButton::L2), b.r1 = m.held(PadButton::R1), b.r2 = m.held(PadButton::R2);
    // (the direction buttons themselves: the menus' keys count the left stick as them, which turns the camera here)
    b.up = pad.down(PadButton::Up), b.down = pad.down(PadButton::Down);
    b.circle = m.pressed(PadButton::Circle), b.triangle = m.pressed(PadButton::Triangle), b.square = m.pressed(PadButton::Square), b.cross = m.pressed(PadButton::Cross);
    // The sticks as the race input reads their bytes (0x43BAC8), down positive.
    auto stick = [&](gt3r::PadAxis a) {
        const int v = pad.axis(a);
        return float(v - (v < 128 ? 127 : 128)) / 127.0f;
    };
    b.left_x = stick(gt3r::PadAxis::LX), b.left_y = stick(gt3r::PadAxis::LY), b.right_x = stick(gt3r::PadAxis::RX), b.right_y = stick(gt3r::PadAxis::RY);
    // The direction buttons as they repeat (the menus' port: ❓ its times; 0.4 s, then every 0.1 s 🔎).
    const PadButton dirs[2][2] = {{PadButton::Up, PadButton::Down}, {PadButton::Left, PadButton::Right}};
    int repeat[2] = {};
    for (int a = 0; a < 2; ++a) {
        const int d = pad.down(dirs[a][1]) ? 1 : pad.down(dirs[a][0]) ? -1 : 0;
        if (d == 0) repeat_dir_[a] = 0;
        else if (d != repeat_dir_[a]) repeat_dir_[a] = d, repeat_time_[a] = 0, repeat[a] = d;
        else if ((repeat_time_[a] += dt) >= 0.4) repeat_time_[a] -= 0.1, repeat[a] = d;
    }
    b.vertical_repeat = repeat[0], b.horizontal_repeat = repeat[1];
    const bool select = m.pressed(PadButton::Select);
    time_ += dt;
    const int steps = std::min(int(time_ * 60.0), 4);
    time_ = steps == 4 ? 0.0 : time_ - steps / 60.0;
    for (int i = 0; i < steps && state_ == State::Finder; ++i) {
        if (i == 0 && select && leaving_ == 0) leaving_ = 1;
        step(b);
        // (what went down counts once)
        b.circle = b.triangle = b.square = b.cross = false, b.vertical_repeat = b.horizontal_repeat = 0;
    }
}

// A step of the pause (PhotoPause::vf4 0x3C4A48).
void RacePhotoPause::step(const race::photo::PadButtons& b) {
    focus_told_ = false;
    // Select: two steps more, then the replay's own camera again (0x3C49C8).
    if (leaving_ > 0) {
        if (++leaving_ >= 3) state_ = State::Closed;
        return;
    }
    if (player_) player_->advance(1.0f / 12.0f); // (0x475488 with 0.0833)
    // Triangle: the color filter (+0x1A8): none, Mature (saturation x 0.6), black and white.
    if (b.triangle) filter_ = (filter_ + 1) % 3;
    blink_ = race::photo::blinking(b);
    const race::photo::Pad p = race::photo::read_pad(b);
    // Left / Right: the list's next or last camera, round (0x400408), its own turn and settings as it was left.
    if (p.camera && !shots_.empty()) {
        index_ = size_t((int(index_) + p.camera + int(shots_.size())) % int(shots_.size()));
        camera_.begin(shots_[index_], kAspect);
    }
    const bool found = camera_.shot().fov_min_found;
    const race::photo::ShotCamera::Events e = camera_.step(p, race_.world);
    if (!found && camera_.shot().fov_min_found)
        LOG_INFO("Photo pause: camera %zu: the zoom stops at %.1f degrees (the eye by the car, 0x380C50)", index_ + 1, double(camera_.shot().fov_min));
    if (e.refused) play("disable");
    focus_told_ = e.focused;
    if (e.shutter) {
        // L1 + Cross on a focused place: the camera carried with the car over the step (0x37FA38), the photo's pictures between.
        pan_from_.reset();
        if (e.on_point && race_.car_motion)
            if (const std::optional<render::Mat4> moved = race_.car_motion()) {
                const Pan from{camera_.eye(), camera_.yaw(), camera_.pitch(), camera_.fov()};
                if (camera_.recompose(*moved)) pan_from_ = from;
            }
        // The shutter (0x3C4D28): the movie gone while the 32 pictures are drawn, a picture a frame.
        play("shutter");
        player_.reset();
        const bool upright = camera_.portrait();
        exposure_.begin(upright ? 960 : 1280, upright ? 1280 : 960, correction());
        samples_ = photo::samples(photo::kSamples);
        state_ = State::Exposing;
    }
}

photo::film::Film RacePhotoPause::film() const {
    photo::film::Film f;
    photo::film::Photo& p = f.photo;
    p.upright = camera_.portrait();
    p.shutter_frames = std::min(1.0f, camera_.shutter_seconds() * 60.0f);
    p.filter = filter_;
    p.saturation = saturation_option_ / 10.0f + 1;
    p.exposure = camera_.exposure(), p.kelvin = camera_.kelvin();
    // The car's frame in the picture: the JPEG's G4PI record {u32 1, u32 3, f32 x, y, x0, y0, x1, y1} (none: 0).
    if (picture_ && picture_->focus) {
        const menu::JpegPicture::Focus& q = *picture_->focus;
        photo::film::Writer w;
        w.u32_(1), w.u32_(3);
        for (float v : {q.x, q.y, q.x0, q.y0, q.x1, q.y1}) w.f32_(v);
        std::copy(w.data().begin(), w.data().end(), p.frame.begin());
    }
    // The cameras (0x33AEA0): the eye, a point the focus distance (at least 1 m) along the view, the view's up; the angle across a
    // 4:3 picture's width, the projection's scales' ratio, its planes, the settings.
    auto camera = [&](const race::photo::Vec3& eye, const render::Mat4& view) {
        photo::film::Camera c;
        const RVec ahead{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)}, up{view.at(1, 0), view.at(1, 1), view.at(1, 2)};
        const float d = std::max(camera_.focus(), 1.0f);
        c.eye = {eye.x, eye.y, eye.z}, c.target = {eye.x + ahead.x * d, eye.y + ahead.y * d, eye.z + ahead.z * d}, c.up = {up.x, up.y, up.z};
        c.fov = 2 * std::atan(std::tan(camera_.fov() * 0.5f * kPi / 180.0f) / 0.75f) * 180.0f / kPi;
        c.aspect = 0.75f;
        c.near = race::lens_near(camera_.fov() * kPi / 180.0f), c.far = 5000;
        c.f_number = camera_.f_number(), c.shutter = camera_.shutter_seconds(), c.exposure = camera_.exposure(), c.kelvin = camera_.kelvin();
        return c;
    };
    photo::film::Frame first, second;
    second.camera = camera(camera_.eye(), camera_.view());
    if (pan_from_) {
        const float d = kPi / 180.0f;
        const render::Mat4 v = render::rotate_z(camera_.roll() * d) * render::rotate_x(pan_from_->pitch * d) * render::rotate_y(pan_from_->yaw * d) * render::translate(-pan_from_->eye);
        first.camera = camera(pan_from_->eye, v);
    } else {
        first.camera = second.camera;
    }
    f.frames = {first, second};
    return f;
}

void RacePhotoPause::back_from_saving() {
    if (state_ != State::Saved) return;
    state_ = State::Finder;
    picture_.reset();
    open_movie();
}

// The finder's color correction (0x3C58C0 -> 0x3EFC10): a gain of 2^EV, the white balance against 5,500 K, the saturation of the
// option photo_saturation (1 + it / 10), then the filter's (x 0.6, or 0).
photo::Correction RacePhotoPause::correction() const {
    photo::Correction c;
    const float gain = std::pow(2.0f, camera_.exposure());
    const float shift = (camera_.kelvin() - 5500.0f) * 0.2f / 2000.0f;
    c.gamma[0] = gain * (1 - shift), c.gamma[1] = gain, c.gamma[2] = gain * (1 + shift);
    c.white = 1;
    c.saturation = saturation_option_ / 10.0f + 1;
    if (filter_ == 1) c.saturation *= 0.6f;
    if (filter_ == 2) c.saturation = 0;
    return c;
}

render::DrawOptions::PostGrade RacePhotoPause::grade() const {
    const photo::Correction c = correction();
    render::DrawOptions::PostGrade g;
    g.on = !c.plain();
    g.black = c.black, g.white = c.white, g.saturation = c.saturation;
    for (int i = 0; i < 3; ++i) g.curve[i] = std::clamp(2 * std::pow(0.5f, 1.0f / std::max(c.gamma[i], 1e-3f)) - 0.5f, 0.0f, 1.0f);
    return g;
}

// The camera's pose for a picture `aspect` wide: its angle over the picture's height, or its width held upright (0x371F88).
race::ViewPose RacePhotoPause::pose(float aspect, float ahead) const {
    race::ViewPose p;
    p.eye = camera_.eye(), p.view = camera_.view();
    if (pan_from_ && state_ == State::Exposing) {
        const float t = std::clamp(ahead, 0.0f, 1.0f), d = kPi / 180.0f;
        p.eye = pan_from_->eye + (camera_.eye() - pan_from_->eye) * t;
        const float yaw = pan_from_->yaw + race::photo::difference(camera_.yaw(), pan_from_->yaw) * t;
        const float pitch = pan_from_->pitch + (camera_.pitch() - pan_from_->pitch) * t;
        p.view = render::rotate_z(camera_.roll() * d) * render::rotate_x(pitch * d) * render::rotate_y(yaw * d) * render::translate(-p.eye);
    }
    const float t = std::tan(camera_.fov() * 0.5f * kPi / 180.0f); // (over the 24 mm side)
    const float half_height = aspect >= 1 ? t : t / aspect;
    // (ViewPose's angle is across a 4:3 picture's width, a narrower one keeping that width)
    p.fov = 2 * std::atan(aspect >= kAspect ? half_height * kAspect : half_height * aspect);
    p.near = race::lens_near(camera_.fov() * kPi / 180.0f); // (CameraVariable::vf12 0x3794A8, no least angle: ✔ 0.818 m at 140 mm in the original's film)
    p.depth_of_field = false;
    return p;
}

void RacePhotoPause::expose(Ui& ui) {
    if (!exposure_.active()) return;
    const int w = exposure_.width(), h = exposure_.height();
    const float aspect = float(w) / float(h);
    const float t = std::tan(camera_.fov() * 0.5f * kPi / 180.0f);
    const float py = 1.0f / (aspect >= 1 ? t : t / aspect), px = py / aspect;
    const photo::Sample& sample = samples_[size_t(exposure_.sample())];
    const photo::LensShift shift = photo::lens_shift(sample, camera_.focus(), camera_.f_number(), 32, aspect, px, py, w, h);
    // Each picture a share of the shutter's time on (0x333680, 0x334730: k / 32 of the shutter's 60ths of a second, at most one step).
    const float frames = std::min(1.0f, camera_.shutter_seconds() * 60.0f);
    const float ahead = float(exposure_.sample()) / float(photo::kSamples) * frames;
    const bool shot = exposure_.shoot(ui, [&](Ui& u, const SDL_FRect& all) {
        RaceWorld::Lens lens;
        lens.shear_x = shift.shear_x, lens.shear_y = shift.shear_y;
        race::ViewPose p = pose(aspect, ahead);
        p.view = render::translate({shift.move_x, shift.move_y, 0}) * p.view;
        const RVec right{p.view.at(0, 0), p.view.at(0, 1), p.view.at(0, 2)}, up{p.view.at(1, 0), p.view.at(1, 1), p.view.at(1, 2)};
        p.eye = p.eye - right * shift.move_x - up * shift.move_y;
        race_.draw(u, all, p, &lens, {}, ahead);
    });
    if (!shot) {
        exposure_.cancel();
        state_ = State::Finder;
        open_movie();
        return;
    }
    if (!exposure_.done()) return;
    std::optional<menu::JpegPicture::Focus> frame = develop_frame_;
    if (!develop_ && race_.car_frame) frame = race_.car_frame(camera_.view(), px, py);
    picture_ = exposure_.develop(quality_, frame);
    LOG_INFO("Photo pause: a photo of %d x %d, f = %d mm, F%.1f, 1/%d s, %+.1f EV, %.0f K, filter %d", w, h, camera_.focal_mm(), double(camera_.f_number()),
             int(1.0f / camera_.shutter_seconds() + 0.5f), double(camera_.exposure()), double(camera_.kelvin()), filter_);
    // (GT4R_PHOTO_OUT=<file>: a scripted run's photo written there as well, to look at)
    if (const char* out = std::getenv("GT4R_PHOTO_OUT"); out && *out && picture_ && !picture_->jpeg.empty())
        if (std::FILE* f = std::fopen(out, "wb")) std::fwrite(picture_->jpeg.data(), 1, picture_->jpeg.size(), f), std::fclose(f);
    state_ = State::Saved;
}

void RacePhotoPause::render(Ui& ui) {
    if (state_ == State::Exposing) expose(ui);
    const SDL_FRect c = RaceHud::canvas();
    const float k = c.h / 480.0f;
    ui.fill(c.x, c.y, c.w, c.h, Color{0, 0, 0, 255});
    if (state_ == State::Closed || develop_) return; // (a film's development: black under its photo's page)
    // The finder (0x3C5430: the race drawn through the camera with its depth of field and correction; 0x3C5AB8 puts it on the picture).
    const SDL_FRect f = camera_.portrait() ? kFinderUpright : kFinder;
    const SDL_FRect dst{c.x + f.x * k, c.y + f.y * k, f.w * k, f.h * k};
    {
        RaceWorld::Lens lens;
        lens.dof.on = true;
        lens.dof.f_number = camera_.f_number(), lens.dof.focal_mm = 35, lens.dof.focus = camera_.focus();
        lens.full_detail = false;
        race_.draw(ui, dst, pose(dst.w / dst.h), &lens, grade(), 0);
        // The edges the view is turned to (0x3C4F38): small red marks at the finder's sides, turned with the roll.
        if (const u8 limits = camera_.limits()) {
            SDL_Renderer* r = ui.renderer();
            const float cx = dst.x + dst.w * 0.5f, cy = dst.y + dst.h * 0.5f, sx = dst.w * 0.5f / 1.1f, sy = dst.h * 0.5f / 1.1f;
            const float roll = camera_.roll() * kPi / 180.0f, cr = std::cos(roll), sr = std::sin(roll);
            const struct {
                u8 bit;
                float x, y, turn;
            } marks[4] = {{1, -1, 0, 0}, {2, 1, 0, 180}, {4, 0, 1, -90}, {8, 0, -1, 90}};
            for (const auto& mk : marks) {
                if (!(limits & mk.bit)) continue;
                const float a = mk.turn * kPi / 180.0f, ca = std::cos(a), sa = std::sin(a);
                const float tri[3][2] = {{-1, 0}, {0, 1}, {0, -1}};
                SDL_Vertex v[3] = {};
                for (int i = 0; i < 3; ++i) {
                    const float lx = tri[i][0] * 0.05f, ly = tri[i][1] * 0.05f;
                    const float x = mk.x + lx * ca - ly * sa, y = mk.y + lx * sa + ly * ca;
                    const float rx = x * cr - y * sr, ry = x * sr + y * cr;
                    v[i].position = {cx + rx * sx, cy - ry * sy};
                    v[i].color = {0.8f, 0, 0, 0.5f};
                }
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
            }
        }
        if (hud_ && player_ && movie_ && state_ == State::Finder) hud_->draw_movie(*movie_, *player_);
    }
}

} // namespace gt4r
