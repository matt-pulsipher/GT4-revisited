#pragma once

// Gran Turismo 4's sound driver on the shared engine's voice mixer: what turns the banks
// (formats/sound/inst_bank.h) and the sequences (formats/sound/sqt.h) into voices. Three
// kinds of sound, as in the game:
//
//   music    one "Sssq" sequence at a time: 16 channels of MIDI-like events playing a bank's
//            programs (the menus' music);
//   effects  up to 12 effect sequences at once: a bank's own few-note sequences (the menus'
//            sounds, the countdown);
//   notes    held notes of a bank's programs whose level, pan and pitch the caller sets every
//            frame (the race's effects).
//
// update() is one step of the game's sound thread (60 a second): the sequences advance, then
// every voice's level and pitch are worked out again where something changed. Offline, call
// update() and then mix VoiceMixer::kRate / 60 frames; in the app, AppSound runs it from the
// mixer's tick hook. Every method may be called from any thread (one mutex).
//
// The rules (docs/formats/sound.md "The sound driver") follow the game's own driver:
//   level  = program volume x tone volume, x the sequence's volume, x the kind's master
//            volume, x channel volume, expression and velocity (each /127), split by the pan
//            table and scaled by the two side gains (negative: that side inverted);
//   pan    = sequence pan + tone pan + channel pan - 0x80;
//   pitch  = 16 x (key - the tone's center) + its fine tune + bend, in 1/16 semitone, through
//            the driver's pitch table, x the sequence's pitch factor.

#include "audio/voice_mixer.h"
#include "gt4r/formats/sound/inst_bank.h"
#include "gt4r/formats/sound/sqt.h"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace gt4r::audio {

// What the caller sets on an effect sequence or a held note.
struct SoundParams {
    float left = 1.0f, right = 1.0f; // side gains, -1..1 (negative inverts the side); a held note's: up to 2
    float pitch = 1.0f;              // pitch factor
    int pan = 0x40;                  // added to the tone's pan (0x40: none)
    int priority = 0;                // a held note's voice priority: higher keeps its voice longer
    bool dry = false;                // a held note: no reverb send even where the tone asks for one
};

class SoundDriver {
public:
    static constexpr int kUpdateRate = 60;
    static constexpr int kEffectSequences = 12;
    static constexpr u32 kSamplesPerUpdate = gt3r::VoiceMixer::kRate / kUpdateRate;

    enum class Kind : u8 { Music = 1, Effect = 2, Note = 5 };
    enum class Fade : int { None = -1, Up = 0, Down = 1, DownAndStop = 2 };

    explicit SoundDriver(gt3r::VoiceMixer& mixer);
    ~SoundDriver();

    // One step of the sound thread.
    void update();

    // --- music ---
    // Starts sequence `number` of `sequences` (0 when the bank has no such number) with the
    // programs of `bank`, stopping the one playing.
    void play_music(std::shared_ptr<const sound::InstBank> bank, std::shared_ptr<const sound::SqtBank> sequences, int number);
    // Stops the sequence; its notes release (`at_once`: they are cut).
    void stop_music(bool at_once);
    // Moves the sequence's volume toward `target` (0..1) by `step` (of full volume) each
    // update; DownAndStop stops the sequence when it gets there.
    void fade_music(Fade mode, float target, float step);
    bool music_playing() const; // a sequence has events left
    bool music_active() const;  // ... or its last notes still sound
    int music_number() const;   // the sequence playing, -1 when none
    int music_loops() const;    // times the loop's end has been passed
    double music_ticks() const; // the sequence's clock

    // --- effect sequences ---
    // Plays the bank's effect sequence [set][entry]; 0 when the bank has none.
    u32 play_effect(std::shared_ptr<const sound::InstBank> bank, int set, int entry, const SoundParams& p = {});
    void set_effect(u32 id, const SoundParams& p);
    void stop_effect(u32 id);
    bool effect_playing(u32 id) const; // events left, or notes still sounding

    // --- held notes ---
    // Keys the program's first tone at key 60; 0 when the program is empty or no voice is free.
    u32 note_on(std::shared_ptr<const sound::InstBank> bank, int program, const SoundParams& p = {});
    void set_note(u32 id, const SoundParams& p);
    void note_off(u32 id);
    bool note_playing(u32 id) const;

