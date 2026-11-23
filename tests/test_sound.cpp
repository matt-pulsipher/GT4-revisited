// The sound: banks, sequences and indexes on made-up files, the sound driver's rules measured
// on rendered samples, then the discs of GT4R_DISC. Nothing here opens an audio device.

#include "test_framework.h"

#include "gt4r/audio/car_sound.h"
#include "gt4r/audio/menu_sound.h"
#include "gt4r/audio/render.h"
#include "gt4r/audio/sound_driver.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/formats/sound/inst_bank.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/formats/sound/sqt.h"
#include "gt4r/menu/layout.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstring>
#include <set>

using namespace gt4r;

namespace {

void put32(Bytes& b, size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(b.data() + at, &v, 4);
}
void put16(Bytes& b, size_t at, u16 v) {
    if (b.size() < at + 2) b.resize(at + 2);
    std::memcpy(b.data() + at, &v, 2);
}
void put_bytes(Bytes& b, size_t at, std::initializer_list<int> bytes) {
    if (b.size() < at + bytes.size()) b.resize(at + bytes.size());
    for (int v : bytes) b[at++] = u8(v);
}
size_t put_text(Bytes& b, const std::string& s) {
    const size_t at = b.size();
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    return at;
}
bool throws(const std::function<void()>& f) {
    try {
        f();
    } catch (const FormatError&) {
        return true;
    }
    return false;
}

// A looping square wave as SPU2 sample data: a silent frame, then two frames of one cycle
// each (28 samples: 1575 Hz at 44.1 kHz).
Bytes square_sample() {
    Bytes d(48, 0);
    for (int frame = 1; frame <= 2; ++frame) {
        u8* f = d.data() + frame * 16;
        f[0] = 0x00;                   // filter 0, shift 0: a sample is its nibble x 4096
        f[1] = frame == 1 ? 0x06 : 0x03; // loop start; end and repeat
        for (int i = 0; i < 7; ++i) f[2 + i] = 0x77;
        for (int i = 7; i < 14; ++i) f[2 + i] = 0x99;
    }
    return d;
}
constexpr double kSquareHz = 44100.0 / 28.0;

// A bank with two programs, an effect program and an effect sequence over the square wave.
//   program 0: first match; keys 0..59 at center 60, keys 60..127 at center 72
//   program 1: layered; two tones over every key, panned left and right
//   effect program 0: keys 0x20 (center 0x20) and 0x21 (center 0x2D, fine -8)
//   effect sequence 0.0: key 0x20 on; a pitch slide of -120 (an octave down) over a second; off after 0.5 s
Bytes make_bank(bool wide_addresses = false) {
    constexpr size_t kBlock = 0x20, kData = 0x200;
    Bytes b(kData, 0);
    std::memcpy(b.data(), "INST", 4);
    const Bytes sample = square_sample();
    put32(b, 0x08, kData);
    put32(b, 0x10, u32(sample.size()));
    put32(b, 0x14, kData);
    put32(b, kBlock, 0x1C0);
    put32(b, kBlock + 4, u32(sample.size()));
    std::memcpy(b.data() + kBlock + 0x0C, "SShd", 4);
    for (size_t i = 0; i < 28; ++i) put32(b, kBlock + 0x10 + i * 4, 0xFFFFFFFF);
    if (wide_addresses) put32(b, kBlock + 0x10 + 27 * 4, 0);
    const auto slot = [&](size_t n, size_t offset) { put32(b, kBlock + 0x10 + n * 4, u32(offset)); };
    const auto tone = [&](size_t at, int lo, int hi, int center, int fine, int flags, int volume, int pan) {
        put_bytes(b, at, {lo, hi, center, fine & 0xFF, flags, 0, 0, 0, 0xFF, 0x80, 0xC0, 0x5F, volume, pan, 12, 0x7F});
    };
    // Programs.
    size_t t = kBlock + 0x80;
    slot(0, 0x80);
    put16(b, t, 1);
    put16(b, t + 2, 6);
    put16(b, t + 4, 6 + 8 + 32);
    put_bytes(b, t + 6, {0x01, 127, 0x40, 0, 12, 0x7F, 0, 0}); // first match, two tones
    tone(t + 14, 0, 59, 60, 0, 0, 127, 0x40);
    tone(t + 30, 60, 127, 72, 0, 0x80, 127, 0x40);
    put_bytes(b, t + 46, {0x81, 127, 0x40, 0, 12, 0x7F, 0, 0}); // layered, two tones
    tone(t + 54, 0, 127, 60, 0, 0, 127, 0x00);
    tone(t + 70, 0, 127, 60, 0, 0, 127, 0x7F);
    // Effect programs.
    t = kBlock + 0x100;
    slot(5, 0x100);
    put16(b, t, 0);
    put16(b, t + 2, 4);
    put_bytes(b, t + 4, {0xFF, 127, 0x40, 0, 12, 0x7F, 0x20, 0x21});
    tone(t + 12, 0, 10, 0x20, 0, 0, 127, 0x40);
    tone(t + 28, 0, 10, 0x2D, -8, 0, 127, 0x40);
    // Effect sequences: one set of one entry.
    t = kBlock + 0x140;
    slot(3, 0x140);
    put16(b, t, 0);
    put16(b, t + 2, 4);
    put16(b, t + 4, 0);
    put16(b, t + 6, 8);
    put_bytes(b, t + 8, {0xA0, 0x20, 100, 0, 0x80, 0x00,       // key 0x20 on, time 0 (written in two bytes)
                         0xB0, 0x41, 15, 0x88, 0, 0x20, 0x81, 0x70, // slide -120 over 15 x 1/15 s, time 240
                         0xA0, 0x20, 0, 0, 0x00,               // key off
                         0xFF, 0x2F, 0x00});
    b.insert(b.end(), sample.begin(), sample.end());
    return b;
}

// A sequence bank of one sequence from its event bytes.
Bytes make_sqt(std::initializer_list<int> events, u32 resolution = 480) {
    Bytes b;
    put_bytes(b, 0, {'S', 's', 'q', 't'});
    put32(b, 4, 0);
    put32(b, 8, 12);
    put_bytes(b, 12, {'S', 's', 's', 'q'});
    put32(b, 16, resolution);
    put_bytes(b, 20, events);
    return b;
}

std::shared_ptr<const sound::InstBank> bank_of(const Bytes& b) { return std::make_shared<const sound::InstBank>(sound::InstBank::parse(b)); }
std::shared_ptr<const sound::SqtBank> sqt_of(const Bytes& b) { return std::make_shared<const sound::SqtBank>(sound::SqtBank::parse(b)); }

double hz(const audio::OfflineSound& s, double from, double seconds) {
    const size_t rate = gt3r::VoiceMixer::kRate;
    return audio::dominant_frequency(s.pcm, size_t(from * rate), size_t(seconds * rate), 300, 8000);
}
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance * b; }

