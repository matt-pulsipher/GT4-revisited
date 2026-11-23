#include "gt4r/audio/analysis.h"

#include <algorithm>
#include <complex>
#include <cstring>
#include <numbers>
#include <stdexcept>

namespace gt4r::audio {

namespace {

u16 le16(const u8* p) { return u16(p[0] | p[1] << 8); }
u32 le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }

// In-place radix-2 transform; n a power of two.
void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2 * std::numbers::pi / double(len);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> u(1, 0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
}

// The power spectrum (bins 0..n/2) of n samples from `first`, Hann windowed, the mean of the two
// sides' (so that a side played inverted against the other still counts: the surround cue),
// scaled so that a full-scale sine's bin holds about 0.5 (its mean square).
struct Analyzer {
    explicit Analyzer(size_t n) : n(n), window(n), buf(n), power(n / 2 + 1) {
        double sum = 0;
        for (size_t i = 0; i < n; ++i) sum += window[i] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * double(i) / double(n));
        scale = 2.0 / (sum * sum);
    }
    const std::vector<double>& operator()(const Signal& s, ptrdiff_t first) {
        // Both sides in one transform: the left as the real part, the right as the imaginary.
        for (size_t i = 0; i < n; ++i) {
            const ptrdiff_t at = first + ptrdiff_t(i);
            const bool in = at >= 0 && size_t(at) < s.frames();
            buf[i] = {in ? s.left[size_t(at)] * window[i] : 0.0, in ? s.right[size_t(at)] * window[i] : 0.0};
        }
        fft(buf);
        for (size_t k = 0; k <= n / 2; ++k) {
            const std::complex<double> a = buf[k], b = std::conj(buf[(n - k) % n]);
            const std::complex<double> l = 0.5 * (a + b), r = std::complex<double>(0, -0.5) * (a - b);
            power[k] = 0.5 * (std::norm(l) + std::norm(r)) * scale;
        }
        return power;
    }
    size_t n;
    std::vector<double> window;
    std::vector<std::complex<double>> buf;
    std::vector<double> power;
    double scale = 1;
};

size_t pow2_at_least(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// Sums a linear power spectrum into logarithmic bands (each band the power between its edges,
// split by fractions of bins at the edges).
void to_log_bands(const std::vector<double>& power, int rate, size_t n, double lo, int per_octave, std::vector<double>& bands) {
    const double bin_hz = double(rate) / double(n);
    for (size_t b = 0; b < bands.size(); ++b) {
        const double f0 = lo * std::exp2(double(b) / per_octave), f1 = lo * std::exp2(double(b + 1) / per_octave);
        const double x0 = f0 / bin_hz, x1 = f1 / bin_hz;
        double sum = 0;
        if (x1 - x0 < 1.0) {
            // Narrower than a bin: the band takes its share of the bin(s) at its middle, interpolated.
            const double mid = 0.5 * (x0 + x1);
            const size_t k = size_t(mid);
            const double t = mid - double(k);
            const double p = k + 1 < power.size() ? power[k] * (1 - t) + power[k + 1] * t : 0.0;
            sum = p * (x1 - x0);
        } else {
            for (size_t k = size_t(x0); k <= size_t(x1) && k < power.size(); ++k) {
                const double a = std::max(x0, double(k) - 0.5), z = std::min(x1, double(k) + 0.5);
                if (z > a) sum += power[k] * (z - a);
            }
        }
        bands[b] += sum;
    }
}

double correlation(const float* a, const float* b, size_t n) {
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) ma += a[i], mb += b[i];
    ma /= double(n), mb /= double(n);
    double c = 0, ea = 0, eb = 0;
    for (size_t i = 0; i < n; ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        c += x * y, ea += x * x, eb += y * y;
    }
    return ea > 0 && eb > 0 ? c / std::sqrt(ea * eb) : 0.0;
}

} // namespace

