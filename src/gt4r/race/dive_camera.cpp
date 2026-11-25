#include "gt4r/race/dive_camera.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt4r::race {

namespace render = gt3r::render;
using RVec = render::Vec3;

namespace {

constexpr float kPi = 3.14159265f, kDegrees = 180.0f / kPi;
constexpr float kStep = 1.0f / 60;

float wrap180(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    return (a < 0 ? a + 360 : a) - 180;
}
float wrap360(float a) {
    a = std::fmod(a, 360.0f);
    return a < 0 ? a + 360 : a;
}
// A direction's pitch (+ down) and yaw as the cameras count them, degrees (0x36EA50, 0x36EA98).
float pitch_of(const RVec& d) { return -std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)) * kDegrees; }
float yaw_of(const RVec& d) { return 180 - std::atan2(d.x, d.z) * kDegrees; }
RVec turn(const RVec& v, const RVec& k, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + render::cross(k, v) * s + k * (render::dot(k, v) * (1 - c));
}
void set_view(ViewPose& out, const RVec& eye, float yaw, float pitch, float roll) {
    const float ya = (180 - yaw) / kDegrees, pa = pitch / kDegrees;
    const RVec dir{std::sin(ya) * std::cos(pa), -std::sin(pa), std::cos(ya) * std::cos(pa)};
    RVec right = render::cross(dir, {0, 1, 0});
    if (render::length(right) < 1e-4f) right = {1, 0, 0};
    right = render::normalize(right);
    RVec up = render::cross(right, dir);
    if (roll != 0) right = turn(right, dir, roll / kDegrees), up = turn(up, dir, roll / kDegrees);
    out.eye = eye;
    out.view.m = {right.x, up.x, -dir.x, 0, right.y, up.y, -dir.y, 0, right.z, up.z, -dir.z, 0, -render::dot(right, eye), -render::dot(up, eye), render::dot(dir, eye), 1};
}
// The width of the picture at the car a draw asks for (1.5 to 11.5 m, the small ones likelier: 0x37E908), as a lens at a distance.
float width_of(float r) { return (((3.0f / 7.0f) * r - 3.0f / 7.0f) * r + 1) * r * 10 + 1.5f; }
float lens_at(float width, float distance) { return 2 * std::atan(width / (2 * std::max(distance, 0.1f))) * kDegrees; }

// Which camera (0x376638's tables 0x6210B8, 0x6210E0, 0x621108, 0x621130 by the shot's length and the car): nine shares, the last
// the rest: 0 on board, 1 .. 3 round the car, 4 .. 6 a fixed camera beside the road, 7 from above, 8 a roadside point. The disc's program
// holds them; these are the ways through them.
enum Way { kOnBoard = 0, kRound0 = 1, kRound2 = 3, kFix0 = 4, kFix2 = 6, kHeli = 7, kPoint = 8 };

} // namespace

float Draws::next() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / float(1u << 24);
}

int Draws::below(int n) { return n <= 0 ? 0 : std::min(n - 1, int(next() * float(n))); }

EaseCurve::EaseCurve(float a, float b, float c, float d) {
    k1 = a != 0 ? b / a : 0;
    const float e = c != 0 ? d / c : 0;
    k2 = 3 - 2 * k1 - e, k3 = k1 + e - 2;
}

float EaseCurve::at(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return ((k3 * t + k2) * t + k1) * t;
}

bool DiveClock::step(std::optional<float> next, bool passed) {
    bool cut = next && passed;
    const float t = next ? *next : 100.0f;
    if (t > 8) {
        timer_ -= kStep;
        if (timer_ < 0) timer_ = 8, cut = true;
    } else {
        timer_ = t;
    }
    return cut;
}

std::optional<float> MusicMarkers::next(float now) const {
    if (at.empty()) return std::nullopt;
    const float t = now + 1.0f / 30;
    const auto it = std::upper_bound(at.begin(), at.end(), t);
    return (it == at.end() ? at.front() + length : *it) - t;
}

bool MusicMarkers::passed(float before, float now) const {
    if (now < before) return true; // (the piece begun again)
    const float a = before + 1.0f / 30, b = now + 1.0f / 30;
    const auto it = std::upper_bound(at.begin(), at.end(), a);
    return it != at.end() && *it <= b;
}

float DiveDirector::Roll::at(float t) const { return to + (from - to) * std::clamp(ease.at(std::clamp(t / std::max(duration, 1e-3f), 0.0f, 1.0f)), 0.0f, 1.0f); }