int peak(const std::vector<s16>& pcm, double from, double to, int side = -1) {
    int p = 0;
    const size_t rate = gt3r::VoiceMixer::kRate;
    for (size_t i = size_t(from * rate) * 2; i < std::min(pcm.size(), size_t(to * rate) * 2); ++i)
        if (side < 0 || int(i & 1) == side) p = std::max(p, std::abs(int(pcm[i])));
    return p;
}

Bytes disc_file(Disc& disc, const std::string& path) {
    const gt3r::Volume::Entry* e = disc.volume().find(path);
    return e ? disc.volume().read(*e) : Bytes{};
}

} // namespace

TEST(sound_bank_parses) {
    const Bytes file = make_bank();
    CHECK(sound::InstBank::is_gt4(file));
    const sound::InstBank b = sound::InstBank::parse(file);
    CHECK(b.address_unit == 8);
    CHECK(b.programs.size() == 2 && b.program(0) && b.program(1) && !b.program(2));
    CHECK(b.program(0)->mode == sound::Program::Mode::FirstMatch && b.program(0)->tones.size() == 2);
    CHECK(b.program(1)->mode == sound::Program::Mode::Layered);
    CHECK(b.program(0)->tones_for_key(40).size() == 1 && b.program(0)->tones_for_key(40)[0]->center == 60);
    CHECK(b.program(0)->tones_for_key(90)[0]->center == 72 && b.program(0)->tones_for_key(90)[0]->reverb());
    CHECK(b.program(1)->tones_for_key(40).size() == 2);
    CHECK(b.effect_program(0) && b.effect_program(0)->mode == sound::Program::Mode::ByKey);
    CHECK(b.effect_program(0)->tones.size() == 2 && b.effect_program(0)->tones[1].fine == -8);
    CHECK(b.effect_program(0)->tones_for_key(0x22).empty() && b.effect_program(0)->tones_for_key(0x1F).empty());
    CHECK(b.effect_sequence(0, 0) && !b.effect_sequence(0, 1) && !b.effect_sequence(1, 0));
    CHECK(b.velocity_map[100] == 100);
    CHECK(b.tone_count() == 6);
    CHECK(sound::InstBank::parse(make_bank(true)).address_unit == 16);

    Bytes bad = file;
    bad.resize(0x100);
    CHECK(throws([&] { sound::InstBank::parse(bad); }));
    bad = file;
    put32(bad, 0x20, 0x90); // the table block ends before its tables
    CHECK(throws([&] { sound::InstBank::parse(bad); }));
    bad = file;
    bad[0x2C] = 'X';
    CHECK(!sound::InstBank::is_gt4(bad) && throws([&] { sound::InstBank::parse(bad); }));
    // A tone whose sample lies past the data is an empty slot.
    Bytes empty = file;
    put16(empty, 0x20 + 0x80 + 14 + 6, 0xFFFF);
    CHECK(sound::InstBank::parse(empty).program(0)->tones_for_key(40).empty());
}

TEST(sound_pitch_and_pan_tables) {
    CHECK(sound::pitch_for_offset(0) == 0x0EB3);       // 44.1 kHz in 48 kHz units
    CHECK(sound::pitch_for_offset(192) == 0x1D66);     // an octave up
    CHECK(sound::pitch_for_offset(-192) == 0x0759);
    CHECK(sound::pitch_for_offset(16) == 0x0F92);      // a semitone: 0xEB3333 x 2^(1/12) >> 12
    CHECK(sound::pitch_for_offset(192 * 4) == 0x3FFF); // limited
    for (u32 spu = 1; spu < 0x4000; spu += 7) CHECK(gt3r::VoiceMixer::hardware_pitch(sound::game_pitch_for_spu(u16(spu))) == spu);
    int l, r;
    sound::pan_gains(0, l, r);
    CHECK(l == 127 && r == 0);
    sound::pan_gains(64, l, r);
    CHECK(l == 120 && r == 120);
    sound::pan_gains(127, l, r);
    CHECK(l == 0 && r == 127);
    sound::pan_gains(32, l, r);
    CHECK(l == 127 && r == 62);
    sound::pan_gains(65, l, r);
    CHECK(l == 110 && r == 127);
    sound::pan_gains(80, l, r);
    CHECK(l == 94 && r == 127);
    for (int p = 1; p < 128; ++p) {
        int l2, r2;
        sound::pan_gains(p, l, r);
        sound::pan_gains(128 - p, l2, r2);
        CHECK(l == r2 && r == l2);
    }
}

