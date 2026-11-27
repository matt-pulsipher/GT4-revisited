#include "gt4r/video/video_recorder.h"

#include "audio/voice_mixer.h"
#include "base/log.h"

#include <algorithm>
#include <csignal>
#include <cstring>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace gt4r {

namespace {

void put_u32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i)));
}

void put_u16(std::vector<u8>& b, u16 v) { b.push_back(u8(v)), b.push_back(u8(v >> 8)); }

std::string shell_quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
#endif
}

} // namespace

std::string VideoRecorder::stem_path(const std::string& path, const char* stem) {
    const size_t dot = path.rfind('.'), slash = path.find_last_of("/\\");
    const bool ext = dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (ext ? path.substr(0, dot) : path) + "." + stem + ".wav";
}

std::vector<u8> VideoRecorder::wav_header(u32 data_bytes) {
    std::vector<u8> h;
    const auto tag = [&h](const char* t) { h.insert(h.end(), t, t + 4); };
    tag("RIFF"), put_u32(h, 36 + data_bytes), tag("WAVE"), tag("fmt ");
    put_u32(h, 16), put_u16(h, 1), put_u16(h, 2), put_u32(h, kRate), put_u32(h, kRate * 4), put_u16(h, 4), put_u16(h, 16);
    tag("data"), put_u32(h, data_bytes);
    return h;
}

std::string VideoRecorder::ffmpeg_command(int w, int h, const std::string& path, const std::string& options) {
    const std::string tags = " -colorspace bt709 -color_primaries bt709 -color_trc bt709";
    const std::string convert = " -vf scale=out_color_matrix=bt709:out_range=tv -pix_fmt yuv420p";
#if defined(__APPLE__)
    const std::string fallback = "-c:v h264_videotoolbox -b:v 80M -maxrate 120M -bufsize 160M -g 60" + convert + tags;
#else
    const std::string fallback = "-c:v libx264 -preset veryfast -crf 12" + convert + tags;
#endif
    return "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgba -s " + std::to_string(w) + "x" + std::to_string(h) + " -framerate " +
           std::to_string(kFps) + " -i - " + (options.empty() ? fallback : options) + " " + shell_quote(path);
}

VideoRecorder::VideoRecorder(std::string path) : path_(std::move(path)) {
    const char* rt = SDL_getenv("GT4R_RECORD_REALTIME");
    realtime_ = rt && *rt == '1';
}

bool VideoRecorder::Wav::open(const std::string& path) {
    f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::vector<u8> h = wav_header(0); // (the sizes filled in by close())
    std::fwrite(h.data(), 1, h.size(), f);
    return true;
}

void VideoRecorder::Wav::write(const s16* samples, size_t count) {
    if (!f) return;
    std::vector<u8> b;
    b.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) put_u16(b, u16(samples[i])); // little-endian on every host
    std::fwrite(b.data(), 1, b.size(), f);
    bytes += u32(b.size());
}

void VideoRecorder::Wav::close() {
    if (!f) return;
    const std::vector<u8> h = wav_header(bytes);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite(h.data(), 1, h.size(), f);
    std::fclose(f);
    f = nullptr;
}

bool VideoRecorder::start(int w, int h) {
    const char* codec = SDL_getenv("GT4R_RECORD_CODEC");
    const std::string cmd = ffmpeg_command(w, h, path_, codec ? codec : "");
#if defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    LOG_ERROR("Recording: not available on this platform");
    return false;
#else
#if defined(_WIN32)
    pipe_ = popen(cmd.c_str(), "wb");
#else
    std::signal(SIGPIPE, SIG_IGN); // (an ffmpeg that stops, missing or given bad options, does not end the game)
    pipe_ = popen(cmd.c_str(), "w");
#endif
    if (!pipe_) {
        LOG_ERROR("Recording: cannot start ffmpeg (%s)", cmd.c_str());
        return false;
    }
    if (!sfx_.open(stem_path(path_, "sfx"))) {
        LOG_ERROR("Recording: cannot write the sound beside %s", path_.c_str());
        return false;
    }
    w_ = w, h_ = h;
    LOG_INFO("Recording %dx%d at %d frames a second to %s%s", w, h, kFps, path_.c_str(), realtime_ ? " (held to the clock)" : "");
    return true;
#endif
}

void VideoRecorder::frame(SDL_Renderer* renderer, gt3r::VoiceMixer* voices) {
    if (failed_) return;
    SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
    SDL_Surface* rgba = shot ? SDL_ConvertSurface(shot, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (shot) SDL_DestroySurface(shot);
    if (!rgba) return;
    if (!pipe_) failed_ = !start(rgba->w & ~1, rgba->h & ~1); // (even sizes, for 4:2:0)
    if (pipe_ && !failed_) {
        // Every frame keeps the first frame's size (a frame of another size is cropped or padded).
        const size_t row = size_t(w_) * 4;
        frame_buf_.assign(row * size_t(h_), 0);
        const size_t copy = size_t(std::min(w_, rgba->w)) * 4;
        for (int y = 0; y < std::min(h_, rgba->h); ++y)
            std::memcpy(frame_buf_.data() + row * size_t(y), static_cast<const u8*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), copy);
        if (std::fwrite(frame_buf_.data(), 1, frame_buf_.size(), pipe_) != frame_buf_.size()) {
            LOG_ERROR("Recording: ffmpeg stopped taking frames (is it installed? GT4R_RECORD_CODEC?)");
            failed_ = true;
        }
    }
    SDL_DestroySurface(rgba);
    if (!pipe_ || failed_) return;
    // The frame's 60th of a second of sound, so that sound and picture keep step.
    sfx_buf_.assign(size_t(kSamplesPerFrame) * 2, 0);
    if (voices) voices->mix(sfx_buf_.data(), size_t(kSamplesPerFrame));
    sfx_.write(sfx_buf_.data(), sfx_buf_.size());
    ++frames_;
    if (realtime_) pace();
}

// The next frame starts at its slot on a 60 Hz schedule: an early frame waits, a late one is made up by those after it. A stall
// of more than a quarter of a second (a load) moves the schedule instead.
void VideoRecorder::pace() {
    constexpr Uint64 kFrameNs = 1'000'000'000ull / kFps;
    const Uint64 now = SDL_GetTicksNS();
    if (!t0_) t0_ = now - Uint64(frames_) * kFrameNs;
    const Uint64 due = t0_ + Uint64(frames_) * kFrameNs;
    if (now < due) {
        SDL_DelayPrecise(due - now);
        return;
    }
    const Uint64 late = now - due;
    if (late > kFrameNs * 15) {
        t0_ += late, ++reanchors_;
        return;
    }
    if (late > kFrameNs) ++late_frames_;
    max_late_ns_ = std::max(max_late_ns_, late);
}

void VideoRecorder::finish() {
    sfx_.close();
    if (!pipe_) return;
    const int rc = pclose(pipe_);
    pipe_ = nullptr;
    LOG_INFO("Recording finished: %d frames (%.2f s), ffmpeg exit %d", frames_, double(frames_) / kFps, rc);
    if (realtime_)
        LOG_INFO("Recording held to the clock: the latest frame %.1f ms behind, %d frames over a 60th of a second late, %d stalls passed over",
                 double(max_late_ns_) / 1e6, late_frames_, reanchors_);
}

} // namespace gt4r