Signal read_wav(ByteSpan d) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error("not a WAV file");
    int format = 0, channels = 0, bits = 0;
    Signal s;
    for (size_t at = 12; at + 8 <= d.size();) {
        const u32 size = le32(d.data() + at + 4);
        const u8* body = d.data() + at + 8;
        const size_t avail = std::min<size_t>(size, d.size() - at - 8);
        if (std::memcmp(d.data() + at, "fmt ", 4) == 0 && avail >= 16) {
            format = le16(body), channels = le16(body + 2), s.rate = int(le32(body + 4)), bits = le16(body + 14);
            if (format == 0xFFFE && avail >= 26) format = le16(body + 24); // the extensible kind's sub-format
        } else if (std::memcmp(d.data() + at, "data", 4) == 0) {
            if (channels < 1 || (format != 1 && format != 3)) throw std::runtime_error("unsupported WAV format");
            const size_t width = size_t(bits / 8), frames = avail / (width * size_t(channels));
            s.left.resize(frames), s.right.resize(frames);
            for (size_t f = 0; f < frames; ++f)
                for (int c = 0; c < std::min(channels, 2); ++c) {
                    const u8* p = body + (f * size_t(channels) + size_t(c)) * width;
                    float x = 0;
                    if (format == 3 && bits == 32) std::memcpy(&x, p, 4);
                    else if (bits == 16) x = float(s16(le16(p))) / 32768.0f;
                    else if (bits == 24) x = float(s32(u32(p[0]) << 8 | u32(p[1]) << 16 | u32(p[2]) << 24) >> 8) / 8388608.0f;
                    else if (bits == 32) x = float(double(s32(le32(p))) / 2147483648.0);
                    else throw std::runtime_error("unsupported WAV sample size");
                    (c == 0 ? s.left : s.right)[f] = x;
                }
            if (channels == 1) s.right = s.left;
            return s;
        }
        at += 8 + size + (size & 1);
    }
    throw std::runtime_error("WAV file without data");
}

Signal signal_from(const std::vector<s16>& stereo, int rate) {
    Signal s;
    s.rate = rate;
    s.left.resize(stereo.size() / 2), s.right.resize(stereo.size() / 2);
    for (size_t f = 0; f < s.left.size(); ++f) s.left[f] = float(stereo[2 * f]) / 32768.0f, s.right[f] = float(stereo[2 * f + 1]) / 32768.0f;
    return s;
}

Signal slice(const Signal& s, double from, double to) {
    Signal o;
    o.rate = s.rate;
    const size_t a = std::min(s.frames(), size_t(std::max(0.0, from) * s.rate));
    const size_t z = to > 0 ? std::min(s.frames(), size_t(to * s.rate)) : s.frames();
    if (z > a) {
        o.left.assign(s.left.begin() + ptrdiff_t(a), s.left.begin() + ptrdiff_t(z));
        o.right.assign(s.right.begin() + ptrdiff_t(a), s.right.begin() + ptrdiff_t(z));
    }
    return o;
}

Signal resample(const Signal& s, int rate) {
    if (rate == s.rate || s.frames() == 0) return s;
    Signal o;
    o.rate = rate;
    const size_t frames = size_t(double(s.frames()) * rate / s.rate);
    o.left.resize(frames), o.right.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        const double x = double(f) * s.rate / rate;
        const size_t i = std::min(size_t(x), s.frames() - 1), j = std::min(i + 1, s.frames() - 1);
        const float t = float(x - double(i));
        o.left[f] = s.left[i] * (1 - t) + s.left[j] * t;
        o.right[f] = s.right[i] * (1 - t) + s.right[j] * t;
    }
    return o;
}

Loudness loudness(const Signal& s) {
    Loudness l;
    if (s.frames() == 0) return l;
    double el = 0, er = 0, lr = 0, peak = 0;
    for (size_t f = 0; f < s.frames(); ++f) {
        el += double(s.left[f]) * s.left[f], er += double(s.right[f]) * s.right[f], lr += double(s.left[f]) * s.right[f];
        peak = std::max({peak, double(std::fabs(s.left[f])), double(std::fabs(s.right[f]))});
    }
    const double n = double(s.frames());
    l.left_db = to_db(el / n), l.right_db = to_db(er / n), l.rms_db = to_db((el + er) / (2 * n)), l.peak_db = to_db(peak * peak);
    l.balance_db = l.right_db - l.left_db;
    l.side_correlation = el > 0 && er > 0 ? lr / std::sqrt(el * er) : 0.0;
    return l;
}

double LogSpectrum::frequency(double band) const { return lo * std::exp2((band + 0.5) / per_octave); }

double LogSpectrum::peak(double from_hz, double to_hz) const {
    double best = -1e9, at = 0;
    for (size_t b = 0; b < db.size(); ++b) {
        const double f = frequency(double(b));
        if (f < from_hz || f > to_hz) continue;
        if (db[b] > best) best = db[b], at = f;
    }
    return at;
}