    // A kind's master volume, 0..1 (1 at start).
    void set_volume(Kind kind, float v);
    float volume(Kind kind) const;
    // Mono output: both sides get the mean of their levels.
    void set_mono(bool mono);

    void stop_all();
    int voices() const; // sounding now

private:
    struct Slide {
        float target = 0, step = 0, value = 0;
        bool running = false;
    };
    struct Voice {
        gt3r::VoiceId id;
        Kind kind = Kind::Music;
        u32 owner = 0; // the effect sequence's or the note's id; 0 for music
        int channel = 0, key = 0;
        int base = 0;                  // program volume x tone volume
        int sequence_volume = 0x3FFF;  // Q14
        int channel_volume = 0x7F, expression = 0x7F, velocity = 0x7F;
        int gain_l = 0x3FFF, gain_r = 0x3FFF;
        int pan_sequence = 0x40, pan_tone = 0x40, pan_channel = 0x40;
        int pitch_factor = 0x1000; // Q12
        int center16 = 0, key16 = 0, fine = 0, bend_range = 0, bend = 0x40;
        Slide slide;
        bool dirty = true;
        u64 order = 0;
    };
    struct Channel {
        u8 program = 0, hold = 0, volume = 0x7F, expression = 0x7F, pan = 0x40, lfo_rate = 0, lfo_depth = 0, bend = 0x40;
        u8 reverb = 0;
        int gain_l = 0x3FFF, gain_r = 0x3FFF;
        const sound::Program* voice = nullptr;
    };
    struct Sequence {
        int state = 0; // 0 idle, 2 playing, 3 ended (its notes may still sound)
        std::shared_ptr<const sound::InstBank> bank;
        std::shared_ptr<const Bytes> data;
        size_t start = 0, pos = 0;
        u8 status = 0;
        float resolution = 480, time = 0, step = 0, next = 0;
        int loop_count = 0;
        int volume = 0x3FFF;
        SoundParams params;
    };
    struct Music : Sequence {
        std::array<Channel, 16> channels{};
        std::shared_ptr<const sound::SqtBank> sequences;
        int number = -1;
        // The loop's start: where to read on from and the clock there.
        size_t loop_pos = 0;
        u8 loop_status = 0;
        float loop_time = 0, loop_next = 0;
        bool loop_set = false;
        int loops = 0;
        Fade fade = Fade::None;
        int fade_target = 0, fade_step = 0, fade_value = 0;
    };
    struct Effect : Sequence {
        u32 id = 0;
        bool skip_time = false; // after a jump the time to the next event is already read
        u64 order = 0;
    };
    struct Note {
        u32 id = 0;
        std::shared_ptr<const sound::InstBank> bank;
    };

    // (all with mutex_ held)
    u8 byte(Sequence& s);
    u32 number(Sequence& s);
    void set_tempo(Sequence& s, float bpm);
    void tick_music();
    void music_event();
    void music_control(u8 controller, u8 value, int channel);
    void music_note_on(int channel, int key, int velocity);
    void music_note_off(int channel, int key);
    void stop_music_locked(bool at_once);
    void tick_effect(Effect& e);
    void effect_event(Effect& e);
    void effect_note_on(Effect& e, int program, int key, int velocity);
    Voice* key_on(const sound::InstBank& bank, std::shared_ptr<const Bytes> adpcm, const sound::Program& program, const sound::Tone& tone,
                  Voice v, bool reverb, int priority);
    void levels(const Voice& v, s16& l, s16& r) const;
    u16 pitch(Voice& v);
    void update_voices();
    void release(Kind kind, u32 owner, bool at_once);
    bool owns_voice(Kind kind, u32 owner) const;
    Effect* effect(u32 id);
    const Effect* effect(u32 id) const;
    static int q14(float v);
    static int note_gain(float v);

    gt3r::VoiceMixer& mixer_;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    Music music_;
    std::array<Effect, kEffectSequences> effects_{};
    std::vector<Note> notes_;
    std::array<int, 8> master_{};
    bool mono_ = false;
    u32 next_id_ = 1;
    u64 order_ = 0;
};

} // namespace gt4r::audio