TEST(sound_sequence_decodes) {
    // time 0; tempo 500000; program 3 on channel 1; note on, off by running status with velocity 0;
    // loop start; a note by note-off status; loop end; end.
    const Bytes file = make_sqt({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xC1, 0x03, 0x00, 0x91, 60, 100, 0x83, 0x60, 60, 0, 0x00,
                                 0xB1, 0x63, 20, 0x00, 0x91, 62, 90, 0x83, 0x60, 0x81, 62, 0x00, 0xB1, 0x63, 30, 0x00, 0xFF, 0x2F, 0x00});
    const sound::SqtBank b = sound::SqtBank::parse(file);
    CHECK(b.sequences.size() == 1 && b.sequences[0].resolution == 480);
    const std::vector<sound::SqtEvent> e = sound::decode_sequence(b, 0);
    CHECK(e.size() == 9);
    CHECK(e[0].kind == sound::SqtEvent::Kind::Tempo && e[0].tempo == 500000);
    CHECK(e[1].kind == sound::SqtEvent::Kind::Program && e[1].channel == 1 && e[1].a == 3);
    CHECK(e[2].kind == sound::SqtEvent::Kind::NoteOn && e[2].a == 60 && e[2].b == 100 && e[2].tick == 0);
    CHECK(e[3].kind == sound::SqtEvent::Kind::NoteOff && e[3].a == 60 && e[3].tick == 480);
    CHECK(e[4].kind == sound::SqtEvent::Kind::Control && e[4].a == sound::kCtlLoop && e[4].b == sound::kLoopStart);
    CHECK(e[6].kind == sound::SqtEvent::Kind::NoteOff && e[6].a == 62 && e[6].tick == 960);
    CHECK(e[8].kind == sound::SqtEvent::Kind::End);
    const sound::SqtSummary s = sound::summarize_sequence(b, 0);
    CHECK(s.notes == 2 && s.loop_start == 480 && s.loop_end == 960 && s.ticks == 960 && s.channels == 2);
    CHECK(std::fabs(s.loop_seconds - 0.5) < 1e-9 && std::fabs(s.intro_seconds - 0.5) < 1e-9);
    CHECK(s.programs == std::vector<u8>{3});

    Bytes bad = file;
    bad.resize(bad.size() - 3); // no end event
    CHECK(throws([&] { sound::decode_sequence(sound::SqtBank::parse(bad), 0); }));
    bad = file;
    bad[12] = 'X';
    CHECK(throws([&] { sound::SqtBank::parse(bad); }));
    size_t pos = 0;
    const Bytes number = {0x81, 0x80, 0x05};
    CHECK(sound::read_sequence_number(number, pos) == (1u << 14 | 5) && pos == 3);
}

TEST(sound_indexes_parse) {
    // The effect index.
    Bytes se;
    put_bytes(se, 0, {'G', 'T', 'S', 'E'});
    put32(se, 4, 1);
    put32(se, 0x0C, 2);
    se.resize(0x20);
    put32(se, 0x10, u32(put_text(se, "ok")));
    put32(se, 0x14, 0x00010600);
    put32(se, 0x18, u32(put_text(se, "clash")));
    put32(se, 0x1C, 0x0100023C);
    const sound::EffectIndex index = sound::EffectIndex::parse(se);
    CHECK(index.entries.size() == 2);
    const sound::EffectEntry* ok = index.find("ok");
    CHECK(ok && ok->bank == 0 && ok->sequence && ok->index == 6 && ok->key == 0);
    const sound::EffectEntry* clash = index.find("clash");
    CHECK(clash && clash->bank == 1 && !clash->sequence && clash->index == 2 && clash->key == 60);
    CHECK(!index.find("none"));
    CHECK(std::string(sound::effect_bank_file(0)) == "sound/gt4sys.ins" && !sound::effect_bank_file(5));
    se.resize(0x18);
    CHECK(throws([&] { sound::EffectIndex::parse(se); }));

    // A music index of each version: two groups, the second of two pieces.
    for (u32 version : {1u, 2u}) {
        const size_t record = version == 2 ? 24 : 20;
        Bytes m;
        put_bytes(m, 0, {'M', 'S', 'E', 'Q'});
        put32(m, 4, version);
        put32(m, 0x0C, 2);
        put32(m, 0x10, 3);
        m.resize(0x24 + 3 * record);
        put32(m, 0x14, 0x24);
        put32(m, 0x18, 1);
        put32(m, 0x1C, u32(0x24 + record));
        put32(m, 0x20, 2);
        for (u32 i = 0; i < 3; ++i) {
            const size_t r = 0x24 + i * record;
            put32(m, r, u32(put_text(m, i == 0 ? "home" : "map")));
            put32(m, r + 4, u32(put_text(m, "file" + std::to_string(i))));
            put32(m, r + 8, u32(put_text(m, "title")));
            put32(m, r + 12, u32(put_text(m, "artist")));
            if (version == 2) put32(m, r + 16, u32(put_text(m, "genre")));
            put32(m, r + record - 4, i == 2 ? 1 : 0);
        }
        const sound::MusicIndex mi = sound::MusicIndex::parse(m);
        CHECK(mi.version == version && mi.groups.size() == 2 && mi.records() == 3);
        CHECK(mi.groups[1].size() == 2 && mi.groups[1][1].name == "map" && mi.groups[1][1].value == 1 && mi.groups[1][1].file == "file2");
        CHECK((mi.groups[0][0].genre == "genre") == (version == 2));
        put32(m, 4, 3);
        CHECK(throws([&] { sound::MusicIndex::parse(m); }));
    }
    CHECK(sound::stream_group("racing") == 2 && sound::stream_group("last_goal") == 26 && sound::stream_group("x") == -1);
    CHECK(std::string(sound::stream_group_name(7)) == "get_car" && !sound::stream_group_name(27));

    // Markers: two on track 1.
    Bytes k;
    put_bytes(k, 0, {'B', 'M', 'R', 'K'});
    put32(k, 8, 44100);
    put32(k, 12, 88200);
    k.resize(0x110);
    put32(k, 0x10 + 4, 2);
    put32(k, 0x90 + 4, 0x110);
    put32(k, 0x110, 1000);
    put32(k, 0x114, 44100);
    const gt3r::sound::MadsMarkers markers = sound::parse_markers(k);
    CHECK(markers.rate == 44100 && markers.length == 88200 && markers.tracks[1].size() == 2 && markers.tracks[0].empty());
    CHECK(std::fabs(markers.seconds(1, 1) - 1.0) < 1e-9);
    k.resize(0x114);
    CHECK(throws([&] { sound::parse_markers(k); }));

    // The mix levels: three modes of two levels.
    Bytes v;
    put_bytes(v, 0, {'S', 'D', 'V', 'L'});
    put32(v, 4, 1);
    put32(v, 0x0C, 3);
    put32(v, 0x10, 2);
    for (u32 i = 0; i < 6; ++i) {
        const float f = 0.1f * float(i + 1);
        u32 bits;
        std::memcpy(&bits, &f, 4);
        put32(v, 0x14 + i * 4, bits);
    }
    const sound::VolumeTable levels = sound::VolumeTable::parse(v);
    CHECK(std::fabs(levels.level(1, 1) - 0.4f) < 1e-6f && std::fabs(levels.level(0, 0) - 0.1f) < 1e-6f);
    CHECK(levels.level(3, 0) == 1.0f && levels.level(0, 4) == 1.0f);
    CHECK(levels.level(2, 3) == 1.0f); // (inside the game's limits, past this table)
}

