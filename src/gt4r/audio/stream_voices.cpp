#include "gt4r/audio/stream_voices.h"

#include "base/log.h"
#include "formats/ads.h"
#include "gt4r/formats/sound/inst_bank.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace gt4r::audio {

namespace {

constexpr u32 kAdpcm = 0x10;
constexpr u8 kLoopStart = 0x04, kEnd = 0x01, kEndRepeat = 0x03;
constexpr u16 kAdsr1 = 0x000F, kAdsr2 = 0x0000; // (the stream's two voices in the saved states)
constexpr double kAtOnce = 2.0;                 // seconds read before the voices start
constexpr size_t kChunk = 64;                   // blocks the loader reads at a time

} // namespace

StreamVoices::StreamVoices(gt3r::VoiceMixer& mixer, gt3r::Vfs& vfs) : mixer_(mixer), vfs_(vfs) {}

StreamVoices::~StreamVoices() { stop(); }

s16 StreamVoices::voice_level(float level) { return s16(std::clamp(int(std::max(level, 0.0f) * 16384.0f), 0, 0x3FFF)); }

void StreamVoices::join() {
    if (cancel_) cancel_->store(true);
    if (loader_.joinable()) loader_.join();
    cancel_.reset();
}

void StreamVoices::stop() {
    join();
    for (gt3r::VoiceId& v : voice_) {
        if (v.valid()) mixer_.stop(v);
        v = {};
    }
    sides_ = 0;
    paused_ = false;
}

bool StreamVoices::play(const std::string& path, bool repeat, float level) {
    stop();
    std::optional<gt3r::Vfs::Stream> s = vfs_.open_stream(path);
    if (!s || s->size < gt3r::AdsHeader::kHeaderBytes) return false;
    gt3r::AdsHeader h;
    try {
        Bytes head(gt3r::AdsHeader::kHeaderBytes);
        s->read(0, head.data(), head.size());
        h = gt3r::AdsHeader::parse(head);
    } catch (const std::exception&) {
        return false;
    }
    if (h.codec != kAdpcm || h.channels < 1 || h.channels > 2 || h.interleave == 0 || h.interleave % 16 || h.sample_rate == 0) return false;
    if (h.body_offset >= s->size) return false;
    const u64 size = std::min<u64>(h.body_size, s->size - h.body_offset);
    const size_t il = h.interleave, sides = h.channels;
    const size_t blocks = size_t(size / (il * sides));
    if (blocks == 0) return false;

    // One buffer a side: block k of side c is the body's block k x sides + c.
    std::array<std::shared_ptr<Bytes>, 2> side;
    for (size_t c = 0; c < sides; ++c) side[c] = std::make_shared<Bytes>(blocks * il, u8(0));
    const u64 body = h.body_offset;
    const auto load = [side, sides, il, blocks, body, repeat](const gt3r::Vfs::Stream& from, size_t k0, size_t k1) {
        for (size_t k = k0; k < k1; ++k)
            for (size_t c = 0; c < sides; ++c) {
                u8* to = side[c]->data() + k * il;
                from.read(body + (k * sides + c) * il, to, il);
                for (size_t f = 0; f < il; f += 16) to[f + 1] = 0; // (the IOP's ring buffer's flags: none here)
                if (k == 0 && repeat) to[1] = kLoopStart;
                if (k + 1 == blocks) to[il - 16 + 1] = repeat ? kEndRepeat : kEnd;
            }
    };
    const double per_second = double(h.sample_rate) / 28.0 * 16.0; // bytes a side
    const size_t first = std::min(blocks, size_t(std::ceil(kAtOnce * per_second / double(il))) + 1);
    try {
        load(*s, 0, first);
    } catch (const std::exception& e) {
        LOG_WARN("Sound: cannot read %s: %s", path.c_str(), e.what());
        return false;
    }

    // The voices: the SPU2 pitch rate x 4096 / 48000, each side's level on its side.
    const u16 spu = u16(std::min<u32>(u32(u64(h.sample_rate) * 4096 / 48000), 0x3FFF));
    pitch_ = sound::game_pitch_for_spu(spu);
    const s16 v = voice_level(level);
    mono_ = sides == 1;
    sides_ = int(sides);
    for (size_t c = 0; c < sides; ++c) {
        gt3r::VoiceParams p;
        p.data = side[c];
        p.start = 0;
        p.pitch = pitch_;
        p.vol_l = mono_ || c == 0 ? v : 0;
        p.vol_r = mono_ || c == 1 ? v : 0;
        level_[c] = v;
        p.adsr1 = kAdsr1, p.adsr2 = kAdsr2;
        p.reverb = false;
        p.priority = 0; // (the stream's voices are its own, never another sound's)
        p.bus = gt3r::VoiceBus::Music;
        voice_[c] = mixer_.key_on(p);
    }
    if (!voice_[0].valid()) {
        stop();
        return false;
    }

    // The rest ahead of the voices. Each chunk is handed over through the mixer's lock (playing()), which
    // the mixer takes before it renders a block.
    if (first < blocks) {
        cancel_ = std::make_shared<std::atomic<bool>>(false);
        loader_ = std::thread([this, from = *s, load, first, blocks, cancel = cancel_, path] {
            try {
                for (size_t k = first; k < blocks && !cancel->load(); k += kChunk) {
                    load(from, k, std::min(blocks, k + kChunk));
                    (void)mixer_.playing(voice_[0]);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Sound: cannot read %s: %s", path.c_str(), e.what());
            }
        });
    }
    return true;
}

void StreamVoices::pause(bool paused) {
    if (paused == paused_ || !sides_) return;
    paused_ = paused;
    for (int c = 0; c < sides_; ++c) {
        if (!voice_[c].valid()) continue;
        if (paused) {
            mixer_.set_volume(voice_[c], 0, 0);
            mixer_.set_pitch(voice_[c], 0);
        } else {
            mixer_.set_pitch(voice_[c], pitch_);
            mixer_.set_volume(voice_[c], mono_ || c == 0 ? level_[c] : 0, mono_ || c == 1 ? level_[c] : 0);
        }
    }
}

bool StreamVoices::playing() const {
    for (int c = 0; c < sides_; ++c)
        if (voice_[c].valid() && mixer_.playing(voice_[c])) return true;
    return false;
}

} // namespace gt4r::audio
