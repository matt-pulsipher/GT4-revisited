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

// The output's level (docs/formats/sound.md "The output's level"): the SPU2's. A voice at side
// gain 0x3FFF plays its samples at full scale, the voices sum and the sum is held to 16 bits;
// a stream plays its samples at its level. The shared engine's mixer and streams play at half
// that (gt3r::VoiceMixer::kOutputTrim); with_output_level() sets a mixer to the SPU2's level
// and stream_request() is the level to ask the shared engine for to hear a stream at `level`.
constexpr float kOutputLevel = 1.0f;
inline void with_output_level(gt3r::VoiceMixer& mixer) { mixer.set_master_volume(kOutputLevel); }
// The surround cue (docs/formats/sound.md "The surround cue"): the program inverts one side of a
// sound behind the camera in every output mode, so a mixer playing the game has it on.
inline void with_surround_cue(gt3r::VoiceMixer& mixer) { mixer.set_surround_cue(true); }
// A voice's level lands at once (docs/formats/sound.md "Voices"): the SPU2 takes its level registers at once and the driver
// eases only what its rules say. With a shared engine whose mixer has the switch (VoiceMixer::set_volume_ramp); an older one
// keeps its 5 ms ease. `on` false gives the engine its own ease back.
template <class Mixer> void with_levels_at_once(Mixer& mixer, bool on = true) {
    if constexpr (requires { mixer.set_volume_ramp(false); }) mixer.set_volume_ramp(!on);
}
// The stream's two voices (stream_voices.h): the SPU2's last two, which the driver never hands out, when the shared engine's
// mixer keeps them apart (VoiceMixer::key_on_spare); an older one gives the stream two of the driver's 46.
template <class Mixer, class Params> gt3r::VoiceId key_stream_voice(Mixer& mixer, int side, const Params& p) {
    if constexpr (requires { mixer.key_on_spare(side, p); }) return mixer.key_on_spare(side, p);
    else return mixer.key_on(p);
}
inline float stream_request(float level) { return level * kOutputLevel / gt3r::VoiceMixer::kOutputTrim; }

// A voice's priority (docs/formats/sound.md "Voices"): a signed 32-bit number, the higher keeping its voice
// longer. kNoPriority is the program's default for a sequence's or a note's (0x55A748); an effect sequence
// without one takes its tones' own priorities and voices by age.
constexpr s32 kNoPriority = -1;
// The race's priority for a sound of `level` (0x392640, 0x398B70): |level| x 2147483520 in the EE's floats,
// truncated, at most 0x7FFFFFFE (from a level of 1 up).
s32 level_priority(float level);

// What the caller sets on an effect sequence or a held note.
struct SoundParams {
    float left = 1.0f, right = 1.0f; // side gains, -1..1 (negative inverts the side); a held note's: up to 2
    float pitch = 1.0f;              // pitch factor
    int pan = 0x40;                  // added to the tone's pan (0x40: none)
    s32 priority = kNoPriority;      // the voices' priority (above)
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

    // --- the voices ---
    // A voice for a layer player (layer_player.h; the driver's kind 4, 0x558FD8): one of the driver's 46 by the
    // rule of a held note (a free one, else the lowest priority at or under `priority`); an invalid id when
    // none. The player sends its priority again every step.
    gt3r::VoiceId key_layer(const gt3r::VoiceParams& p, s32 priority);
    void set_layer_priority(gt3r::VoiceId id, s32 priority);

private:
    struct Slide {
        float target = 0, step = 0, value = 0;
        bool running = false;
    };
    struct Voice {
        gt3r::VoiceId id;
        int slot = -1; // the driver's record it sounds on
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

    // The driver's voice records (0x6518C8 + 0xA8, 0xE0 bytes each; docs "Voices"): 46 that the program hands
    // out, each the shared mixer's voice while it sounds.
    struct Slot {
        gt3r::VoiceId id;
        bool busy = false;
        s32 priority = kNoPriority; // +0x34
        int group = -1;             // +0x40: an effect tone's voice group (0 none), -1 for a held note's or a layer's
        bool kept = false;          // +0x1A bit 4: a prioritized effect's tone flagged 0x01 (an effect without a priority leaves it)
        s32 age = 0;                // +0x14: the driver's steps since it was keyed
        s32 serial = 0;             // +0x48: the order it was handed out
    };
    // How a voice is found when none is free: the lowest priority at or under the claim's, the oldest by
    // age among equals (0x55D0B0); or the first handed out among those the claim allows (0x55D1C0).
    struct Claim {
        enum class Take : u8 { ByPriority, Oldest } take = Take::Oldest;
        s32 priority = kNoPriority; // ByPriority
        int group = -1;             // Oldest: only that group's voices (-1: any)
        bool skip_kept = false;     // Oldest: not the voices kept by their tones
    };

    // (all with mutex_ held)
    int claim(const Claim& c);               // a slot (its old voice cut), -1 when none
    bool has_free() const;                   // a free record, and a voice of the mixer for it
    s32 next_serial();
    void age_slots();                        // once a step: ended voices go back to the free list, the others age
    void release_slot(int slot);
    gt3r::VoiceId key_slot(int slot, gt3r::VoiceParams p); // keys the slot's voice; frees the slot when the mixer refuses
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
    // What a slot takes on besides its voice.
    struct SlotSetup {
        s32 priority = 0;
        int group = -1;
        bool kept = false;
    };
    Voice* key_on(const sound::InstBank& bank, std::shared_ptr<const Bytes> adpcm, const sound::Program& program, const sound::Tone& tone,
                  Voice v, bool reverb, const Claim& claim, const SlotSetup& setup);
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
    std::array<Slot, gt3r::VoiceMixer::kVoices> slots_{};
    std::vector<int> free_; // the free list, its head last (0x6518C8 + 0x2AA8)
    s32 serial_ = 0;        // (+0xA0)
    Music music_;
    std::array<Effect, kEffectSequences> effects_{};
    std::vector<Note> notes_;
    std::array<int, 8> master_{};
    bool mono_ = false;
    u32 next_id_ = 1;
    u64 order_ = 0;
};

} // namespace gt4r::audio