double LogSpectrum::centroid() const {
    double sum = 0, weighted = 0;
    for (size_t b = 0; b < db.size(); ++b) {
        const double p = std::pow(10.0, db[b] / 10.0);
        sum += p, weighted += p * std::log2(frequency(double(b)));
    }
    return sum > 0 ? std::exp2(weighted / sum) : 0.0;
}

LogSpectrum log_spectrum(const Signal& s, int per_octave, double lo, double hi, int window) {
    LogSpectrum out;
    out.lo = lo, out.hi = std::min(hi, 0.5 * s.rate), out.per_octave = per_octave;
    const size_t bands = size_t(std::max(1.0, std::floor(std::log2(out.hi / lo) * per_octave)));
    std::vector<double> sum(bands, 0.0);
    const size_t n = pow2_at_least(size_t(std::max(256, window)));
    Analyzer analyze(n);
    size_t count = 0;
    for (size_t at = 0; at + n <= std::max(n, s.frames()); at += n / 2, ++count) to_log_bands(analyze(s, ptrdiff_t(at)), s.rate, n, lo, per_octave, sum);
    out.db.resize(bands);
    for (size_t b = 0; b < bands; ++b) out.db[b] = float(to_db(sum[b] / double(std::max<size_t>(1, count))));
    return out;
}

double spectrum_match(const LogSpectrum& a, const LogSpectrum& b) {
    const size_t n = std::min(a.db.size(), b.db.size());
    return n ? correlation(a.db.data(), b.db.data(), n) : 0.0;
}

double pitch_ratio(const LogSpectrum& a, const LogSpectrum& b, double max_semitones, double* score) {
    const int per = a.per_octave;
    const int reach = int(std::lround(max_semitones / 12.0 * per));
    const size_t n = std::min(a.db.size(), b.db.size());
    std::vector<double> c(size_t(2 * reach + 1), -2.0);
    // a(band + shift) against b(band): a is `shift` bands higher.
    for (int shift = -reach; shift <= reach; ++shift) {
        const size_t a0 = size_t(std::max(0, shift)), b0 = size_t(std::max(0, -shift));
        const size_t len = n - size_t(std::abs(shift));
        if (len < size_t(per)) continue;
        c[size_t(shift + reach)] = correlation(a.db.data() + a0, b.db.data() + b0, len);
    }
    const size_t best = size_t(std::max_element(c.begin(), c.end()) - c.begin());
    double shift = double(best) - reach;
    if (best > 0 && best + 1 < c.size()) {
        const double y0 = c[best - 1], y1 = c[best], y2 = c[best + 1], d = y0 - 2 * y1 + y2;
        if (d < 0) shift += 0.5 * (y0 - y2) / d;
    }
    if (score) *score = c[best];
    return std::exp2(shift / per);
}

std::vector<double> pitch_track(const Signal& s, double lo, double hi, double hop, int harmonics, int window) {
    std::vector<double> out;
    const size_t n = pow2_at_least(size_t(window));
    Analyzer analyze(n);
    const double bin_hz = double(s.rate) / double(n);
    const size_t step = std::max<size_t>(1, size_t(hop * s.rate));
    std::vector<double> logp(n / 2 + 1);
    for (size_t centre = 0; centre < s.frames(); centre += step) {
        const auto& p = analyze(s, ptrdiff_t(centre) - ptrdiff_t(n / 2));
        double total = 0;
        for (size_t k = 0; k < p.size(); ++k) total += p[k], logp[k] = std::log(p[k] + 1e-14);
        if (total < 1e-10) {
            out.push_back(0);
            continue;
        }
        // Candidates 1/96 octave apart; the value at a harmonic is the interpolated log power.
        auto at = [&](double f) {
            const double x = f / bin_hz;
            const size_t k = size_t(x);
            if (k + 1 >= logp.size()) return std::log(1e-14);
            // the larger of the two nearest bins: a peak between bins is not halved
            return std::max(logp[k], logp[k + 1]);
        };
        double best = -1e300, best_f = 0;
        for (double f = lo; f <= hi; f *= std::exp2(1.0 / 96)) {
            double sum = 0;
            for (int h = 1; h <= harmonics; ++h) sum += at(f * h);
            if (sum > best) best = sum, best_f = f;
        }
        out.push_back(best_f);
    }
    return out;
}