TEST(sound_driver_music_notes) {
    const auto bank = bank_of(make_bank());
    // Channel 0, program 0. Key 60 (the second tone: center 72, an octave below its recording)
    // for a second at 120 beats a minute, then key 72, then key 48 (the first tone: center 60).
    const auto seq = sqt_of(make_sqt({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xC0, 0x00, 0x00, 0x90, 60, 127, 0x87, 0x40, 60, 0, 0x00,
                                      72, 127, 0x87, 0x40, 72, 0, 0x00, 48, 127, 0x87, 0x40, 48, 0, 0x00, 0xFF, 0x2F, 0x00}));
    audio::OfflineSound s(false);
    s.driver().play_music(bank, seq, 0);
    CHECK(s.driver().music_playing() && s.driver().music_number() == 0);
    s.advance(1);
    CHECK(s.driver().voices() == 1);
    s.advance_seconds(3.2);
    CHECK(!s.driver().music_playing() && !s.driver().music_active() && s.driver().voices() == 0);
    CHECK(near(hz(s, 0.2, 0.6), kSquareHz / 2, 0.01));
    CHECK(near(hz(s, 1.2, 0.6), kSquareHz, 0.01));
    CHECK(near(hz(s, 2.2, 0.6), kSquareHz / 2, 0.01));
    // Full level, centered: the wave's 28672 x 120/127 by the pan table, halved by the mixer's trim.
    CHECK(near(peak(s.pcm, 1.2, 1.8), 28672 * 120 / 127 / 2, 0.06));
    CHECK(near(peak(s.pcm, 1.2, 1.8, 0), peak(s.pcm, 1.2, 1.8, 1), 0.01));
    // A note ends when its off event comes: silence before the next update's samples are over.
    CHECK(peak(s.pcm, 3.1, 3.2) == 0);
}

TEST(sound_driver_music_controls) {
    const auto bank = bank_of(make_bank());
    // Program 1 (two tones, left and right). Volume 64 from the start; after a second pan hard
    // left; after another, expression 0x40; then the bend up a full step (the range is 12).
    const auto seq = sqt_of(make_sqt({0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xC0, 0x01, 0x00, 0xB0, 0x07, 64, 0x00, 0x90, 60, 127, 0x87, 0x40,
                                      0xB0, 0x0A, 0, 0x87, 0x40, 0x0B, 64, 0x87, 0x40, 0xE0, 0x7F, 0x87, 0x40, 0xFF, 0x2F, 0x00}));
    audio::OfflineSound s(false);
    s.driver().play_music(bank, seq, 0);
    s.advance(2);
    CHECK(s.driver().voices() == 2);
    s.advance_seconds(4.5);
    // Both tones at key 60 = their center: the recording's own pitch.
    CHECK(near(hz(s, 0.3, 0.5), kSquareHz, 0.01));
    const int full = 28672 / 2;
    // Channel pan centered, the tones' pans 0 and 0x7F: each side one tone at the table's 127.
    CHECK(near(peak(s.pcm, 0.3, 0.9, 0), full * 64 / 127, 0.06));
    CHECK(near(peak(s.pcm, 0.3, 0.9, 1), full * 64 / 127, 0.06));
    // Channel pan 0: the left tone stays, the right tone's pan is 0x7F + 0 - 0x40 = 0x3F: both sides.
    CHECK(peak(s.pcm, 1.3, 1.9, 0) > peak(s.pcm, 0.3, 0.9, 0) * 3 / 2);
    CHECK(near(peak(s.pcm, 1.3, 1.9, 1), full * 64 / 127 * 110 / 127, 0.08));
    // Expression halves it.
    CHECK(near(peak(s.pcm, 2.3, 2.9, 1), peak(s.pcm, 1.3, 1.9, 1) / 2, 0.08));
    // Bend 0x7F with a range of 12: (63 x 12) / 4 = 189 sixteenths of a semitone up.
    CHECK(near(hz(s, 3.3, 0.5), kSquareHz * std::pow(2.0, 189.0 / 192.0), 0.01));
    // The surround cue: controller 0x5E value 1 inverts the left side.
    const auto phase = sqt_of(make_sqt({0x00, 0xC0, 0x00, 0x00, 0xB0, 0x5E, 1, 0x00, 0x90, 72, 127, 0x83, 0x60, 0xFF, 0x2F, 0x00}));
    audio::OfflineSound p(false);
    p.driver().play_music(bank, phase, 0);
    p.advance_seconds(0.5);
    long long dot = 0;
    for (size_t i = 4800; i + 1 < p.pcm.size(); i += 2) dot += (long long)p.pcm[i] * p.pcm[i + 1];
    CHECK(dot < 0);
}

TEST(sound_driver_music_clock_and_loop) {
    const auto bank = bank_of(make_bank());
    // 240 beats a minute (250000 us): a beat is 15 updates. Loop start at tick 480, a note of
    // a beat, loop end at tick 960: the loop is a beat long.
    const auto seq = sqt_of(make_sqt({0x00, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90, 0x00, 0xC0, 0x00, 0x83, 0x60, 0xB0, 0x63, 20, 0x00, 0x90, 60, 127, 0x83, 0x60,
                                      60, 0, 0x00, 0xB0, 0x63, 30, 0x00, 0xFF, 0x2F, 0x00}));
    audio::OfflineSound s(false);
    s.driver().play_music(bank, seq, 0);
    int first_voice = -1;
    std::vector<int> loops;
    for (int i = 0; i < 100; ++i) {
        s.advance(1);
        if (first_voice < 0 && s.driver().voices()) first_voice = i;
        if (int(loops.size()) < s.driver().music_loops()) loops.push_back(i);
    }
    // The first update runs at the starting tempo (120: 16 ticks), the rest at 32 ticks: tick 480 is
    // reached on the 16th update, tick 960 on the 31st.
    CHECK(first_voice == 15);
    CHECK(loops.size() >= 4 && loops[0] == 30);
    for (size_t i = 1; i < loops.size(); ++i) CHECK(loops[i] - loops[i - 1] == 15);
    CHECK(s.driver().music_playing());
    // A loop count: controller 0x66 value 2 plays the loop twice in all.
    const auto counted = sqt_of(make_sqt({0x00, 0xFF, 0x51, 0x03, 0x03, 0xD0, 0x90, 0x00, 0xB0, 0x66, 2, 0x00, 0xB0, 0x63, 20, 0x83, 0x60, 0x63, 30, 0x00, 0xFF, 0x2F, 0x00}));
    audio::OfflineSound c(false);
    c.driver().play_music(bank, counted, 0);
    c.advance(100);
    CHECK(c.driver().music_loops() == 1 && !c.driver().music_active());

    // Stopping, and fading out: a second's fade ends the sequence after 60 updates.
    s.driver().fade_music(audio::SoundDriver::Fade::DownAndStop, 0.0f, 1.0f / 60.0f);
    s.advance(58);
    CHECK(s.driver().music_active());
    s.advance(4);
    CHECK(!s.driver().music_active());
    // A sequence number past the bank's last plays the first.
    s.driver().play_music(bank, seq, 7);
    CHECK(s.driver().music_number() == 0);
    s.driver().stop_music(true);
    CHECK(!s.driver().music_active() && s.driver().music_number() == -1);
}