bool DiveDirector::load(ByteSpan program, u32 load_address) {
    auto f = [&](u32 at) {
        float v = 0;
        if (at >= load_address && at - load_address + 4 <= program.size()) std::memcpy(&v, program.data() + (at - load_address), 4);
        else v = NAN;
        return v;
    };
    Tables t;
    bool ok = true;
    // Records of six floats: the three channels' gammas, the saturation, the black and the white (0x400458).
    auto read = [&](u32 at, u32 n, std::vector<std::array<float, 6>>& out) {
        for (u32 i = 0; i < n; ++i) {
            std::array<float, 6> v{};
            for (u32 k = 0; k < 6; ++k) v[k] = f(at + 24 * i + 4 * k);
            for (float x : v) ok &= std::fabs(x) < 10;
            out.push_back({v[0], v[1], v[2], v[4], v[5], v[3]});
        }
    };
    read(0x622700, 21, t.tints), read(0x6228F8, 12, t.hues);
    for (u32 k = 0; k < 3; ++k) t.points[k] = f(0x6210A8 + 4 * k);
    const u32 ways[3] = {0x6210B8, 0x6210E0, 0x621108};
    for (size_t i = 0; i < 3; ++i) {
        float sum = 0;
        for (u32 k = 0; k < 9; ++k) t.ways[i][k] = f(ways[i] + 4 * k), sum += t.ways[i][k], ok &= t.ways[i][k] >= 0 && t.ways[i][k] < 10;
        ok &= sum >= 1;
    }
    ok &= t.points[0] > 0 && t.points[0] < t.points[1] && t.points[1] < t.points[2];
    if (!ok) return false;
    tables_ = std::move(t);
    return true;
}

void DiveDirector::reset(u32 seed, size_t car) {
    const Tables t = tables_;
    *this = {};
    tables_ = t;
    r_ = Draws(seed), car_ = car;
}

bool DiveDirector::step(const World& w, bool cut, float length, ViewPose& out) {
    ++since_car_, ++frames_;
    cut_ = false;
    if (w.cars == 0) return false;
    car_ = std::min(car_, w.cars - 1);
    if (cut || kind_ == Kind::None) {
        // Another car, at random, from four seconds on (0x400BA8).
        if (since_car_ > 240 && kind_ != Kind::None) car_ = size_t(r_.below(int(w.cars)));
        if (begin_shot(w, length)) {
            cut_ = true, frames_ = 0, first_ = true;
            // One cut of ten lets the picture trail (0x376C98).
            trail_ = r_.next() < 0.1f ? r_.next() * 0.5f + 0.5f : 0.0f;
        }
    }
    color(cut);
    bool ok = false;
    out.haze = false, out.depth_of_field = false, out.f_number = 8, out.focus = 100, out.body_hidden = false;
    if (kind_ == Kind::Point) ok = point_pose(w, out);
    else if (kind_ == Kind::Round) ok = round_pose(w, out);
    else if (kind_ == Kind::Above) ok = above_pose(w, out);
    else if (kind_ == Kind::Fixed) {
        set_view(out, fixed_eye_, fixed_yaw_, fixed_pitch_, fixed_roll_);
        out.fov = shown_angle(fixed_lens_ / kDegrees);
        ok = true;
    } else if (kind_ == Kind::OnBoard) ok = on_board_pose(w, out);
    first_ = false;
    if (!ok) kind_ = Kind::None;
    return ok;
}

bool DiveDirector::begin_shot(const World& w, float length) {
    length_ = std::max(length, 0.1f);
    // The shares by the length: a long shot rides or circles, the others stand beside the road.
    const std::array<float, 9>& share = tables_.ways[length_ > 7.8f ? 2 : length_ < 2 ? 0 : 1];
    float r = r_.next();
    int way = 0;
    for (; way < 8; ++way)
        if ((r -= share[size_t(way)]) <= 0) break;
    // (A fixed camera that finds no place: one of a long shot's, 0x376A94.)
    if (way >= kFix0 && way <= kFix2) return begin_fixed(w, length_, way - kFix0) || (begin_round(w, length_, r_.below(3)), true);
    if (way == kHeli) return begin_above(), true;
    if (way == kPoint && begin_point(w, length_)) return true;
    if (way >= kRound0 && way <= kRound2) return begin_round(w, length_, way - kRound0), true;
    if (way == kOnBoard) return begin_on_board(w, length_), true;
    return begin_round(w, length_, r_.below(3)), true;
}

// --- a roadside point ----------------------------------------------------------------------

