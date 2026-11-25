#include "gt4r/audio/course_sound.h"

#include <algorithm>
#include <cmath>

namespace gt4r::audio {

namespace {

constexpr float kRise = 1.0f / 120.0f, kFall = 1.0f / 180.0f; // a gated emitter's gain a step
constexpr float kPrologueStep = 1.0f / 60.0f;                  // Prologue's, both ways (0x40FE80)
constexpr float kNextCar = 0.8f;                              // each further car between the gates
constexpr float kNear = 32.0f;                                // full level inside this distance
constexpr float kSound = 331.45f;                             // m/s
constexpr float kHalfPi = 1.5707963f;
constexpr float kReverbStep = 1.0f / 36.0f;

float dot(const race::Vec3& a, const race::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

const char* CourseSound::effect_name(int c, bool prologue) {
    static const char* const kNames[2][kChannels] = {{"air", "city", "crowds", "cheer", "sea"}, {"air", "city", nullptr, "crowds", "sea"}};
    return c >= 0 && c < kChannels ? kNames[prologue ? 1 : 0][c] : nullptr;
}

void CourseSound::clear() {
    for (auto& l : lists_) l.clear();
    out_ = {};
    tunnel1_ = tunnel2_ = 0;
    reverb_ = target_ = 0.125f;
}

bool CourseSound::empty() const {
    for (const auto& l : lists_)
        if (!l.empty()) return false;
    return true;
}

void CourseSound::load(const gt3r::sound::CrssData& data, const std::function<bool(const race::Vec3&, float*)>& distance, float length) {
    clear();
    for (int c = 0; c < kChannels && c < gt3r::sound::CrssData::kChannels; ++c)
        for (const gt3r::sound::CrssEmitter& src : data.channels[size_t(c)]) {
            Emitter e;
            e.src = src;
            float a = 0, b = 0;
            if (src.gated() && distance && distance({src.gate_a[0], src.gate_a[1], src.gate_a[2]}, &a) &&
                distance({src.gate_b[0], src.gate_b[1], src.gate_b[2]}, &b)) {
                // The shorter way between the gates, which may cross the line (hi < lo).
                float lo = std::min(a, b), hi = std::max(a, b);
                if (hi - lo > length * 0.5f) std::swap(lo, hi);
                e.lo = lo, e.hi = hi;
            }
            lists_[size_t(c)].push_back(std::move(e));
        }
}

void CourseSound::update(const std::vector<Listener>& listeners, const std::vector<float>& car_distances) {
    for (auto& list : lists_)
        for (Emitter& e : list) {
            if (e.lo == e.hi) {
                e.gain = 1.0f;
                continue;
            }
            float target = 0, w = 1.0f;
            for (float d : car_distances) {
                const bool inside = e.lo <= e.hi ? d >= e.lo && d <= e.hi : d >= e.lo || d <= e.hi;
                if (inside) target += w;
                w *= kNextCar;
            }
            target = std::min(target, 1.0f);
            e.gain = e.gain < target ? std::min(e.gain + (prologue_ ? kPrologueStep : kRise), target)
                                     : std::max(e.gain - (prologue_ ? kPrologueStep : kFall), target);
        }

    out_ = {};
    tunnel1_ = tunnel2_ = 0;
    float target = 0.125f;
    for (const Listener& at : listeners) {
        for (int c = 0; c < kChannels; ++c) {
            const auto& list = lists_[size_t(c)];
            Out& o = out_[size_t(c)];
            if (list.empty()) continue;
            if (c == Air || c == City) {
                const Emitter& e = list.front();
                const float level = prologue_ ? 1.0f : e.src.points.empty() ? 0.0f : e.src.points.front().weight * e.gain;
                o.left += level, o.right += level;
                continue;
            }
            float doppler_sum = 0;
            for (const Emitter& e : list) {
                float doppler = 0;
                for (const gt3r::sound::CrssEmitter::Point& p : e.src.points) {
                    race::Vec3 d{p.x - at.position.x, p.y - at.position.y, p.z - at.position.z};
                    float dist = std::sqrt(dot(d, d));
                    if (dist == 0) dist = 1.0f;
                    d = {d.x / dist, d.y / dist, d.z / dist};
                    float w = p.weight * e.gain;
                    if (dist >= kNear) w *= kNear / dist;
                    doppler += (kSound / (kSound - dot(d, at.velocity)) - 1.0f) * w;
                    const float pan = std::clamp((dot(d, at.right) + 1.0f) * 0.5f, 0.0f, 1.0f);
                    o.left += std::sin((1.0f - pan) * kHalfPi) * w;
                    o.right += std::sin(pan * kHalfPi) * w;
                }
                if (!e.src.points.empty()) doppler_sum += doppler / float(e.src.points.size());
            }
            if (c == Crowds || prologue_) o.pitch += doppler_sum / float(list.size());
        }
        switch (at.road_flags & 0x18) {
        case 0x08: tunnel1_ = 1.0f, target = 0.5f; break;
        case 0x10: tunnel2_ = 1.0f, target = 0.25f; break;
        case 0x18: target = 0.25f; break;
        default: break;
        }
    }
    for (Out& o : out_) {
        o.left = std::clamp(o.left, -1.0f, 1.0f);
        o.right = std::clamp(o.right, -1.0f, 1.0f);
        if (listeners.size() > 1) o.left = o.right = std::max(o.left, o.right), o.pitch = 1.0f;
    }
    if (listeners.size() > 1) target = 0.125f;
    target_ = target;
    reverb_ = reverb_ < target ? std::min(reverb_ + kReverbStep, target) : std::max(reverb_ - kReverbStep, target);
}

} // namespace gt4r::audio
