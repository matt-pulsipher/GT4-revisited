#include "gt4r/audio/layer_player.h"

#include "gt4r/formats/sound/inst_bank.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace gt4r::audio {

using gt3r::sound::EngineLayer;

namespace {

constexpr int kFull = 0x3FFF, kStep = 0x1000;

// A side's size moves at most kStep a step; the sign is the new one's.
s16 limit_side(int now, int last) {
    const bool negative = now < 0;
    int size = std::abs(now);
    const int was = std::abs(last);
    size = std::clamp(size, was - kStep, was + kStep);
    return s16(negative ? -size : size);
}

// The mixer's priority (0 keeps its voice against everything) from the player's.
u8 mixer_priority(u32 priority) { return u8(255 - std::min<u32>(priority >> 23, 255)); }

} // namespace

int LayerPlayer::crossfade(int i) {
    static const std::array<s16, 1025> table = [] {
        std::array<s16, 1025> t{};
        for (int k = 0; k <= 1024; ++k) t[size_t(k)] = s16(std::lround(4096.0 * std::sin(double(k) / 1024.0 * std::numbers::pi / 2)));
        return t;
    }();
    return table[size_t(std::clamp(i, 0, 1024))];
}

bool LayerPlayer::choose(const std::vector<EngineLayer>& layers, int rpm, int& want0, int& want1) {
    rpm = std::max(rpm, kMinRpm);
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i].lo_rpm <= rpm && rpm <= layers[i].hi_rpm) {
            want0 = int(i), want1 = -1;
            return true;
        }
        if (i > 0 && layers[i - 1].hi_rpm < rpm && rpm < layers[i].lo_rpm) {
            want0 = int(i - 1), want1 = int(i);
            return true;
        }
    }
    return false;
}

int LayerPlayer::layer_pitch(const EngineLayer& layer, int rpm, int last) {
    const u32 base = u32(s32(layer.base_rpm == 0 ? 1 : s16(layer.base_rpm)));
    int pitch = int(u32(layer.pitch * u32(rpm)) / base);
    if (pitch <= 0) pitch = 1;
    if (last > 0) pitch = std::clamp(pitch, last >> 1, last << 1);
    return pitch;
}

void LayerPlayer::layer_levels(const EngineLayer& layer, const EngineLayer* other, int rpm, s16 left, s16 right, s16& out_left, s16& out_right,
                               const Slot* last) {
    int x = 0x4000;
    if (other) {
        if (rpm < layer.lo_rpm) {
            const int gap = layer.lo_rpm - other->hi_rpm;
            x = gap ? ((rpm - other->hi_rpm) << 14) / gap : 0x4000;
        } else if (layer.hi_rpm < rpm) {
            const int gap = other->lo_rpm - layer.hi_rpm;
            x = gap ? ((other->lo_rpm - rpm) << 14) / gap : 0x4000;
        }
    }
    x = std::max(x, 0);
    const int g = (crossfade(x >> 4) * kFull) >> 12;
    const int level = (int(layer.volume) * g) >> 14;
    int l = (int(left) * level) >> 14, r = (int(right) * level) >> 14;
    if (last) l = limit_side(l, last->left), r = limit_side(r, last->right);
    out_left = s16(l), out_right = s16(r);
}

void LayerPlayer::voice_levels(int l, int r, bool mono, s16& out_l, s16& out_r) {
    // A kind 4 voice: base 0x3FFF, sequence 0x3FFF, the kind's master 0x3FFF, the three 0x7F
    // factors, the center pan.
    int t = (kFull * kFull) >> 14;
    t = (t * kFull) >> 14;
    int pan_l, pan_r;
    sound::pan_gains(0x40, pan_l, pan_r);
    const auto side = [&](int pan, int v) { return std::clamp(((t * pan / 127) * v) >> 14, -0x4000, kFull); };
    int left = side(pan_l, l), right = side(pan_r, r);
    if (mono) left = right = (std::abs(left) + std::abs(right)) / 2;
    out_l = s16(left), out_r = s16(right);
}

void LayerPlayer::set_bank(std::shared_ptr<const gt3r::sound::EngineBank> bank) {
    bank_ = std::move(bank);
    input_ = Input{};
    slots_ = {};
}

void LayerPlayer::stop(gt3r::VoiceMixer& mixer) {
    for (Slot& s : slots_) {
        if (s.sounding) mixer.key_off(s.voice);
        s = Slot{};
    }
}

void LayerPlayer::step(gt3r::VoiceMixer* mixer, bool mono) {
    if (!bank_ || bank_->layers.empty()) return;
    const std::vector<EngineLayer>& layers = bank_->layers;
    const int rpm = std::max(input_.rpm, kMinRpm);
    int want0 = slots_[0].want, want1 = slots_[1].want;
    choose(layers, rpm, want0, want1);
    // A slot keeps the layer it plays when that layer is still wanted.
    if (want0 == slots_[1].layer || want1 == slots_[0].layer) std::swap(want0, want1);
    slots_[0].want = want0, slots_[1].want = want1;

    for (int k = 0; k < 2; ++k) {
        Slot& s = slots_[size_t(k)];
        if (mixer && s.sounding && !mixer->playing(s.voice)) s.sounding = false, s.voice = {}; // (taken by another sound)
        const int other = slots_[size_t(k ^ 1)].want;
        const EngineLayer* far = other >= 0 && size_t(other) < layers.size() ? &layers[size_t(other)] : nullptr;
        bool fresh = false;
        if (s.want == s.layer && s.sounding) {
            if (s.want < 0) continue;
            const EngineLayer& layer = layers[size_t(s.want)];
            s.pitch = layer_pitch(layer, rpm, s.pitch);
            const Slot last = s;
            layer_levels(layer, far, rpm, input_.left, input_.right, s.left, s.right, &last);
        } else {
            if (s.want != s.layer && s.sounding) {
                if (mixer) mixer->key_off(s.voice);
                s.voice = {}, s.sounding = false;
            }
            s.layer = s.want;
            if (s.want < 0 || size_t(s.want) >= layers.size()) continue;
            const EngineLayer& layer = layers[size_t(s.want)];
            s.pitch = layer_pitch(layer, rpm);
            layer_levels(layer, far, rpm, input_.left, input_.right, s.left, s.right);
            fresh = true;
        }
        if (!mixer) {
            s.sounding = true; // (the slots alone)
            continue;
        }
        // The voice: the slot's pitch x the Doppler factor straight to the SPU2, its levels the
        // driver's for kind 4.
        const u32 spu = std::min<u32>((u32(u16(s.pitch)) * u32(std::clamp(input_.doppler, 0, 0x3FFF))) >> 12 & 0xFFFF, 0x3FFF);
        s16 l, r;
        voice_levels(s.left, s.right, mono, l, r);
        const u16 pitch = sound::game_pitch_for_spu(u16(spu));
        if (fresh) {
            gt3r::VoiceParams p;
            p.data = bank_->adpcm;
            p.start = layers[size_t(s.want)].offset;
            p.pitch = pitch;
            p.vol_l = l, p.vol_r = r;
            p.adsr1 = kAdsr1, p.adsr2 = kAdsr2;
            p.reverb = !input_.dry;
            p.priority = mixer_priority(input_.priority);
            s.voice = mixer->key_on(p);
            s.sounding = s.voice.valid();
        } else {
            mixer->set_pitch(s.voice, pitch);
            mixer->set_volume(s.voice, l, r);
            mixer->set_priority(s.voice, mixer_priority(input_.priority));
        }
    }
}

} // namespace gt4r::audio