bool DiveDirector::begin_point(const World& w, float length) {
    if (!w.script || w.script->points.empty() || !(w.lap_length > 0)) return false;
    const Car c = w.car(car_);
    if (!c.state) return false;
    const float d = c.state->course_v, v = std::max(c.state->speed, 0.1f);
    struct Choice {
        size_t point;
        float score;
        bool during; // the car passes it within the shot, not at its ends
    };
    std::vector<Choice> list;
    for (size_t i = 0; i < w.script->points.size(); ++i) {
        const ReplayPoint& p = w.script->points[i];
        // The car's distance brought into the point's stretch (0x3FF380).
        float at = d;
        if (!(p.start <= at && at <= p.end)) {
            if (p.start <= d - w.lap_length && d - w.lap_length <= p.end) at = d - w.lap_length;
            else if (p.start <= d + w.lap_length && d + w.lap_length <= p.end) at = d + w.lap_length;
        }
        if (at > p.end || at < p.start - 500) continue;
        // The scores (0x3FF1B0): the car past the point at the shot's middle, its stretch about the shot's length.
        const float middle = (p.start + p.end) * 0.5f;
        const float to_point = std::max((middle - at) / v, 0.0f), to_end = (p.end - at) / v;
        float s = 0;
        if (to_end > length) s -= (to_end - length) * 0.5f;
        if (to_point > length) s -= (to_point - length) * 4;
        const float before = std::max(middle - p.start, 0.01f), after = std::max(p.end - middle, 0.01f);
        const float x = std::max(middle - at, 0.0f) / before, y = std::max(at + length * v - middle, 0.0f) / after;
        if (x > 1) s -= x - 1;
        if (y > 1) s -= (y - 1) / 3;
        s -= 3 * std::fabs(x - y);
        list.push_back({i, s, to_point > length / 10 && to_point < length - length / 10});
    }
    if (list.empty()) return false;
    std::stable_sort(list.begin(), list.end(), [](const Choice& a, const Choice& b) { return a.score > b.score; });
    if (std::any_of(list.begin(), list.end(), [](const Choice& a) { return a.during; })) std::erase_if(list, [](const Choice& a) { return !a.during; });
    const float pick = r_.next();
    size_t at = 0;
    while (at < 2 && tables_.points[at] < pick) ++at;
    point_ = w.script->points[list[at < list.size() ? at : 0].point];
    kind_ = Kind::Point;

    // 0x37E4D8's draws.
    zoom_in_ = r_.next() >= 0.7f;
    rise_ = EaseCurve(0.1f, r_.next() * 0.3f, 1, 0.01f);
    const EaseCurve height(0.01f, 0.027f, 0.01f, 0.022f);
    rise_from_ = height.at(r_.next()) * 0.8f, rise_to_ = height.at(r_.next()) * 0.8f;
    roll_ = {};
    if (r_.next() < 0.6f) {
        roll_.ease = EaseCurve(0.1f, r_.next() * 0.3f, 1, 0.01f);
        roll_.from = r_.next() * 40 - 20, roll_.to = r_.next() * 40 - 20;
        roll_.duration = r_.next() * 0.5f + 0.2f;
    }
    spin_ = r_.next() < 0.15f ? r_.next() * 200 - 100 : 0.0f;
    spin_rate_ = 0;
    let_go_ = r_.next() * 0.2f + 0.5f, let_gone_ = 1 - r_.next() * 0.2f;
    if (r_.next() < 0.5f) let_go_ = 1, let_gone_ = 1.05f;
    pull_from_ = r_.next() - 0.5f;
    if (r_.next() < 0.7f) pull_from_ = 1;
    opening_ = r_.next() * 20.6f + 1.4f;
    return true;
}

