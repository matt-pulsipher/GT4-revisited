#pragma once

// Measurements of recorded or rendered sound, for comparing ours with the original's
// (`gt4tool sound compare`, tests): levels, spectra on a logarithmic frequency scale, the pitch
// ratio between two sounds, a pitch track, sudden starts (onsets) and where one recording sits
// in another. Nothing here plays sound.

#include "gt4r/base/types.h"

#include <cmath>

#include <string>
#include <vector>

namespace gt4r::audio {

// Two sides of samples, -1..1.
struct Signal {
    int rate = 48000;
    std::vector<float> left, right;
    size_t frames() const { return left.size(); }
    double seconds() const { return rate > 0 ? double(left.size()) / rate : 0; }
};

// A WAV file of 16-bit, 24-bit or 32-bit float samples, one or two sides (one: both sides
// alike); throws on anything else.
Signal read_wav(ByteSpan file);
// Interleaved 16-bit stereo, as the renderers make it.
Signal signal_from(const std::vector<s16>& stereo, int rate = 48000);
// The stretch from `from` to `to` seconds (to <= 0: the end).
Signal slice(const Signal& s, double from, double to = 0);
// The same sound at another rate (linear interpolation).
Signal resample(const Signal& s, int rate);

struct Loudness {
    double rms_db = -120, left_db = -120, right_db = -120, peak_db = -120; // dB of full scale
    double balance_db = 0;                                                 // right over left
    double side_correlation = 0; // of the two sides' waveforms: 1 alike, 0 unrelated, below 0 one inverted
};
Loudness loudness(const Signal& s);
inline double to_db(double power_ratio) { return power_ratio > 1e-12 ? 10.0 * std::log10(power_ratio) : -120.0; }

// The mean power spectrum of a stretch on a logarithmic scale: `per_octave` bands from `lo` Hz
// up to `hi` Hz, each in dB of full scale (the mean of the two sides' powers, so a side played
// inverted is not lost). Windows of `window` samples.
struct LogSpectrum {
    double lo = 40, hi = 12000;
    int per_octave = 48;
    std::vector<float> db;
    double frequency(double band) const;
    // The band holding the most power between two frequencies.
    double peak(double from_hz, double to_hz) const;
    // The power-weighted mean frequency (Hz).
    double centroid() const;
};
LogSpectrum log_spectrum(const Signal& s, int per_octave = 48, double lo = 40, double hi = 12000, int window = 8192);

// How much higher `a` is pitched than `b`: the frequency ratio that best lays a's spectrum over
// b's (their shapes correlated with the means taken off), searched up to `max_semitones` either
// way. `score`: that correlation, -1..1.
double pitch_ratio(const LogSpectrum& a, const LogSpectrum& b, double max_semitones = 12, double* score = nullptr);
// The correlation of two spectra's shapes as they are (no shift).
double spectrum_match(const LogSpectrum& a, const LogSpectrum& b);

// A note's fundamental over time, `hop` seconds apart: the frequency between lo and hi whose
// first `harmonics` multiples hold the most (log) power. 0 where the stretch is silent.
std::vector<double> pitch_track(const Signal& s, double lo, double hi, double hop = 0.05, int harmonics = 6, int window = 8192);

// Sudden rises of the power between lo and hi Hz: their times (seconds) and how many dB they
// rose over the 50 ms before. Two onsets are at least `gap` seconds apart.
struct Onset {
    double time = 0, rise_db = 0;
};
std::vector<Onset> onsets(const Signal& s, double lo = 200, double hi = 8000, double min_rise_db = 9, double gap = 0.1);

// The power between lo and hi Hz over time, `hop` seconds apart (dB of full scale).
std::vector<double> band_track(const Signal& s, double lo, double hi, double hop = 0.05);

// Where `needle` sits in `haystack`: the offset (seconds into the haystack) at which their
// spectra over time correlate best, and that correlation. Spectra in 1/6 octaves, every 50 ms.
struct Alignment {
    double offset = 0, score = -1;
};
std::vector<Alignment> align(const Signal& needle, const Signal& haystack, int best = 3);

// The same sound found sample by sample: `needle` (a piece of music as stored, say) placed at
// `approx` seconds into `haystack` (a recording that holds it, mixed with other sounds), the
// offset refined within `search` seconds by the waveforms' correlation, then the gain of each
// side that best scales the needle to the haystack (least squares) and how much of the
// haystack's power that explains (0..1), all between lo and hi Hz (by default under 1 kHz, where
// two resamplings of one sound still agree sample by sample).
struct Fit {
    double offset = 0;
    double gain_left = 0, gain_right = 0;
    double explained = 0;
    double correlation = 0; // of the waveforms at that offset
};
Fit fit(const Signal& needle, const Signal& haystack, double approx, double search = 0.06, double lo = 0, double hi = 1000);

} // namespace gt4r::audio