TEST(sound_driver_effect_sequences) {
    const auto bank = bank_of(make_bank());
    audio::OfflineSound s(false);
    CHECK(s.driver().play_effect(bank, 0, 1) == 0 && s.driver().play_effect(bank, 3, 0) == 0);
    audio::SoundParams p;
    p.left = p.right = 0.5f;
    const u32 id = s.driver().play_effect(bank, 0, 0, p);
    CHECK(id != 0 && s.driver().effect_playing(id));
    s.advance(1);
    CHECK(s.driver().voices() == 1);
    s.advance_seconds(1.0);
    CHECK(!s.driver().effect_playing(id) && s.driver().voices() == 0);
    // Key 0x20 at its center, velocity 100, the sides at half.
    CHECK(near(peak(s.pcm, 0.02, 0.06), 28672 * 100 / 127 * 120 / 127 / 4, 0.08));
    // The slide: -120 x 1.2 = -144 steps over a second, each 4/3 of a sixteenth of a semitone: an
    // octave down; at 0.4 s it is 0.4 of the way.
    const double slid = kSquareHz * std::pow(2.0, -0.4);
    CHECK(near(hz(s, 0.35, 0.1), slid, 0.04));
    CHECK(hz(s, 0.0, 0.08) > hz(s, 0.35, 0.1) * 1.15);
    // The note is let go at 240 ticks = 0.5 s.
    CHECK(peak(s.pcm, 0.4, 0.48) > 0 && peak(s.pcm, 0.56, 1.0) == 0);

    // Thirteen at once: the first started makes way.
    std::vector<u32> ids;
    for (int i = 0; i < 13; ++i) ids.push_back(s.driver().play_effect(bank, 0, 0));
    CHECK(!s.driver().effect_playing(ids[0]) && s.driver().effect_playing(ids[1]) && s.driver().effect_playing(ids[12]));
    s.driver().stop_all();
    CHECK(s.driver().voices() == 0 && !s.driver().effect_playing(ids[12]));
}

TEST(sound_driver_held_notes) {
    const auto bank = bank_of(make_bank());
    audio::OfflineSound s(false);
    CHECK(s.driver().note_on(bank, 5) == 0);
    audio::SoundParams p;
    p.left = 1.0f;
    p.right = 0.25f;
    // Program 0's first tone (center 60) at key 60.
    const u32 id = s.driver().note_on(bank, 0, p);
    CHECK(id != 0);
    s.advance_seconds(0.5);
    CHECK(s.driver().note_playing(id));
    CHECK(near(hz(s, 0.1, 0.3), kSquareHz, 0.01));
    CHECK(near(peak(s.pcm, 0.1, 0.4, 0), 4 * peak(s.pcm, 0.1, 0.4, 1), 0.05));
    p.pitch = 0.5f;
    p.left = p.right = 1.0f;
    p.pan = 0x7F; // 0x7F + 0x40 + 0x40 - 0x80: hard right
    s.driver().set_note(id, p);
    s.advance_seconds(0.5);
    CHECK(near(hz(s, 0.6, 0.3), kSquareHz / 2, 0.01) == false); // (the left side, which the helper reads, is silent)
    CHECK(peak(s.pcm, 0.7, 1.0, 0) == 0 && peak(s.pcm, 0.7, 1.0, 1) > 10000);
    s.driver().note_off(id);
    s.advance_seconds(0.2);
    CHECK(!s.driver().note_playing(id) && s.driver().voices() == 0);
    // The master volume of a kind scales its voices.
    s.driver().set_volume(audio::SoundDriver::Kind::Note, 0.5f);
    const size_t at = s.pcm.size();
    const u32 half = s.driver().note_on(bank, 0);
    s.advance_seconds(0.3);
    const std::vector<s16> tail(s.pcm.begin() + long(at), s.pcm.end());
    CHECK(half && near(peak(tail, 0.1, 0.3), 28672 * 120 / 127 / 4, 0.06));
    s.driver().set_mono(true);
    s.driver().stop_all();
}

// --- on the discs ---------------------------------------------------------------------------