bool DiveDirector::point_pose(const World& w, ViewPose& out) {
    const Car c = w.car(car_);
    if (!c.state) return false;
    const float L = w.lap_length;
    float at = c.state->course_v;
    if (!(point_.start <= at && at <= point_.end)) at = std::fabs(at - L - point_.start) < std::fabs(at - point_.start) ? at - L : std::fabs(at + L - point_.start) < std::fabs(at - point_.start) ? at + L : at;
    const float progress = std::clamp((at - point_.start) / std::max(point_.end - point_.start, 0.1f), 0.0f, 1.0f);
    const float t = std::clamp(float(frames_) / (length_ * 60), 0.0f, 1.0f);
    const RVec origin{c.body.at(0, 3), c.body.at(1, 3), c.body.at(2, 3)};
    RVec target = origin;
    target.y += 0.4f + rise_from_ + (rise_to_ - rise_from_) * rise_.at(progress);
    const RVec to = target - point_.at;
    const float distance = std::max(render::length(to), 0.1f);
    float pitch = pitch_of(to), yaw = yaw_of(to);
    const RVec speed{c.state->velocity.x, c.state->velocity.y, c.state->velocity.z};
    // The aim's own motion a step leads the car in the picture.
    const float drift = std::clamp(wrap180(yaw - yaw_of(origin - speed * kStep - point_.at)), -0.4f, 0.4f);
    if (first_) {
        // The lens: a picture so wide at the car where the stretch begins, at the point and where it ends (0x37EC50); either
        // from the one to the other, or held from the point on.
        middle_ = std::clamp((point_.start + point_.end) * 0.5f, point_.start, point_.end);
        const float side = std::sqrt(std::max(0.0f, distance * distance - (middle_ - at) * (middle_ - at)));
        auto away = [&](float v) { return std::max(1.0f, std::sqrt((v - middle_) * (v - middle_) + side * side)); };
        middle_lens_ = lens_at(width_of(r_.next()), away(middle_));
        flat_lens_ = r_.next() >= 0.5f;
        const float begin = lens_at(width_of(r_.next()), away(point_.start));
        lens_keys_[0] = {0, begin};
        if (flat_lens_) lens_keys_[1] = {(middle_ - point_.start) / std::max(point_.end - point_.start, 0.1f), middle_lens_}, lens_keys_[2] = {1, middle_lens_}, lens_key_count_ = 3;
        else lens_keys_[1] = {1, lens_at(width_of(r_.next()), away(point_.end))}, lens_key_count_ = 2;
    }
    float f = lens_keys_[size_t(lens_key_count_ - 1)][1];
    for (int k = 0; k + 1 < lens_key_count_; ++k)
        if (progress <= lens_keys_[size_t(k + 1)][0]) {
            const auto &a = lens_keys_[size_t(k)], &b = lens_keys_[size_t(k + 1)];
            f = a[1] + (b[1] - a[1]) * std::clamp((progress - a[0]) / std::max(b[0] - a[0], 1e-3f), 0.0f, 1.0f);
            break;
        }
    f = std::clamp(f, 2.0f, 70.0f);
    // The operator lets go of the car toward the end, and at the stretch's ends.
    float hold = let_go_ < t ? std::clamp(1 - (t - let_go_) / std::max(let_gone_ - let_go_, 1e-3f), 0.001f, 1.0f) : 1.0f;
    if (progress > 0.95f || progress < 0.01f) hold = 0;
    if (first_) {
        lead_.reset(-drift);
        gain_yaw_.reset(hold), gain_pitch_.reset(hold);
        pull_.reset(pull_from_);
    }
    const float lead = first_ ? lead_.x : lead_.step(-drift);
    const float gy = first_ ? hold : gain_yaw_.step(hold), gp = first_ ? hold : gain_pitch_.step(hold);
    yaw -= lead * f / 2;
    pitch -= 0.15f * f * std::cos(pitch / kDegrees);
    if (first_) {
        yaw_.reset(wrap360(yaw), u32(r_.next() * 4e9f) | 1), pitch_.reset(wrap360(pitch), u32(r_.next() * 4e9f) | 1);
        // The lens starts wide on some and closes in.
        lens_ = zoom_in_ ? std::clamp(f * (r_.next() * 15 + 3), 2.0f, 90.0f) : f, lens_rate_ = 0;
        yaw = yaw_.value(), pitch = pitch_.value();
    } else {
        yaw = yaw_.step(wrap360(yaw), gy), pitch = pitch_.step(wrap360(pitch), gp);
        // (0x3851D0: a spring whose step changes the lens by no more than 0.91 to 1.03 of it.)
        lens_rate_ += ((f - lens_) * 100 - 20 * lens_rate_) / 60;
        const float next = std::max(lens_ + lens_rate_ / 60, 0.001f);
        lens_ = std::clamp(next, lens_ * 0.91f, lens_ * 1.03f);
        // (the start's turn of the picture, on a spring back to level)
        spin_rate_ += ((0 - spin_) * 30 - 10 * spin_rate_) / 60;
        spin_ += spin_rate_ / 60;
    }
    set_view(out, point_.at, yaw, pitch, roll_.at(t) + spin_);
    out.fov = shown_angle(lens_ / kDegrees);
    out.depth_of_field = true, out.f_number = opening_;
    out.focus = std::clamp(distance * std::clamp(first_ ? pull_.x : pull_.step(2), 0.0f, 1.0f), 0.001f, 100000.0f);
    return true;
}

