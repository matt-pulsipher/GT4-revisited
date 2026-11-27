#pragma once

// Gameplay videos: `gt4r <disc> --record <out.mkv>` (docs/PLAYING.md "Gameplay videos").
//
// A recorded run is a scripted one (no window, no real input): the app steps exactly a 60th of a second a frame, as with
// --shot, and every finished frame (the HUD and the pages as drawn) goes through a pipe to ffmpeg (on PATH). The sound is
// rendered in step with the frames: the voice mixer (engines, tires, effects, the menus' sounds) gives 800 samples a frame
// into <out>.sfx.wav beside the video. No music is recorded: the run's Music volume is 0 (the sequenced music too), and the
// streamed music, never pulled by a device, is not heard either. Nothing plays on the speakers: a recorded run opens no
// sound device.
//
// GT4R_RECORD_CODEC replaces ffmpeg's output options (default: the VideoToolbox H.264 encoder at 80 Mbit/s on macOS, x264
// CRF 12 elsewhere; BT.709 tags). GT4R_RECORD_REALTIME=1 holds the frames to the wall clock as a 60 Hz schedule (a late
// frame is made up by the next ones; the log tells how late they got), for what goes by real time: an online race.

#include "gt4r/base/types.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

namespace gt3r {
class VoiceMixer;
}

namespace gt4r {

class VideoRecorder {
public:
    static constexpr int kFps = 60;
    static constexpr u32 kRate = 48000;                     // the voice mixer's rate (VoiceMixer::kRate)
    static constexpr int kSamplesPerFrame = int(kRate) / kFps; // 800

    explicit VideoRecorder(std::string path);
    ~VideoRecorder() { finish(); }
    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    // After the frame is drawn, before it is presented: the frame, and its 60th of a second of sound from `voices` (null:
    // silence).
    void frame(SDL_Renderer* renderer, gt3r::VoiceMixer* voices);
    void finish();
    int frames() const { return frames_; }
    bool realtime() const { return realtime_; }
    bool failed() const { return failed_; }

    // "out.mkv" -> "out.sfx.wav"
    static std::string stem_path(const std::string& path, const char* stem);
    // A WAV file's 44-byte header for 16-bit stereo PCM at kRate holding `data_bytes` of samples.
    static std::vector<u8> wav_header(u32 data_bytes);
    // The ffmpeg command for `w` x `h` RGBA frames into `path` with the output options `options` (empty: the default's).
    static std::string ffmpeg_command(int w, int h, const std::string& path, const std::string& options);

private:
    struct Wav {
        std::FILE* f = nullptr;
        u32 bytes = 0;
        bool open(const std::string& path);
        void write(const s16* samples, size_t count);
        void close();
    };
    bool start(int w, int h);
    void pace();

    std::string path_;
    std::FILE* pipe_ = nullptr;
    bool failed_ = false;
    int w_ = 0, h_ = 0; // the first frame's size (later frames are cropped or padded to it)
    int frames_ = 0;
    bool realtime_ = false;
    Uint64 t0_ = 0;          // realtime: when frame 0 was due
    Uint64 max_late_ns_ = 0; // realtime: the latest a frame started (after a stall moved the schedule)
    int late_frames_ = 0, reanchors_ = 0;
    Wav sfx_;
    std::vector<s16> sfx_buf_;
    std::vector<u8> frame_buf_;
};

} // namespace gt4r