TEST(sound_files_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        const bool gt4 = disc->info().game == Game::GT4;
        size_t banks = 0, sequences = 0, notes = 0, engines = 0, unordered = 0, markers = 0, programs = 0, missing_programs = 0;
        for (const ImageFile& f : disc->files()) {
            const bool ins = f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".ins") == 0;
            const bool sqt = f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".sqt") == 0;
            const bool adm = f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".adm") == 0;
            const bool engine = f.path.rfind("sound/", 0) == 0 && (f.path.find('/', 6) != std::string::npos || f.path.compare(f.path.size() - 3, 3, ".es") == 0);
            if (!ins && !sqt && !adm && !engine) continue;
            const Bytes d = disc_file(*disc, f.path);
            try {
                if (ins) {
                    CHECK(sound::InstBank::is_gt4(d));
                    const sound::InstBank b = sound::InstBank::parse(d);
                    CHECK(b.tone_count() > 0);
                    ++banks;
                    if (f.path.rfind("music/", 0) == 0) {
                        // Every program a sequence of the same name asks for is in the bank.
                        const Bytes s = disc_file(*disc, f.path.substr(0, f.path.size() - 4) + ".sqt");
                        CHECK(!s.empty());
                        const sound::SqtBank sq = sound::SqtBank::parse(s);
                        for (size_t i = 0; i < sq.sequences.size(); ++i)
                            for (u8 p : sound::summarize_sequence(sq, i).programs) {
                                ++programs;
                                if (!b.program(p)) {
                                    ++missing_programs;
                                    std::printf("  %s: sequence %zu asks for program %d, which the bank leaves out\n", f.path.c_str(), i, p);
                                }
                            }
                    }
                } else if (sqt) {
                    const sound::SqtBank b = sound::SqtBank::parse(d);
                    for (size_t i = 0; i < b.sequences.size(); ++i) {
                        const sound::SqtSummary s = sound::summarize_sequence(b, i);
                        CHECK(s.notes > 0 && s.loop_start >= 0 && s.loop_end > s.loop_start);
                        notes += s.notes;
                        ++sequences;
                    }
                } else if (adm) {
                    const auto m = sound::parse_markers(d);
                    if (m.rate != 44100) std::printf("  %s: rate %u, length %u\n", f.path.c_str(), m.rate, m.length);
                    CHECK(m.rate == 44100 || m.rate == 48000);
                    if (!m.length) std::printf("  %s: no length\n", f.path.c_str());
                    ++markers;
                } else {
                    const gt3r::sound::EngineBank b = audio::parse_engine_bank(d);
                    CHECK(!b.layers.empty());
                    for (const auto& l : b.layers) CHECK(l.size >= 32 && l.offset + l.size <= b.adpcm->size());
                    ++engines;
                    try {
                        gt3r::sound::EngineBank::parse(d);
                    } catch (const FormatError&) {
                        ++unordered;
                    }
                }
            } catch (const std::exception& e) {
                std::printf("  %s: %s\n", f.path.c_str(), e.what());
                CHECK(false);
            }
        }
        std::printf("  %s: %zu banks, %zu sequences (%zu notes), %zu engine sounds (%zu with layers out of data order), %zu marker files\n",
                    game_name(disc->info().game), banks, sequences, notes, engines, unordered, markers);
        CHECK(programs > 50 && missing_programs <= 2);
        CHECK(banks == (gt4 ? 36u : 5u));
        CHECK(sequences == (gt4 ? 27u : 9u));
        CHECK(engines == (gt4 ? 3576u : 1115u));
        CHECK(markers == (gt4 ? 100u : 0u));
    }
}

TEST(sound_indexes_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        const bool gt4 = disc->info().game == Game::GT4;
        gt3r::Vfs vfs(&disc->volume());
        audio::SoundLibrary lib(vfs);
        CHECK(lib.region() == (gt4 ? "us" : "jp"));
        // Every effect of the index is in its bank.
        const sound::EffectIndex& effects = lib.effects();
        CHECK(effects.entries.size() == (gt4 ? 73u : 58u));
        size_t found = 0;
        for (const sound::EffectEntry& e : effects.entries) {
            const auto bank = lib.effect_bank(e.bank);
            if (!bank) continue; // (a bank the disc leaves out)
            if (e.sequence ? bank->effect_sequence(e.index, e.key) != nullptr : bank->program(e.index) != nullptr)
                ++found;
            else
                std::printf("  %s: %s names a sound its bank leaves out\n", game_name(disc->info().game), e.name.c_str());
        }
        CHECK(found + 6 >= effects.entries.size());
        CHECK(effects.find("ok") && effects.find("cursor") && effects.find("cancel"));
        // The mix levels.
        if (gt4) {
            CHECK(std::fabs(lib.levels().level(1, sound::VolumeTable::Menu) - 0.5f) < 1e-6f);
            CHECK(std::fabs(lib.levels().level(1, sound::VolumeTable::Sequence) - 0.48f) < 1e-6f);
            CHECK(std::fabs(lib.levels().level(1, sound::VolumeTable::Stream) - 0.3f) < 1e-6f);
        }
        // The sequenced music's groups and their banks.
        const std::vector<std::string>& groups = lib.music_groups();
        CHECK(groups.size() == (gt4 ? 10u : 18u));
        if (gt4) {
            CHECK(lib.music_group("ARCADE") == 7 && lib.music_group("map") == 8 && lib.music_group("midi") == -1);
            CHECK(lib.music_pieces(8).size() == 5 && lib.music_pieces(8)[3].number == 3 && lib.music_pieces(8)[0].file == "music/us/map");
        } else {
            CHECK(lib.music_group("gtmenu01") >= 0 && lib.music_pieces(lib.music_group("gtmenu01"))[0].number == 7);
        }
        for (size_t g = 0; g < groups.size(); ++g) {
            const audio::MusicBank bank = lib.music_bank(int(g));
            CHECK(bool(bank));
            for (const audio::MusicPiece& p : lib.music_pieces(int(g))) CHECK(bank && size_t(p.number) < bank.sequences->sequences.size());
        }
        // The streamed music: every piece's file is on the disc.
        const sound::MusicIndex& streams = lib.streams();
        CHECK(streams.groups.size() == (gt4 ? 27u : 17u));
        size_t files = 0;
        for (size_t g = 0; g < streams.groups.size(); ++g)
            for (size_t n = 0; n < streams.groups[g].size(); ++n) {
                const std::string path = lib.stream_file(int(g), int(n));
                if (disc->volume().find(path))
                    ++files;
                else
                    std::printf("  %s: not on the disc\n", path.c_str());
            }
        std::printf("  %s: %zu effects, %zu music groups, %zu of %zu streams on the disc\n", game_name(disc->info().game), effects.entries.size(),
                    groups.size(), files, streams.records());
        CHECK(files + 8 >= streams.records());
        if (gt4) CHECK(lib.stream_file(sound::stream_group("racing"), 0) == "bgm/us/photo_01.ads");
    }
}

