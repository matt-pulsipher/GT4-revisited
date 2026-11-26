// gt4tool sound compare: measurements of a rendering of ours against a recording of the original
// game (no disc needed).
//
//   gt4tool sound compare <ours.wav> <reference.wav|.mp4> [options]
//     --ours A:B        the stretch of ours, seconds (default: all)
//     --ref A:B         the stretch of the reference (default: all, or where ours is found)
//     --find            find where ours sits in the reference (by spectra over time) first
//     --at T[:S]        place ours sample by sample near T seconds of the reference (within S, 0.5)
//     --fit             with --find: place ours sample by sample and give the gain that scales it to
//                       the reference (ours: a piece as stored; the reference: the game's mix of it)
//     --band LO:HI      the spectra (match, pitch ratio, octaves) and the fit only between LO and HI Hz
//     --pitch LO:HI     track a note's fundamental between LO and HI Hz in both
//     --onsets LO:HI    list sudden starts of the power between LO and HI Hz (default 200:8000)
//     --rise DB         how many dB an onset rises over the 100 ms before (default 9)
//     --track LO:HI[:S] the power between LO and HI Hz every S seconds (0.05), both side by side
//     --peaks N         the N strongest peaks of each spectrum (within --band)
//     --seconds         a level for every second of both, side by side
//
// It prints both stretches' levels (dB of full scale, each side, the peak), their spectra's
// centroid and how well the spectra's shapes match as they are, the pitch ratio that lays ours
// over the reference best, and the onsets. A reference that is not a WAV file is decoded by
// `ffmpeg` (on the PATH) to 48 kHz stereo first.

#include "base/file.h"
#include "gt4r/audio/analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

bool parse_range(const char* s, double& a, double& b) { return std::sscanf(s, "%lf:%lf", &a, &b) == 2; }

audio::Signal load(const std::string& path) {
    if (path.size() > 4 && path.compare(path.size() - 4, 4, ".wav") == 0) {
        const auto data = gt3r::read_host_file(path);
        if (!data) throw std::runtime_error("cannot read " + path);
        return audio::read_wav(ByteSpan(*data));
    }
    // Anything else through ffmpeg; the path is quoted for the shell (cmd.exe on Windows, where
    // a path cannot hold a double quote, and the pipe is read in binary).
#if defined(_WIN32)
    const std::string quoted = "\"" + path + "\"";
#else
    std::string quoted = "'";
    for (char c : path) quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
#endif
    const std::string cmd = "ffmpeg -nostdin -loglevel error -i " + quoted + " -vn -ac 2 -ar 48000 -c:a pcm_s16le -f wav -";
#if defined(_WIN32)
    FILE* p = _popen(cmd.c_str(), "rb");
#else
    FILE* p = popen(cmd.c_str(), "r");
#endif
    if (!p) throw std::runtime_error("cannot run ffmpeg");
    Bytes data;
    u8 buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, p)) > 0;) data.insert(data.end(), buf, buf + n);
#if defined(_WIN32)
    _pclose(p);
#else
    pclose(p);
#endif
    // ffmpeg writing to a pipe leaves the sizes unset: take the data to the end.
    if (data.size() > 44) {
        for (size_t at = 12; at + 8 <= data.size();) {
            if (std::memcmp(data.data() + at, "data", 4) == 0) {
                const u32 size = u32(data.size() - at - 8);
                std::memcpy(data.data() + at + 4, &size, 4);
                break;
            }
            u32 size;
            std::memcpy(&size, data.data() + at + 4, 4);
            at += 8 + size;
        }
    }
    return audio::read_wav(ByteSpan(data));
}

void print_level(const char* name, const audio::Signal& s) {
    const audio::Loudness l = audio::loudness(s);
    std::printf("%-9s %7.2f s  rms %6.1f dB  left %6.1f  right %6.1f  (right - left %+5.1f)  peak %6.1f dB  sides correlate %+.2f\n", name, s.seconds(),
                l.rms_db, l.left_db, l.right_db, l.balance_db, l.peak_db, l.side_correlation);
}

double median(std::vector<double> v) {
    v.erase(std::remove(v.begin(), v.end(), 0.0), v.end());
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + ptrdiff_t(v.size() / 2), v.end());
    return v[v.size() / 2];
}

} // namespace

