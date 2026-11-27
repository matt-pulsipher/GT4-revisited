// Gameplay videos (src/gt4r/video/video_recorder.h): the sound file's header, the names beside the video, ffmpeg's command.

#include "test_framework.h"

#include "audio/voice_mixer.h"
#include "gt4r/video/video_recorder.h"

using gt4r::VideoRecorder;

TEST(video_recorder_wav_header) {
    const std::vector<gt3r::u8> h = VideoRecorder::wav_header(48000 * 4);
    CHECK(h.size() == 44);
    const auto u32_at = [&h](size_t o) { return gt3r::u32(h[o]) | gt3r::u32(h[o + 1]) << 8 | gt3r::u32(h[o + 2]) << 16 | gt3r::u32(h[o + 3]) << 24; };
    const auto u16_at = [&h](size_t o) { return gt3r::u16(h[o] | h[o + 1] << 8); };
    CHECK(std::string(h.begin(), h.begin() + 4) == "RIFF" && std::string(h.begin() + 8, h.begin() + 16) == "WAVEfmt ");
    CHECK(u32_at(4) == 36 + 48000 * 4);
    CHECK(u32_at(16) == 16 && u16_at(20) == 1 && u16_at(22) == 2); // PCM, stereo
    CHECK(u32_at(24) == 48000 && u32_at(28) == 48000 * 4 && u16_at(32) == 4 && u16_at(34) == 16);
    CHECK(std::string(h.begin() + 36, h.begin() + 40) == "data" && u32_at(40) == 48000 * 4);
    // The frame's share of the voice mixer's output: 800 samples, a 60th of a second.
    CHECK(VideoRecorder::kRate == gt3r::VoiceMixer::kRate);
    CHECK(VideoRecorder::kSamplesPerFrame * VideoRecorder::kFps == int(gt3r::VoiceMixer::kRate));
}

TEST(video_recorder_names) {
    CHECK(VideoRecorder::stem_path("out.mkv", "sfx") == "out.sfx.wav");
    CHECK(VideoRecorder::stem_path("/a.b/out", "sfx") == "/a.b/out.sfx.wav");
    CHECK(VideoRecorder::stem_path("clips/race.1.mkv", "sfx") == "clips/race.1.sfx.wav");
    const std::string cmd = VideoRecorder::ffmpeg_command(2560, 1440, "it's.mkv", "");
    CHECK(cmd.find("-s 2560x1440 -framerate 60 -i - ") != std::string::npos);
    CHECK(cmd.find("bt709") != std::string::npos);
#ifndef _WIN32
    const std::string quoted = " 'it'\\''s.mkv'"; // (the name's quote closed, an escaped one, opened again)
    CHECK(cmd.size() > quoted.size() && cmd.substr(cmd.size() - quoted.size()) == quoted);
#endif
    const std::string own = VideoRecorder::ffmpeg_command(8, 6, "o.mkv", "-c:v ffv1");
    CHECK(own.find("-i - -c:v ffv1 'o.mkv'") != std::string::npos || own.find("-i - -c:v ffv1 \"o.mkv\"") != std::string::npos);
}