TEST(sound_menu_effects_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    // The names the menus' scripts play.
    const char* const names[] = {"ok", "cancel", "disable", "cursor", "next", "return", "select", "buy"};
    for (const auto& disc : test::discs()) {
        gt3r::Vfs vfs(&disc->volume());
        for (const char* name : names) {
            // (Prologue's "buy" is a tone of volume 0.)
            if (disc->info().game == Game::Prologue && std::string(name) == "buy") continue;
            audio::OfflineSound s;
            audio::MenuSound menu(vfs, s.driver());
            CHECK(menu.play(name));
            s.advance_seconds(3.0);
            const audio::Level l = audio::measure(s.pcm);
            // Heard, not clipped, and over within the three seconds.
            if (!(l.peak > 800 && l.clipped == 0 && l.sounding_until < 2.9 && l.sounding_until > 0.03))
                std::printf("  %s %s: peak %d, until %.2f s\n", game_name(disc->info().game), name, l.peak, l.sounding_until);
            CHECK(l.peak > 800 && l.clipped == 0);
            CHECK(l.sounding_until < 2.9 && l.sounding_until > 0.03);
            CHECK(s.driver().voices() == 0);
        }
        audio::OfflineSound s;
        audio::MenuSound menu(vfs, s.driver());
        CHECK(!menu.play("no such sound"));
        // The level follows the effects option.
        menu.play("ok");
        s.advance_seconds(1.5);
        const int full = audio::measure(s.pcm).peak;
        s.pcm.clear();
        menu.set_effect_option(0.5f);
        menu.play("ok");
        s.advance_seconds(1.5);
        CHECK(near(audio::measure(s.pcm).peak, full / 2, 0.1));
    }
}

TEST(sound_menu_music_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        const bool gt4 = disc->info().game == Game::GT4;
        gt3r::Vfs vfs(&disc->volume());
        audio::OfflineSound s;
        std::string streamed;
        bool stream_on = false;
        audio::StreamPlayer player;
        player.play = [&](const std::string& path, bool, float) {
            streamed = path;
            stream_on = true;
            return true;
        };
        player.stop = [&] { stream_on = false; };
        player.playing = [&] { return stream_on; };
        audio::MenuSound menu(vfs, s.driver(), {}, player);
        const std::string group = gt4 ? "MAP" : "gtmenu01";
        const std::string other = gt4 ? "ARCADE" : "option01";
        CHECK(!menu.start_music("no such group") && menu.music_group().empty());
        menu.load(group);
        CHECK(menu.keep_music(group));
        CHECK(menu.music_group() == (gt4 ? "map" : "gtmenu01"));
        const int first = menu.music_number();
        s.advance_seconds(20.0);
        const audio::Level level = audio::measure(s.pcm);
        CHECK(level.peak > 2000 && level.clipped == 0);
        CHECK(s.max_voices <= gt3r::VoiceMixer::kVoices);
        // Keeping the group that plays changes nothing; starting it again moves on to its next piece.
        CHECK(menu.keep_music(group) && menu.music_number() == first);
        CHECK(menu.keep_music("keep") && menu.music_number() == first);
        CHECK(menu.start_music(group));
        if (gt4) CHECK(menu.music_number() == first + 1);
        // Another group takes over; a fade out ends it.
        CHECK(menu.keep_music(other) && menu.music_group() != (gt4 ? "map" : "gtmenu01"));
        s.advance_seconds(2.0);
        menu.fade_out_music(0.5f);
        s.advance_seconds(0.4);
        CHECK(!menu.music_group().empty());
        s.advance_seconds(0.2);
        CHECK(menu.music_group().empty());
        s.advance_seconds(1.0);
        CHECK(s.driver().voices() == 0);
        // The group comes back at the piece after the one that was fading (a group of one: the same).
        CHECK(menu.keep_music(other));
        menu.unload();
        CHECK(menu.music_group().empty());

        // Streams by the game's names.
        if (gt4) {
            CHECK(menu.stream_file("pcm", "get_car") == "bgm/us/str13j.ads");
            CHECK(menu.stream_file("pcm", "racing", 3) == "bgm/us/photo_04.ads");
            CHECK(menu.stream_file("pcm", "racing", 0x1000 + 3) == "bgm/us/i_photo_04.ads");
            CHECK(menu.stream_file("narration", "l0p0001e") == "narration/us/l0p0001e.ads");
            CHECK(menu.stream_file("pcm", "autodemo").empty() && menu.stream_file("pcm", "nothing").empty() && menu.stream_file("tape", "x").empty());
        }
        const std::string drawn = menu.stream_file("pcm", "racing");
        CHECK(!drawn.empty() && disc->volume().find(drawn));
        CHECK(menu.start_stream("pcm", "racing") && streamed == drawn && menu.is_playing_stream());
        menu.stop_stream();
        CHECK(!menu.is_playing_stream());
        CHECK(!menu.start_stream("pcm", "nothing"));
    }
}

TEST(sound_music_renders_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        const bool gt4 = disc->info().game == Game::GT4;
        gt3r::Vfs vfs(&disc->volume());
        audio::SoundLibrary lib(vfs);
        const audio::MusicBank bank = lib.music_bank(gt4 ? "music/us/home" : "music/jp/bgmmid");
        CHECK(bool(bank));
        if (!bank) continue;
        const int number = gt4 ? 0 : 5; // (Prologue's shortest: the options')
        const sound::SqtSummary sum = sound::summarize_sequence(*bank.sequences, size_t(number));
        const audio::MusicRender r = audio::render_music(bank.bank, bank.sequences, number, 2);
        CHECK(r.loop_frames.size() == 2);
        if (r.loop_frames.size() < 2) continue;
        // The loop comes round where the tempo says, to within an update.
        const double rate = gt3r::VoiceMixer::kRate, update = 1.0 / audio::SoundDriver::kUpdateRate;
        const double first = double(r.loop_frames[0]) / rate, period = double(r.loop_frames[1] - r.loop_frames[0]) / rate;
        std::printf("  %s: loop ends at %.3f s (the events say %.3f), period %.3f s (%.3f), most voices %d\n", game_name(disc->info().game), first,
                    sum.intro_seconds + sum.loop_seconds, period, sum.loop_seconds, r.max_voices);
        CHECK(std::fabs(first - (sum.intro_seconds + sum.loop_seconds)) <= 2 * update);
        CHECK(std::fabs(period - sum.loop_seconds) <= 2 * update);
        CHECK(r.max_voices <= gt3r::VoiceMixer::kVoices && r.max_voices >= 4);
        const audio::Level level = audio::measure(r.pcm);
        CHECK(level.rms > 0.01 && level.clipped < 50);
        // The two passes of the loop sound alike: their levels second by second.
        const size_t a = r.loop_frames[0] - size_t(sum.loop_seconds * rate), b = r.loop_frames[0];
        int compared = 0, alike = 0;
        for (size_t t = 0; t + size_t(rate) <= size_t(sum.loop_seconds * rate) && b + t + size_t(rate) <= r.end_frame; t += size_t(rate)) {
            double ea = 0, eb = 0;
            for (size_t i = 0; i < size_t(rate) * 2; ++i) {
                ea += double(r.pcm[(a + t) * 2 + i]) * r.pcm[(a + t) * 2 + i];
                eb += double(r.pcm[(b + t) * 2 + i]) * r.pcm[(b + t) * 2 + i];
            }
            ++compared;
            alike += ea > 0 && eb > 0 && std::fabs(std::log(ea / eb)) < 0.35;
        }
        CHECK(compared > 20 && alike >= compared * 9 / 10);
        // In tune: the notes sit on the A440 grid.
        double contrast = 0;
        const int cents = audio::tuning_offset_cents(r.pcm, size_t(rate * 5), size_t(rate * 60), &contrast);
        std::printf("  %s: tuning %+d cents, contrast %.2f\n", game_name(disc->info().game), cents, contrast);
        CHECK(cents == 0 && contrast > 2.0);
    }
}

