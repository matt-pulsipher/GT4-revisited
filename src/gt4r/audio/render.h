#pragma once

// Sound rendered to memory through a mixer of its own, for the tools and the tests: nothing
// here opens an audio device.

#include "gt4r/audio/sound_driver.h"

#include <functional>
#include <vector>

namespace gt4r::audio {

// A voice mixer in the game's boot state (reverb "studio C" at depth 0x0FFF) with a sound
// driver on it, advanced by hand.
class OfflineSound {
public:
    explicit OfflineSound(bool reverb = true);

    gt3r::VoiceMixer& mixer() { return mixer_; }
    SoundDriver& driver() { return driver_; }

    // Runs `updates` steps of the driver, appending the stereo samples (48 kHz, interleaved).
    // `each`, when given, runs before every step.
    void advance(int updates, const std::function<void(int)>& each = {});
    void advance_seconds(double seconds, const std::function<void(int)>& each = {}) {
        advance(int(seconds * SoundDriver::kUpdateRate + 0.5), each);
    }

    std::vector<s16> pcm;
    int max_voices = 0;

private:
    gt3r::VoiceMixer mixer_;
    SoundDriver driver_;
};

struct MusicRender {
    std::vector<s16> pcm;          // 48 kHz stereo, interleaved
    std::vector<u64> loop_frames;  // the frame of each pass through the loop's end
    u64 end_frame = 0;             // where the sequence was stopped
    int max_voices = 0;
};
// Plays a sequence until its loop has come round `loops` times (or it ends, or `max_seconds`),
// then lets `tail_seconds` of release and reverb ring. `volume`: the music's master volume (0..1;
// the menus play at the mix table's sequence level x the music option, MenuSound).
MusicRender render_music(std::shared_ptr<const sound::InstBank> bank, std::shared_ptr<const sound::SqtBank> sequences, int number,
                         int loops = 1, double max_seconds = 600, double tail_seconds = 2, float volume = 1.0f);

// What a rendering measures.
struct Level {
    int peak = 0;       // largest |sample|
    double rms = 0;     // of all samples, 0..1
    size_t clipped = 0; // samples at full scale
    double seconds = 0;
    // The last moment above 1/1000 of full scale, in seconds.
    double sounding_until = 0;
};
Level measure(const std::vector<s16>& stereo);
// The strongest frequency of a stretch of the left side (Hz), by autocorrelation over
// `lo`..`hi` Hz; 0 for silence.
double dominant_frequency(const std::vector<s16>& stereo, size_t first_frame, size_t frames, double lo, double hi);

// How far a piece of music sits from concert pitch (A = 440 Hz, equal temperament), in cents,
// -50..+40 in steps of 10: the offset at which the notes' frequencies between 440 and 1760 Hz
// hold the most energy. `contrast`: that energy over the mean of the other offsets' (1: no
// tuning to be found; well above 1: clearly tuned).
int tuning_offset_cents(const std::vector<s16>& stereo, size_t first_frame, size_t frames, double* contrast = nullptr);

// A 16-bit stereo 48 kHz WAV file's bytes.
Bytes wav_file(const std::vector<s16>& stereo, u32 rate = gt3r::VoiceMixer::kRate);

} // namespace gt4r::audio
