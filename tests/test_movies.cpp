// The movies' sound: one track of a movie that holds two (gt4r/formats/movie_tracks.h), on a
// made-up stream and on the opening of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/movie_tracks.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstring>

using namespace gt4r;

namespace {

// A private stream 1 packet: MPEG-2 PES header with a PTS, the sound header FF A1 00 <track>, `payload`.
void put_sound_packet(Bytes& b, u8 track, size_t payload, u8 fill) {
    const size_t len = 3 + 5 + 4 + payload;
    const u8 head[] = {0, 0, 1, 0xBD, u8(len >> 8), u8(len), 0x81, 0x80, 5, 0x21, 0, 1, 0, 1, 0xFF, 0xA1, 0, track};
    b.insert(b.end(), head, head + sizeof head);
    b.insert(b.end(), payload, fill);
}

// Sample-to-sample change over level, mean of the sides: music ~0.05-0.3, noise ~1.4.
double roughness(const std::vector<s16>& pcm, u32 channels) {
    double sum = 0;
    for (u32 c = 0; c < channels; ++c) {
        double e = 0, d = 0;
        for (size_t i = c; i < pcm.size(); i += channels) {
            e += double(pcm[i]) * pcm[i];
            if (i >= channels) d += std::pow(double(pcm[i]) - pcm[i - channels], 2);
        }
        sum += e > 0 ? std::sqrt(d / e) : 0;
    }
    return sum / channels;
}

std::vector<s16> first_seconds(gt3r::video::Movie& m, double seconds) {
    const u32 rate = m.info().audio_rate, channels = m.info().audio_channels;
    std::vector<s16> pcm(size_t(seconds * rate) * channels);
    const size_t got = m.read_audio(pcm.data(), pcm.size() / channels);
    pcm.resize(got * channels);
    return pcm;
}

} // namespace

TEST(movie_sound_tracks_filtered) {
    // Track 1, track 2, track 1, a packet split over two reads.
    Bytes file;
    put_sound_packet(file, 1, 40, 0x11);
    put_sound_packet(file, 2, 40, 0x22);
    put_sound_packet(file, 1, 40, 0x33);
    put_sound_packet(file, 2, 40, 0x44);
    const size_t second = 18 + 40, fourth = 3 * second; // where the packets start
    auto read = [&file](u64 at, void* out, size_t len) { std::memcpy(out, file.data() + at, len); };
    auto filtered = first_sound_track_only(read, file.size());
    Bytes got(file.size());
    // The fourth packet's start code in one read, its header in the next.
    filtered(0, got.data(), fourth + 4);
    filtered(fourth + 4, got.data() + fourth + 4, file.size() - fourth - 4);
    CHECK(got[3] == 0xBD);              // track 1 (the first): kept
    CHECK(got[second + 3] == 0xBE);     // track 2: padding
    CHECK(got[2 * second + 3] == 0xBD); // track 1
    CHECK(got[fourth + 3] == 0xBE);     // track 2, its sound header past the read's end
    // The rest untouched.
    for (size_t i : {size_t(0), second, 2 * second, fourth}) got[i + 3] = file[i + 3];
    CHECK(got == file);
}

TEST(movie_opening_sound_one_track) {
    if (test::discs().empty()) SKIP("no disc");
    bool tried = false;
    for (const auto& disc : test::discs()) {
        gt3r::Vfs vfs(&disc->volume());
        const std::string path = "mpeg/main/opening_us.pss";
        if (!vfs.exists(path)) continue;
        tried = true;
        // The opening's two tracks, 1 and 2, are ADPCM streams of 48 kHz stereo, 200.6 s each.
        auto m = open_movie(vfs, path);
        CHECK(m->info().has_audio && m->info().audio_rate == 48000 && m->info().audio_channels == 2);
        CHECK(std::fabs(m->info().audio_seconds - 200.6) < 0.1);
        const std::vector<s16> pcm = first_seconds(*m, 10);
        CHECK(pcm.size() == 10u * 48000 * 2);
        const double rough = roughness(pcm, 2);
        // Both tracks together through the engine's own reader: noise.
        auto both = gt3r::video::open_movie(vfs, path);
        const double rough_both = roughness(first_seconds(*both, 10), 2);
        std::printf("  opening's sound, first 10 s: roughness %.3f (both tracks at once %.3f)\n", rough, rough_both);
        CHECK(rough < 0.1);
        CHECK(rough_both > 0.3);
    }
    if (!tried) SKIP("no GT4 disc");
}
