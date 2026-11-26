#pragma once

// Gran Turismo 4's music streams on two SPU2 voices (docs/formats/sound.md "Streams"): the IOP's
// stream module plays a PS-ADPCM ".ads" piece on core 1's voices 22 and 23, one side each, at the
// SPU2 pitch rate x 4096 / 48000 (3763 for 44.1 kHz), the voices' envelope 0x000F / 0x0000, no
// reverb send, each voice's level the stream's level x 16384 on its own side (4416 for the menus'
// 0.3 x 115 / 128 in the saved states). The SPU2's Gaussian interpolation is what the original
// sounds like: its pieces lose 1 to 6 dB from 4 kHz up against a plain resampling.
//
// Here the two voices are the shared mixer's, on its music bus. A piece is read from the VFS
// (mods included) into one buffer per side: the first two seconds at once, the rest on a thread
// of its own ahead of the voices. play() refuses what is not PS-ADPCM (the caller plays it
// another way).

#include "audio/voice_mixer.h"
#include "gt4r/base/types.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace gt3r {
class Vfs;
} // namespace gt3r

namespace gt4r::audio {

class StreamVoices {
public:
    StreamVoices(gt3r::VoiceMixer& mixer, gt3r::Vfs& vfs);
    ~StreamVoices();
    StreamVoices(const StreamVoices&) = delete;
    StreamVoices& operator=(const StreamVoices&) = delete;

    // Starts `path` (an .ads) from its start, stopping the piece playing; `repeat`: from the start
    // again at its end. False (and nothing playing) when the file is missing or not PS-ADPCM.
    bool play(const std::string& path, bool repeat, float level);
    void stop();
    // The race's pause: the voices hold where they are, silent.
    void pause(bool paused);
    bool playing() const;

    // The SPU2 level of a stream's voice at `level` (the saved states' 4416 for 0.26953125).
    static s16 voice_level(float level);

private:
    void join();

    gt3r::VoiceMixer& mixer_;
    gt3r::Vfs& vfs_;
    gt3r::VoiceId voice_[2];
    s16 level_[2] = {0, 0}; // each voice's level on its side
    u16 pitch_ = 0x1000;
    int sides_ = 0;
    bool mono_ = false; // a one-sided piece: one voice on both sides
    bool paused_ = false;
    std::thread loader_;
    std::shared_ptr<std::atomic<bool>> cancel_;
};

} // namespace gt4r::audio
