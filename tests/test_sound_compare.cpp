// The measurements `gt4tool sound compare` makes, on made-up signals whose answers are known.

#include "test_framework.h"

#include "gt4r/audio/analysis.h"
#include "gt4r/audio/render.h"

#include <cmath>
#include <numbers>

using namespace gt4r;

namespace {

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

// A tone of harmonics 1..n (amplitude 1/h each), `seconds` long, both sides at `left` / `right`.
audio::Signal tone(double f, double seconds, int harmonics = 1, float left = 0.5f, float right = 0.5f) {
    audio::Signal s;
    s.left.resize(size_t(seconds * s.rate)), s.right.resize(s.left.size());
    for (size_t i = 0; i < s.left.size(); ++i) {
        double x = 0;
        for (int h = 1; h <= harmonics; ++h) x += std::sin(2 * std::numbers::pi * f * h * double(i) / s.rate) / h;
        s.left[i] = float(x) * left, s.right[i] = float(x) * right;
    }
    return s;
}

} // namespace

TEST(sound_compare_levels_and_wav) {
    // A sine of amplitude 0.5: mean square 1/8, -9.03 dB; the right side 6 dB under the left.
    const audio::Signal s = tone(1000, 1, 1, 0.5f, 0.25f);
    const audio::Loudness l = audio::loudness(s);
    CHECK(near(l.left_db, -9.03, 0.05));
    CHECK(near(l.balance_db, -6.02, 0.05));
    CHECK(near(l.peak_db, -6.02, 0.05));
    // Through the renderers' 16-bit WAV file and back.
    std::vector<s16> pcm(s.frames() * 2);
    for (size_t i = 0; i < s.frames(); ++i) pcm[2 * i] = s16(std::lround(s.left[i] * 32767)), pcm[2 * i + 1] = s16(std::lround(s.right[i] * 32767));
    const Bytes wav = audio::wav_file(pcm);
    const audio::Signal back = audio::read_wav(ByteSpan(wav));
    CHECK(back.rate == 48000 && back.frames() == s.frames());
    CHECK(near(audio::loudness(back).left_db, l.left_db, 0.01));
    CHECK(near(audio::loudness(audio::slice(back, 0.25, 0.75)).left_db, l.left_db, 0.01));
    CHECK(near(audio::slice(back, 0.25, 0.75).seconds(), 0.5, 1e-3));
}

TEST(sound_compare_spectrum_and_pitch) {
    // The spectrum's strongest band sits at the tone; the centroid of a pure tone is the tone.
    const audio::LogSpectrum a = audio::log_spectrum(tone(440, 2));
    CHECK(near(a.peak(100, 4000), 440, 440 * 0.03));
    CHECK(near(a.centroid(), 440, 440 * 0.05));
    // A rich tone a semitone up is found a semitone up, within 10 cents; the same tone at 0.
    const audio::LogSpectrum b = audio::log_spectrum(tone(220, 2, 8)), c = audio::log_spectrum(tone(220 * std::exp2(1.0 / 12), 2, 8));
    double score = 0;
    CHECK(near(1200 * std::log2(audio::pitch_ratio(c, b, 6, &score)), 100, 10));
    CHECK(score > 0.9);
    CHECK(near(1200 * std::log2(audio::pitch_ratio(b, b, 6)), 0, 1));
    CHECK(audio::spectrum_match(b, b) > 0.999);
    // 37 cents: between the bands, placed by the parabola.
    const audio::LogSpectrum d = audio::log_spectrum(tone(220 * std::exp2(37.0 / 1200), 2, 8));
    CHECK(near(1200 * std::log2(audio::pitch_ratio(d, b, 6)), 37, 10));
}

TEST(sound_compare_pitch_track) {
    // A note gliding from 100 to 200 Hz over 2 s, harmonics 1..6: the track follows within 1%.
    audio::Signal s;
    s.left.resize(size_t(2.0 * s.rate)), s.right.resize(s.left.size());
    double phase = 0;
    for (size_t i = 0; i < s.left.size(); ++i) {
        const double f = 100 * std::exp2(double(i) / double(s.left.size()));
        phase += 2 * std::numbers::pi * f / s.rate;
        double x = 0;
        for (int h = 1; h <= 6; ++h) x += std::sin(phase * h) / h;
        s.left[i] = s.right[i] = float(0.3 * x);
    }
    const auto track = audio::pitch_track(s, 60, 400, 0.1);
    CHECK(track.size() == 20);
    for (size_t i = 3; i + 3 < track.size(); ++i) {
        const double want = 100 * std::exp2(double(i) * 0.1 / 2.0);
        CHECK(near(track[i], want, want * 0.01));
    }
}