// --- round the car -------------------------------------------------------------------------

void DiveDirector::begin_round(const World& w, float length, int way) {
    kind_ = Kind::Round, way_ = way;
    const Car c = w.car(car_);
    const RVec size = c.box_max - c.box_min;
    still_ = r_.next();
    const float ease = r_.next() * 0.3f;
    round_ease_ = EaseCurve(0.1f, ease, 1, 0.01f);
    const float x0 = r_.next() * 0.3f, x1 = std::max(r_.next() * 0.3f + 0.7f, 0.0f), y0 = r_.next() * x0, y1 = std::max(r_.next() * 0.3f + 0.7f, x1);
    ramp_ = {x0, x1, y0, y1};
    eased_ = r_.next() < 0.3f;
    round_lens_ = r_.next() * 35 + 25;
    roll_from_ = r_.next() * 40 - 20, roll_to_ = r_.next() * 40 - 20;
    const float seconds = std::min(length, 5.0f);
    start_angle_ = r_.next() * 360;
    // Ways 0 and 2 sweep farther the longer the shot, 1 from a little to half round.
    sweep_ = way == 1 ? r_.next() * 160 + 20 : r_.next() * 220 * seconds * 0.6f + 100;
    if (r_.next() < 0.5f) sweep_ = -sweep_;
    base_angle_ = r_.next() * 360;
    const float high = std::max(size.y, 0.8f), half_w = size.x / 2, half_l = size.z / 2, tall = size.y;
    auto around = [&] { return RVec{r_.next() * size.x * 0.7f - half_w * 0.7f, r_.next() * tall * 0.8f, r_.next() * 3 - 1.5f}; };
    look_to_ = around(), look_from_ = around();
    // The eye: its height and its distance from the look's place, farther with a narrower lens; at least the car's half diagonal.
    const float diagonal = std::max({std::sqrt(half_w * half_w + half_l * half_l), half_w, high});
    const float reach = std::max(std::fabs(look_to_.z), std::fabs(look_from_.z));
    const float narrow = (1 - (round_lens_ - 25) / 35) * 0.8f + 0.2f;
    eye_to_ = {0, r_.next() * 2 - 1, narrow * r_.next() * 4 + reach + diagonal + 0.2f};
    eye_from_ = {0, r_.next() * 3 - 1.5f, r_.next() * 4 + reach + diagonal + 0.2f};
    for (auto& n : noise_) n[0] = n[1] = n[2] = 0;
    wall_ = 0;
}

bool DiveDirector::round_pose(const World& w, ViewPose& out) {
    const Car c = w.car(car_);
    if (!c.state) return false;
    float u = 1 - std::clamp(float(frames_) / (length_ * 60), 0.0f, 1.0f);
    // The slow part of the sweep (0x36F7D8: straight lines through two places).
    if (eased_) {
        const auto [x0, x1, y0, y1] = ramp_;
        u = u <= x0 ? (x0 > 0 ? y0 * u / x0 : y0) : u <= x1 ? y0 + (y1 - y0) * (u - x0) / std::max(x1 - x0, 1e-3f) : y1 + (1 - y1) * (u - x1) / std::max(1 - x1, 1e-3f);
    }
    if (way_ == 2) u = still_;
    const float k = 1 - round_ease_.at(1 - u);
    if (eased_) u = k;
    const float roll = roll_to_ + (roll_from_ - roll_to_) * k;
    const RVec look = look_to_ + (look_from_ - look_to_) * k, off = eye_to_ + (eye_from_ - eye_to_) * k;
    // The angle round the car, its steps slower about its nose and tail (0x381428: a quarter turn's cubic).
    float a = wrap360(start_angle_ + u * sweep_ + base_angle_);
    auto quarter = [](float x) {
        const float q = std::clamp(x / 90, 0.0f, 1.0f);
        return ((0.12857f * q - 0.55714f) * q + 1.428571f) * q * 90;
    };
    a = a < 90 ? quarter(a) : a < 180 ? 180 - quarter(180 - a) : a < 270 ? quarter(a - 180) + 180 : 360 - quarter(360 - a);
    a = wrap360(a - base_angle_) / kDegrees;
    // In the car's heading, level: the place looked at and the eye round it.
    const RVec origin{c.body.at(0, 3), c.body.at(1, 3), c.body.at(2, 3)};
    const float h = car_heading(*c.state);
    const RVec ahead{std::sin(h), 0, std::cos(h)}, right = render::cross(ahead, {0, 1, 0});
    auto world = [&](const RVec& p) { return origin + right * p.x + RVec{0, p.y, 0} - ahead * p.z; };
    const RVec target = world(look);
    RVec eye = world(look + RVec{std::sin(a) * off.z, off.y, std::cos(a) * off.z});
    eye = clear_eye(w, target, eye);
    float ground = 0;
    if (w.ground && w.ground(eye, origin.y + 3, ground)) eye.y = std::max(eye.y, ground + 0.2f);
    const RVec to = target - eye;
    const float distance = std::max(render::length(to), 0.1f);
    float pitch = std::clamp(pitch_of(to), -40.0f, 40.0f), yaw = yaw_of(to);
    // The hand's shake on ways 1 and 2 (0x384928): a random target on one step of ten, by a spring.
    for (auto& n : noise_) {
        if (r_.next() < 0.1f) n[1] = (r_.next() * 2 - 1) * 128;
        n[2] += (0.35f * 115 * (n[1] - n[0]) - 10 * n[2]) / 60;
        n[0] += n[2] / 60;
    }
    if (way_ >= 1) {
        pitch += std::clamp(noise_[0][0] / 128, -1.0f, 1.0f) * round_lens_ / 20;
        yaw += std::clamp(noise_[1][0] / 128, -1.0f, 1.0f) * round_lens_ / 20;
    }
    set_view(out, eye, yaw, pitch, roll);
    out.fov = shown_angle(round_lens_ / kDegrees);
    out.depth_of_field = true, out.f_number = 100, out.focus = distance;
    return true;
}