TEST(sound_cars_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue; // (Prologue's database is in the earlier format)
        gt3r::Vfs vfs(&disc->volume());
        db::SpecDb db([&](const std::string& path) { return vfs.read(path); }, [&](const std::string& dir) { return vfs.list_dir(dir); });
        size_t cars = 0, with_files = 0, turbo = 0;
        std::set<int> ids;
        for (const db::CarEntry& car : db.cars()) {
            const audio::CarSoundFiles f = audio::car_sound_files(db, car.label);
            ++cars;
            if (f.engine.empty()) continue;
            ids.insert(f.id);
            turbo += f.turbo;
            if (disc->volume().find(f.engine) && disc->volume().find(f.exhaust))
                ++with_files;
            else
                std::printf("  %s: %s or %s is not on the disc\n", car.label.c_str(), f.engine.c_str(), f.exhaust.c_str());
        }
        std::printf("  %zu cars, %zu with both sound files, %zu engine sounds in use, %zu turbo\n", cars, with_files, ids.size(), turbo);
        CHECK(cars > 700 && with_files + 5 >= cars);
        const audio::CarSoundFiles lm = audio::car_sound_files(db, "gtr_concept_lm_01");
        CHECK(lm.id == 35907 && lm.turbo && lm.muffler == 3 && lm.exhaust == "sound/turbo3/35907");
        CHECK(turbo > 100 && turbo < cars);

        // A car's engine swept: the note's pitch follows the revs.
        audio::OfflineSound s(false);
        audio::CarSound sound(vfs, s.mixer());
        CHECK(sound.load("_117_coupe_68") && sound.files().id == 55201 && sound.files().variant == "normal0");
        const float revs[] = {2000, 3000, 4000, 5000};
        double hz[4] = {};
        for (int i = 0; i < 4; ++i) {
            const size_t at = s.pcm.size() / 2;
            s.advance(90, [&](int) {
                audio::CarSoundInput in;
                in.rpm = revs[i];
                in.throttle = 1.0f;
                sound.update(in);
            });
            // The firing frequency of a four-stroke four: two a turn.
            hz[i] = audio::dominant_frequency(s.pcm, at + 24000, 40000, revs[i] / 60.0 * 0.45, revs[i] / 60.0 * 4.5);
            std::printf("  %.0f rpm: %.1f Hz (%.2f a turn)\n", double(revs[i]), hz[i], hz[i] / (revs[i] / 60.0));
            CHECK(audio::measure(std::vector<s16>(s.pcm.begin() + long(at * 2), s.pcm.end())).peak > 1000);
        }
        for (int i = 0; i < 4; ++i) CHECK(hz[i] > 0);
        // The same order of the engine at every speed (within 4%), whichever layers play.
        for (int i = 1; i < 4; ++i) {
            const double order0 = hz[0] / revs[0], order = hz[i] / revs[i];
            const double ratio = order / order0, whole = std::round(ratio * 2) / 2;
            CHECK(whole > 0 && std::fabs(ratio - whole) < 0.04 * whole);
        }
        sound.stop();
        s.advance(30);
        CHECK(s.mixer().active_voices() == 0);
    }
}

// The music a menu page names is a group of the sequenced music.
TEST(sound_page_music_on_disc) {
    if (test::discs().empty()) SKIP("no disc");
    for (const auto& disc : test::discs()) {
        if (disc->info().game != Game::GT4) continue; // (Prologue's pages name none)
        gt3r::Vfs vfs(&disc->volume());
        audio::SoundLibrary lib(vfs);
        const Bytes file = disc_file(*disc, "projects/gtmode/gtmode.mproject");
        CHECK(!file.empty());
        const menu::Widget project = menu::parse_layout(ByteSpan(file));
        size_t named = 0, kept = 0;
        std::string home, map;
        const std::function<void(const menu::Widget&)> walk = [&](const menu::Widget& w) {
            if (w.type == "RootWindow") {
                const std::string bgm = w.text("bgm");
                if (bgm == "keep") {
                    ++kept;
                } else if (!bgm.empty()) {
                    ++named;
                    CHECK(lib.music_group(bgm) >= 0);
                }
                if (w.name() == "HomeRoot") home = bgm;
                if (w.name() == "TopRoot") map = bgm;
            }
            for (const menu::Widget& c : w.children()) walk(c);
        };
        walk(project);
        CHECK(named >= 20 && kept >= 5);
        CHECK(home == "HOME" && map == "MAP");
        // The page's music through the scripts' sound object.
        audio::OfflineSound s;
        audio::MenuSound menu(vfs, s.driver());
        CHECK(menu.keep_music(map) && menu.music_group() == "map");
        CHECK(menu.keep_music("keep") && menu.music_group() == "map");
        CHECK(menu.keep_music(home) && menu.music_group() == "home");
    }
}