std::vector<double> band_track(const Signal& s, double lo, double hi, double hop) {
    std::vector<double> out;
    const size_t n = 2048;
    Analyzer analyze(n);
    const double bin_hz = double(s.rate) / double(n);
    const size_t step = std::max<size_t>(1, size_t(hop * s.rate));
    for (size_t centre = 0; centre < s.frames(); centre += step) {
        const auto& p = analyze(s, ptrdiff_t(centre) - ptrdiff_t(n / 2));
        double sum = 0;
        for (size_t k = size_t(lo / bin_hz); k <= size_t(hi / bin_hz) && k < p.size(); ++k) sum += p[k];
        out.push_back(to_db(sum));
    }
    return out;
}

std::vector<Onset> onsets(const Signal& s, double lo, double hi, double min_rise_db, double gap) {
    // Band power every 5 ms over 1024-sample windows.
    const double hop = 0.005;
    const Signal at48 = s.rate == 48000 ? s : resample(s, 48000);
    std::vector<double> track;
    {
        const size_t n = 1024;
        Analyzer analyze(n);
        const double bin_hz = 48000.0 / double(n);
        const size_t step = size_t(hop * 48000);
        for (size_t centre = 0; centre < at48.frames(); centre += step) {
            const auto& p = analyze(at48, ptrdiff_t(centre) - ptrdiff_t(n / 2));
            double sum = 0;
            for (size_t k = size_t(lo / bin_hz); k <= size_t(hi / bin_hz) && k < p.size(); ++k) sum += p[k];
            track.push_back(to_db(sum));
        }
    }
    // The mean power of the 30 ms from a frame against the 100 ms that end 10 ms before it.
    std::vector<double> power(track.size());
    for (size_t i = 0; i < track.size(); ++i) power[i] = std::pow(10.0, track[i] / 10.0);
    auto mean = [&](size_t from, size_t to) {
        double sum = 0;
        for (size_t j = from; j < to; ++j) sum += power[j];
        return sum / double(std::max<size_t>(1, to - from));
    };
    std::vector<Onset> out;
    const size_t back = 20, skip = 2, ahead = 6;
    for (size_t i = back + skip; i + ahead < track.size(); ++i) {
        const double before = mean(i - back - skip, i - skip), after = mean(i, i + ahead);
        const double rise = to_db(after) - to_db(before);
        if (rise < min_rise_db || to_db(after) < -90) continue;
        // The onset: the largest single step from two frames before to the end of the window.
        size_t at = i;
        double step = -1e9;
        for (size_t j = i - skip; j < i + ahead; ++j)
            if (track[j] - track[j - 1] > step) step = track[j] - track[j - 1], at = j;
        const double t = double(at) * hop;
        if (!out.empty() && t - out.back().time < gap) {
            out.back().rise_db = std::max(out.back().rise_db, rise);
            continue;
        }
        out.push_back({t, rise});
    }
    return out;
}

std::vector<Alignment> align(const Signal& needle, const Signal& haystack, int best) {
    // Features: 1/6-octave band powers (dB) from 100 Hz to 8 kHz every 50 ms, each frame's mean
    // taken off; the score is the correlation of the whole stretch.
    constexpr int kPer = 6;
    constexpr double kLo = 100, kHi = 8000, kHop = 0.05;
    auto features = [&](const Signal& s0) {
        const Signal s = s0.rate == 48000 ? s0 : resample(s0, 48000);
        const size_t n = 2048, step = size_t(kHop * 48000);
        const size_t bands = size_t(std::floor(std::log2(kHi / kLo) * kPer));
        Analyzer analyze(n);
        std::vector<float> out;
        std::vector<double> b(bands);
        for (size_t centre = 0; centre < s.frames(); centre += step) {
            std::fill(b.begin(), b.end(), 0.0);
            to_log_bands(analyze(s, ptrdiff_t(centre) - ptrdiff_t(n / 2)), 48000, n, kLo, kPer, b);
            double mean = 0;
            for (double& x : b) mean += (x = to_db(x));
            mean /= double(bands);
            for (double x : b) out.push_back(float(x - mean));
        }
        return std::make_pair(out, bands);
    };
    const auto [a, bands] = features(needle);
    const auto [h, bands_h] = features(haystack);
    std::vector<Alignment> scores;
    const size_t na = a.size() / bands, nh = h.size() / bands;
    if (na == 0 || nh < na) return {};
    for (size_t off = 0; off + na <= nh; ++off) scores.push_back({double(off) * kHop, correlation(a.data(), h.data() + off * bands, na * bands)});
    std::vector<Alignment> out;
    // The best few, each a second or more from the others.
    std::vector<Alignment> sorted = scores;
    std::sort(sorted.begin(), sorted.end(), [](const Alignment& x, const Alignment& y) { return x.score > y.score; });
    for (const Alignment& c : sorted) {
        if (int(out.size()) >= best) break;
        bool near = false;
        for (const Alignment& o : out) near |= std::fabs(o.offset - c.offset) < 1.0;
        if (!near) out.push_back(c);
    }
    return out;
}