// A wall between the place looked at and the eye (0x3FE7B0): as near as it lets, and the picture trails.
RVec DiveDirector::clear_eye(const World& w, const RVec& target, const RVec& eye) {
    wall_ = 0;
    if (!w.blocked || !w.blocked(target, eye)) return eye;
    float clear = 0, hidden = 1;
    for (int i = 0; i < 6; ++i) {
        const float mid = (clear + hidden) / 2;
        (w.blocked(target, target + (eye - target) * mid) ? hidden : clear) = mid;
    }
    wall_ = 1.4f;
    const float whole = std::max(render::length(eye - target), 0.1f);
    return target + (eye - target) * std::clamp(clear * 0.95f, std::min(1.0f, 2.0f / whole), 1.0f);
}

// --- fixed cameras (CameraAutoFix 0x381D88, 0x3821F0, 0x382708) ---------------------------------

RVec DiveDirector::ahead_of(const World& w, const Car& c, float seconds) const {
    if (w.ahead)
        if (const std::optional<RVec> at = w.ahead(car_, seconds)) return *at;
    const RVec origin{c.body.at(0, 3), c.body.at(1, 3), c.body.at(2, 3)};
    return origin + RVec{c.state->velocity.x, c.state->velocity.y, c.state->velocity.z} * seconds;
}

bool DiveDirector::begin_fixed(const World& w, float length, int way) {
    const Car c = w.car(car_);
    if (!c.state) return false;
    const RVec origin{c.body.at(0, 3), c.body.at(1, 3), c.body.at(2, 3)}, velocity{c.state->velocity.x, c.state->velocity.y, c.state->velocity.z};
    const float lens = r_.next() * 30 + 50;
    RVec eye;
    float yaw = 0;
    if (way == 0) {
        // Just ahead of the car and looking its way: it comes past at once.
        const float seconds = std::min(length * (r_.next() * 0.15f + 0.05f), 0.5f);
        eye = origin + velocity * seconds;
        eye.x += r_.next() * 2 - 1, eye.z += r_.next() * 2 - 1;
        yaw = yaw_of(eye - origin) + r_.next() * 20 - 10;
    } else if (way == 1) {
        // Beside the line it will take, looking back at it.
        const float seconds = length * (r_.next() * 0.35f + 0.5f);
        const RVec at = ahead_of(w, c, seconds), before = ahead_of(w, c, std::max(seconds - 0.1f, 0.0f));
        const RVec back = before - at;
        if (render::length(back) < 1e-3f) return false;
        yaw = yaw_of(back);
        float side = r_.next() * 5 + 2;
        if (r_.next() < 0.5f) side = -side;
        eye = at + render::normalize(render::cross(back, {0, 1, 0})) * side;
        if (w.blocked && w.blocked(at + RVec{0, 1, 0}, eye + RVec{0, 1, 0})) eye = at + (eye - at) * 0.4f;
        yaw += r_.next() * 20 - 10;
        if (std::fabs(wrap180(yaw_of(origin - eye) - yaw)) > lens / 2) return false;
    } else {
        // Beside the car, turned where it will be in one to three seconds, which comes in from an edge.
        const RVec size = c.box_max - c.box_min;
        const float h = car_heading(*c.state);
        const RVec ahead{std::sin(h), 0, std::cos(h)}, right = render::cross(ahead, {0, 1, 0});
        float x = size.x * (r_.next() * 1.5f + 0.7f), z = size.z * r_.next();
        if (r_.next() < 0.5f) x = -x;
        if (r_.next() < 0.5f) z = -z;
        eye = origin + right * x + ahead * z;
        const RVec to = ahead_of(w, c, r_.next() * 2 + 1) - eye, now = origin - eye;
        const float way_round = now.z * to.x - now.x * to.z < 0 ? -1.0f : 1.0f;
        yaw = yaw_of(to) + lens * way_round / 2;
    }
    // On the ground there, 0.1 m over it and up to 1.6 more.
    float ground = 0;
    if (w.ground) {
        if (!w.ground(eye, origin.y + 3, ground) || std::fabs(ground - origin.y) > 5) return false;
    } else {
        ground = origin.y;
    }
    const float up = r_.next() + 0.1f;
    eye.y = ground + up + r_.next() * 0.6f;
    kind_ = Kind::Fixed;
    fixed_eye_ = eye, fixed_yaw_ = yaw, fixed_lens_ = lens;
    fixed_pitch_ = r_.next() * 20 - 15, fixed_roll_ = r_.next() * 30 - 15;
    return true;
}