int sound_compare(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: gt4tool sound compare <ours.wav> <reference.wav|video> [--ours A:B] [--ref A:B] [--find] [--fit] [--at T[:S]] [--band LO:HI]\n"
                             "                             [--pitch LO:HI] [--onsets LO:HI] [--rise DB] [--peaks N] [--track LO:HI] [--seconds]\n");
        return 2;
    }
    double oa = 0, ob = 0, ra = 0, rb = 0, plo = 0, phi = 0, olo = 200, ohi = 8000, rise = 9, tlo = 0, thi = 0, thop = 0.05, blo = 40, bhi = 12000;
    double at = -1, at_search = 0.5;
    int peaks = 0;
    bool find = false, fit = false, seconds = false, pitch = false, track = false;
    for (int i = 5; i < argc; ++i) {
        const bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--ours") && more) parse_range(argv[++i], oa, ob);
        else if (!std::strcmp(argv[i], "--ref") && more) parse_range(argv[++i], ra, rb);
        else if (!std::strcmp(argv[i], "--pitch") && more) pitch = parse_range(argv[++i], plo, phi);
        else if (!std::strcmp(argv[i], "--onsets") && more) parse_range(argv[++i], olo, ohi);
        else if (!std::strcmp(argv[i], "--rise") && more) rise = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--track") && more) track = std::sscanf(argv[++i], "%lf:%lf:%lf", &tlo, &thi, &thop) >= 2;
        else if (!std::strcmp(argv[i], "--band") && more) parse_range(argv[++i], blo, bhi);
        else if (!std::strcmp(argv[i], "--peaks") && more) peaks = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--find")) find = true;
        else if (!std::strcmp(argv[i], "--fit")) fit = find = true;
        else if (!std::strcmp(argv[i], "--at") && more) std::sscanf(argv[++i], "%lf:%lf", &at, &at_search), fit = find = true;
        else if (!std::strcmp(argv[i], "--seconds")) seconds = true;
        else {
            std::fprintf(stderr, "error: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    try {
        audio::Signal ours = load(argv[3]), ref = load(argv[4]);
        if (ours.rate != 48000) ours = audio::resample(ours, 48000);
        if (ref.rate != 48000) ref = audio::resample(ref, 48000);
        if (oa > 0 || ob > 0) ours = audio::slice(ours, oa, ob);
        if (ra > 0 || rb > 0) ref = audio::slice(ref, ra, rb);
        double ref_start = ra;
        if (find) {
            auto found = at >= 0 ? std::vector<audio::Alignment>{{at - ra, 0}} : audio::align(ours, ref, 3);
            std::printf("ours found in the reference at:");
            for (const auto& f : found) std::printf("  %.2f s (match %.3f)", ra + f.offset, f.score);
            std::printf("\n");
            if (!found.empty() && fit) {
                const audio::Fit f = audio::fit(ours, ref, found[0].offset, at >= 0 ? at_search : 0.06, blo > 40 ? blo : 0, bhi < 12000 ? bhi : 1000);
                std::printf("placed at %.4f s (waveforms correlate %.3f): gain left %.4f (%+.2f dB), right %.4f (%+.2f dB); explains %.1f%% of the "
                            "reference's power there\n",
                            ra + f.offset, f.correlation, f.gain_left, 20 * std::log10(std::fabs(f.gain_left) + 1e-12), f.gain_right,
                            20 * std::log10(std::fabs(f.gain_right) + 1e-12), 100 * f.explained);
                found[0].offset = f.offset;
            }
            if (!found.empty()) {
                ref = audio::slice(ref, found[0].offset, found[0].offset + ours.seconds());
                ref_start = ra + found[0].offset;
            }
        }
        print_level("ours", ours);
        print_level("reference", ref);
        const audio::LogSpectrum so = audio::log_spectrum(ours, 48, blo, bhi), sr = audio::log_spectrum(ref, 48, blo, bhi);
        double score = 0;
        const double ratio = audio::pitch_ratio(so, sr, 12, &score);
        std::printf("spectra: centroid %.0f Hz / %.0f Hz; shapes match %.3f as they are; ours sits %+.0f cents from the reference (match %.3f)\n",
                    so.centroid(), sr.centroid(), audio::spectrum_match(so, sr), 1200 * std::log2(ratio), score);
        // The bands' levels side by side, an octave each.
        std::printf("octaves (Hz: ours / reference dB):");
        for (int o = 0; o < int(so.db.size()) / so.per_octave; ++o) {
            double a = 0, b = 0;
            for (int k = 0; k < so.per_octave; ++k) {
                const size_t i = size_t(o * so.per_octave + k);
                a += std::pow(10.0, so.db[i] / 10.0), b += i < sr.db.size() ? std::pow(10.0, sr.db[i] / 10.0) : 0.0;
            }
            std::printf("  %.0f: %.1f / %.1f", so.frequency(o * so.per_octave), audio::to_db(a), audio::to_db(b));
        }
        std::printf("\n");
        for (int side = 0; side < 2 && peaks > 0; ++side) {
            const audio::LogSpectrum& sp = side ? sr : so;
            std::vector<std::pair<float, double>> found;
            for (size_t b = 1; b + 1 < sp.db.size(); ++b)
                if (sp.db[b] > sp.db[b - 1] && sp.db[b] >= sp.db[b + 1]) found.push_back({sp.db[b], sp.frequency(double(b))});
            std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
            found.resize(std::min(found.size(), size_t(peaks)));
            std::sort(found.begin(), found.end(), [](const auto& x, const auto& y) { return x.second < y.second; });
            std::printf("peaks, %s:", side ? "reference" : "ours");
            for (const auto& [db, f] : found) std::printf(" %.0f Hz (%.0f)", f, double(db));
            std::printf("\n");
        }
        if (pitch) {
            const auto po = audio::pitch_track(ours, plo, phi, 0.1), pr = audio::pitch_track(ref, plo, phi, 0.1);
            const double mo = median(po), mr = median(pr);
            std::printf("fundamental %.0f..%.0f Hz: ours %.2f Hz, reference %.2f Hz (median), %+.0f cents\n", plo, phi, mo, mr,
                        mo > 0 && mr > 0 ? 1200 * std::log2(mo / mr) : 0.0);
            std::printf("  every 0.5 s (ours / reference):");
            for (size_t i = 0; i < std::max(po.size(), pr.size()); i += 5)
                std::printf(" %.1f/%.1f", i < po.size() ? po[i] : 0.0, i < pr.size() ? pr[i] : 0.0);
            std::printf("\n");
        }
        for (int side = 0; side < 2; ++side) {
            const auto on = audio::onsets(side ? ref : ours, olo, ohi, rise);
            std::printf("onsets %.0f..%.0f Hz, %s (%zu):", olo, ohi, side ? "reference" : "ours", on.size());
            for (size_t i = 0; i < std::min<size_t>(on.size(), 40); ++i)
                std::printf(" %.3f(+%.0f)", (side ? ref_start : oa) + on[i].time, on[i].rise_db);
            std::printf("\n");
        }
        if (track) {
            const auto a = audio::band_track(ours, tlo, thi, thop), b = audio::band_track(ref, tlo, thi, thop);
            std::printf("%.0f..%.0f Hz every %.0f ms (ours / reference, dB):\n", tlo, thi, thop * 1000);
            for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
                std::printf("  %7.3f / %7.3f s  %6.1f / %6.1f\n", oa + thop * double(i), ref_start + thop * double(i), i < a.size() ? a[i] : -120.0,
                            i < b.size() ? b[i] : -120.0);
        }
        if (seconds) {
            std::printf("each second (ours / reference, rms dB):\n");
            const double len = std::max(ours.seconds(), ref.seconds());
            for (double t = 0; t < len; t += 1.0) {
                const auto a = audio::loudness(audio::slice(ours, t, t + 1)), b = audio::loudness(audio::slice(ref, t, t + 1));
                std::printf("  %6.1f s  %6.1f / %6.1f\n", t, a.rms_db, b.rms_db);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}

int sound_players(int argc, char** argv); // cmd_sound_players.cpp

// `gt4tool sound compare ...` and `gt4tool sound players ...`: handled before the disc is opened.
// -1: not these commands.
int sound_main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "sound") != 0) return -1;
    if (std::strcmp(argv[2], "compare") == 0) return sound_compare(argc, argv);
    if (std::strcmp(argv[2], "players") == 0) return sound_players(argc, argv);
    return -1;
}

} // namespace gt4r::tool