namespace {

// Four one-pole low-pass stages at `hz`: the gains are fitted where two resamplings of the same
// sound still agree sample by sample.
Signal low_pass(const Signal& s, double hz) {
    Signal o = s;
    const float k = float(1.0 - std::exp(-2 * std::numbers::pi * hz / s.rate));
    for (auto* side : {&o.left, &o.right})
        for (int stage = 0; stage < 4; ++stage) {
            float y = 0;
            for (float& x : *side) x = y += k * (x - y);
        }
    return o;
}

} // namespace

// Between lo and hi Hz: the low-pass at hi less the low-pass at lo (lo 0: none).
static Signal band_pass(const Signal& s, double lo, double hi) {
    Signal o = low_pass(s, hi);
    if (lo <= 0) return o;
    const Signal under = low_pass(s, lo);
    for (size_t i = 0; i < o.frames(); ++i) o.left[i] -= under.left[i], o.right[i] -= under.right[i];
    return o;
}

Fit fit(const Signal& needle0, const Signal& haystack0, double approx, double search, double lo, double hi) {
    Fit out;
    const int rate = haystack0.rate;
    const Signal haystack = band_pass(haystack0, lo, hi);
    const Signal n = band_pass(needle0.rate == rate ? needle0 : resample(needle0, rate), lo, hi);
    if (n.frames() == 0) return out;
    // Mono sums for the search; at most 8 s of the needle are used to place it.
    const size_t len = std::min<size_t>(n.frames(), size_t(8 * rate));
    std::vector<float> a(len);
    for (size_t i = 0; i < len; ++i) a[i] = n.left[i] + n.right[i];
    auto hay = [&](ptrdiff_t i) { return i >= 0 && size_t(i) < haystack.frames() ? haystack.left[size_t(i)] + haystack.right[size_t(i)] : 0.0f; };
    const ptrdiff_t centre = ptrdiff_t(std::lround(approx * rate)), reach = ptrdiff_t(search * rate);
    auto score = [&](ptrdiff_t lag, size_t stride) {
        double c = 0, e0 = 0, e1 = 0;
        for (size_t i = 0; i < len; i += stride) {
            const double x = a[i], y = hay(lag + ptrdiff_t(i));
            c += x * y, e0 += x * x, e1 += y * y;
        }
        return e0 > 0 && e1 > 0 ? c / std::sqrt(e0 * e1) : 0.0;
    };
    // Coarse (every 8th sample, lags 4 apart), then every lag near the best.
    ptrdiff_t best = centre;
    double best_score = -2;
    for (ptrdiff_t lag = centre - reach; lag <= centre + reach; lag += 4) {
        const double c = score(lag, 8);
        if (c > best_score) best_score = c, best = lag;
    }
    const ptrdiff_t coarse = best;
    best_score = -2;
    for (ptrdiff_t lag = coarse - 6; lag <= coarse + 6; ++lag) {
        const double c = score(lag, 1);
        if (c > best_score) best_score = c, best = lag;
    }
    out.offset = double(best) / rate, out.correlation = best_score;
    // The gains over the whole needle.
    double xl = 0, xr = 0, ll = 0, rr = 0, hl = 0, hr = 0;
    for (size_t i = 0; i < n.frames(); ++i) {
        const ptrdiff_t j = best + ptrdiff_t(i);
        if (j < 0 || size_t(j) >= haystack.frames()) continue;
        xl += double(n.left[i]) * haystack.left[size_t(j)], ll += double(n.left[i]) * n.left[i], hl += double(haystack.left[size_t(j)]) * haystack.left[size_t(j)];
        xr += double(n.right[i]) * haystack.right[size_t(j)], rr += double(n.right[i]) * n.right[i], hr += double(haystack.right[size_t(j)]) * haystack.right[size_t(j)];
    }
    out.gain_left = ll > 0 ? xl / ll : 0, out.gain_right = rr > 0 ? xr / rr : 0;
    // The power left over once the scaled needle is taken away, against the haystack's.
    const double left_over = (hl - out.gain_left * xl) + (hr - out.gain_right * xr);
    out.explained = hl + hr > 0 ? std::clamp(1.0 - left_over / (hl + hr), 0.0, 1.0) : 0.0;
    return out;
}

} // namespace gt4r::audio