// --- from above (CameraHeli 0x3834A0, 0x3837C0) -----------------------------------------------

void DiveDirector::begin_above() {
    kind_ = Kind::Above;
    auto over = [&] {
        const float reach = r_.next() * 6 + 1, a = r_.next() * 2 * kPi;
        const float y = r_.next() * 4 + 3;
        return RVec{reach * std::cos(a), y, reach * std::sin(a)};
    };
    auto look = [&] {
        const float x = r_.next() * 1.5f - 0.75f, y = r_.next() * 0.6f + 0.4f;
        return RVec{x, y, r_.next() * 3 - 1.5f};
    };
    eye_from_ = over(), eye_to_ = over();
    look_from_ = look(), look_to_ = look();
    roll_from_ = r_.next() * 40 - 20;
    round_lens_ = r_.next() * 30 + 40;
    round_ease_ = EaseCurve(0.1f, 0.3f, 1, 0.01f);
    for (auto& n : noise_) n[0] = n[1] = n[2] = 0;
    wall_ = 0;
}

bool DiveDirector::above_pose(const World& w, ViewPose& out) {
    const Car c = w.car(car_);
    if (!c.state) return false;
    const float k = round_ease_.at(std::clamp(float(frames_) / (length_ * 60), 0.0f, 1.0f));
    const RVec off = eye_from_ + (eye_to_ - eye_from_) * k, look = look_from_ + (look_to_ - look_from_) * k;
    const RVec origin{c.body.at(0, 3), c.body.at(1, 3), c.body.at(2, 3)};
    const float h = car_heading(*c.state);
    const RVec ahead{std::sin(h), 0, std::cos(h)}, right = render::cross(ahead, {0, 1, 0});
    auto world = [&](const RVec& p) { return origin + right * p.x + RVec{0, p.y, 0} - ahead * p.z; };
    const RVec target = world(look), eye = clear_eye(w, target, world(off));
    const RVec to = target - eye;
    float pitch = pitch_of(to), yaw = yaw_of(to);
    // Its hand follows the car's turn slowly (a share of 0.2).
    if (first_) yaw_.reset(wrap360(yaw), u32(r_.next() * 4e9f) | 1), yaw = yaw_.value();
    else yaw = yaw_.step(wrap360(yaw), 0.2f);
    for (auto& n : noise_) {
        if (r_.next() < 0.3f) n[1] = (r_.next() * 2 - 1) * 128;
        n[2] += (115 * (n[1] - n[0]) - 10 * n[2]) / 60;
        n[0] += n[2] / 60;
    }
    pitch += std::clamp(noise_[0][0] / 128, -1.0f, 1.0f) * 0.7f;
    yaw += std::clamp(noise_[1][0] / 128, -1.0f, 1.0f) * 0.7f;
    set_view(out, eye, yaw, pitch, roll_from_);
    out.fov = shown_angle(round_lens_ / kDegrees);
    return true;
}

// --- on board ------------------------------------------------------------------------------