TEST(sound_compare_onsets_and_align) {
    // Three beeps at 0.5, 1.5 and 2.5 s over quiet noise: three onsets within 10 ms.
    audio::Signal s;
    s.left.resize(size_t(3.5 * s.rate)), s.right.resize(s.left.size());
    u32 seed = 1;
    for (size_t i = 0; i < s.left.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double t = double(i) / s.rate;
        double x = (double(seed >> 8) / 16777216.0 - 0.5) * 0.002;
        for (double at : {0.5, 1.5, 2.5})
            if (t >= at && t < at + 0.2) x += 0.4 * std::sin(2 * std::numbers::pi * 1500 * t);
        s.left[i] = s.right[i] = float(x);
    }
    const auto on = audio::onsets(s, 500, 4000);
    CHECK(on.size() == 3);
    for (size_t i = 0; i < on.size() && i < 3; ++i) CHECK(near(on[i].time, 0.5 + double(i), 0.01));
    // A stretch of a changing sound is found where it was cut from, to 50 ms.
    audio::Signal music;
    music.left.resize(size_t(12.0 * music.rate)), music.right.resize(music.left.size());
    for (size_t i = 0; i < music.left.size(); ++i) {
        const double t = double(i) / music.rate;
        const u32 k = u32(t * 4) * 2654435761u; // a new note every 250 ms, in no repeating order
        const double f = 200 * std::exp2(double((k >> 27) % 13) / 12.0);
        music.left[i] = music.right[i] = float(0.3 * std::sin(2 * std::numbers::pi * f * t) + 0.1 * std::sin(2 * std::numbers::pi * f * 3 * t));
    }
    const auto found = audio::align(audio::slice(music, 5.0, 8.0), music, 1);
    CHECK(!found.empty() && near(found[0].offset, 5.0, 0.051) && found[0].score > 0.9);
}

TEST(sound_compare_inverted_side) {
    // A side played inverted against the other (the surround cue): the sides correlate -1 and the
    // spectrum keeps the whole power, where a mix of the two sides would cancel.
    audio::Signal same = tone(300, 1, 4), inverted = same;
    for (float& x : inverted.right) x = -x;
    CHECK(near(audio::loudness(same).side_correlation, 1, 1e-6));
    CHECK(near(audio::loudness(inverted).side_correlation, -1, 1e-6));
    const audio::LogSpectrum a = audio::log_spectrum(same), b = audio::log_spectrum(inverted);
    for (size_t i = 0; i < a.db.size(); ++i) CHECK(near(a.db[i], b.db[i], 0.01));
}

TEST(sound_compare_fit_gain) {
    // A piece mixed at 0.25 into a recording with other sound, 1.5 s in: placed to the sample and
    // its gain found.
    const audio::Signal piece = tone(110, 3, 6, 0.4f, 0.4f);
    audio::Signal mix;
    mix.left.assign(size_t(6 * mix.rate), 0.0f), mix.right = mix.left;
    u32 seed = 7;
    for (size_t i = 0; i < mix.frames(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        mix.left[i] = mix.right[i] = (float(seed >> 8) / 16777216.0f - 0.5f) * 0.05f;
    }
    const size_t at = size_t(1.5 * mix.rate) + 17;
    for (size_t i = 0; i < piece.frames(); ++i) mix.left[at + i] += 0.25f * piece.left[i], mix.right[at + i] += 0.25f * piece.right[i];
    const audio::Fit f = audio::fit(piece, mix, 1.48, 0.06);
    CHECK(near(f.offset * mix.rate, double(at), 0.5));
    CHECK(near(f.gain_left, 0.25, 0.01) && near(f.gain_right, 0.25, 0.01));
    CHECK(f.correlation > 0.9);
}