void DiveDirector::begin_on_board(const World& w, float length) {
    (void)length;
    kind_ = Kind::OnBoard;
    const Car c = w.car(car_);
    // 0x376238: the nose or the front bumper three times in ten each, else any of the car's other places.
    std::vector<int> places;
    for (int i = 3; i < int(c.mounts.size()) && i < 26; ++i)
        if (i != 5 && i != 20 && (c.mounts[size_t(i)].flags & 1)) places.push_back(i);
    const float r = r_.next();
    mount_ = r < 0.3f ? 0 : r < 0.6f || places.empty() ? 5 : places[size_t(r_.below(int(places.size())))];
    board_lens_ = 0, back_ = false;
    if (mount_ == 0 || mount_ == 5) {
        board_lens_ = r_.next() * 20 + 60;
        back_ = r_.next() < 0.5f;
    }
    rolls_ = r_.next() < 0.4f;
    if (rolls_) {
        const bool still = mount_ != 0 && mount_ != 5 && !(mount_ >= 14 && mount_ < 18);
        roll_.ease = EaseCurve(0.1f, r_.next() * 0.3f, 1, 0.01f);
        roll_.from = r_.next() * 40 - 20, roll_.to = r_.next() * 40 - 20;
        if (still) roll_.to = roll_.from;
        roll_.duration = r_.next() * 0.5f + 0.2f;
    }
    if (c.state) board_.reset(*c.state);
}

bool DiveDirector::on_board_pose(const World& w, ViewPose& out) {
    const Car c = w.car(car_);
    if (!c.state) return false;
    board_.step(*c.state, kStep);
    int mount = mount_;
    if (size_t(mount) >= c.mounts.size() || c.mounts[size_t(mount)].flags == 0) mount = DriveCamera::kInCar;
    out = board_.pose(*c.state, c.body, c.mounts, mount, back_);
    if (board_lens_ > 0) out.fov = shown_angle(board_lens_ / kDegrees);
    else if (mount != DriveCamera::kInCar && mount != DriveCamera::kChase) out.fov = shown_angle(out.fov);
    if (rolls_) {
        // (nearest the eye)
        const float a = roll_.at(std::clamp(float(frames_) / (length_ * 60), 0.0f, 1.0f)) / kDegrees, cs = std::cos(a), sn = std::sin(a);
        render::Mat4 turned = out.view;
        for (int col = 0; col < 4; ++col) {
            turned.at(0, col) = out.view.at(0, col) * cs + out.view.at(1, col) * sn;
            turned.at(1, col) = out.view.at(1, col) * cs - out.view.at(0, col) * sn;
        }
        out.view = turned;
    }
    return true;
}

// --- the picture's colors ------------------------------------------------------------------

void DiveDirector::color(bool cut) {
    if (cut && !tables_.tints.empty() && !tables_.hues.empty()) {
        // 0x4007D8 -> 0x4004A8: plain, one of the tints or one of the strong ones by what it was.
        color_now_ = color_to_;
        const float shares[3][3] = {{0, 0.35f, 0.15f}, {0.25f, 0.25f, 0.15f}, {0.25f, 0.15f, 0.05f}};
        const auto& s = shares[size_t(std::clamp(color_state_, 0, 2))];
        float r = r_.next() - s[0];
        if (r < 0) {
            color_state_ = 0, color_to_ = {1, 1, 1, 0, 1, 1};
        } else if ((r -= s[1]) < 0) {
            color_state_ = 1, color_to_ = tables_.tints[size_t(r_.below(int(tables_.tints.size())))];
        } else if ((r -= s[2]) < 0) {
            color_state_ = 1, color_to_ = tables_.hues[size_t(r_.below(int(tables_.hues.size())))];
        }
        color_moves_ = r_.next() < 0.2f;
        color_on_ = true;
    }
    if (color_moves_) {
        const float rate = r_.next() * 0.04f + 0.015f;
        for (size_t i : {0u, 1u, 2u, 5u}) color_now_[i] += (color_to_[i] - color_now_[i]) * rate;
        color_now_[3] = color_to_[3], color_now_[4] = color_to_[4];
    } else {
        color_now_ = color_to_;
    }
}

render::DrawOptions::PostGrade DiveDirector::grade(const render::DrawOptions::PostGrade& plain) const {
    if (!color_on_) return plain;
    render::DrawOptions::PostGrade g;
    g.on = true, g.black = color_now_[3], g.white = std::max(color_now_[4], 0.05f), g.saturation = color_now_[5];
    for (size_t i = 0; i < 3; ++i) g.curve[i] = 2 * std::pow(0.5f, 1 / std::max(color_now_[i], 0.05f)) - 0.5f;
    return g;
}

} // namespace gt4r::race
