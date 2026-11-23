#include "gt4r/audio/sound_driver.h"

#include <algorithm>
#include <cmath>

namespace gt4r::audio {

using sound::Program;
using sound::Tone;

namespace {

constexpr int kFull = 0x3FFF;
constexpr float kEffectResolution = 240.0f, kStartTempo = 120.0f;
constexpr int kNoteKey16 = 60 << 4; // a held note sounds at key 60

// The mixer's priority: 0 keeps its voice against everything, 255 is taken first. The game's
// runs the other way (a higher number keeps its voice).
u8 mixer_priority(int game) { return u8(255 - std::clamp(game, 0, 255)); }

} // namespace

int SoundDriver::q14(float v) { return std::clamp(int(v * 16383.0f), -0x4000, kFull); }

// A held note's side gain: the game keeps it in 16 bits, so it may pass 1 (the level is held
// to the voice's range after the tone's volume is taken) up to just under 2.
int SoundDriver::note_gain(float v) { return std::clamp(int(v * 16383.0f), -0x8000, 0x7FFF); }

SoundDriver::SoundDriver(gt3r::VoiceMixer& mixer) : mixer_(mixer) { master_.fill(kFull); }

SoundDriver::~SoundDriver() { stop_all(); }

// --- reading a sequence -----------------------------------------------------------------

u8 SoundDriver::byte(Sequence& s) {
    if (!s.data || s.pos >= s.data->size()) { // a stream without its end event: stop there
        s.state = 3;
        return 0;
    }
    return (*s.data)[s.pos++];
}

u32 SoundDriver::number(Sequence& s) {
    u32 v = 0;
    for (;;) {
        const bool more = s.data && s.pos < s.data->size() && ((*s.data)[s.pos] & 0x80);
        v = v << 7 | (byte(s) & 0x7F);
        if (!more) return v;
    }
}

// Ticks an update: beats a second x ticks a beat / updates a second, in the game's order.
void SoundDriver::set_tempo(Sequence& s, float bpm) {
    const float beat = 60000000.0f / bpm; // microseconds
    s.step = ((1000000.0f / beat) * s.resolution) / float(kUpdateRate);
}

// --- voices -----------------------------------------------------------------------------

void SoundDriver::levels(const Voice& v, s16& l, s16& r) const {
    int t = (v.base * v.sequence_volume) >> 14;
    t = (t * master_[size_t(v.kind)]) >> 14;
    int x = t * v.channel_volume / 127;
    x = x * v.expression / 127;
    x = x * v.velocity / 127;
    int pan_l, pan_r;
    sound::pan_gains(v.pan_sequence + v.pan_tone + v.pan_channel - 0x80, pan_l, pan_r);
    int left = std::clamp(((x * pan_l / 127) * v.gain_l) >> 14, -0x4000, kFull);
    int right = std::clamp(((x * pan_r / 127) * v.gain_r) >> 14, -0x4000, kFull);
    if (mono_) left = right = (std::abs(left) + std::abs(right)) / 2;
    l = s16(left);
    r = s16(right);
}

u16 SoundDriver::pitch(Voice& v) {
    int note = v.key16 + int(v.slide.value * 1.3333334f);
    int bend = v.bend, range = v.bend_range;
    if (v.kind == Kind::Music) { // (rounds toward zero here, down below)
        note += (bend - 0x40) * range / 4;
        bend = 0x40;
        range = 1;
    }
    note += v.fine - v.center16 + (((bend - 0x40) * range) >> 2);
    const u32 spu = (u32(sound::pitch_for_offset(note)) * u32(v.pitch_factor)) >> 12;
    return sound::game_pitch_for_spu(u16(std::min<u32>(spu, 0x3FFF)));
}

SoundDriver::Voice* SoundDriver::key_on(const sound::InstBank&, std::shared_ptr<const Bytes> adpcm, const Program& program, const Tone& tone,
                                        Voice v, bool reverb, int priority) {
    v.base = int(program.volume) * int(tone.volume);
    v.center16 = int(tone.center) << 4;
    v.fine = tone.fine;
    v.bend_range = tone.flags & Tone::kProgramBend ? program.bend_range : tone.bend_range;
    v.order = order_++;
    v.dirty = false;

    gt3r::VoiceParams p;
    p.data = std::move(adpcm);
    p.start = tone.address;
    p.pitch = pitch(v);
    levels(v, p.vol_l, p.vol_r);
    p.adsr1 = tone.adsr1;
    p.adsr2 = tone.adsr2;
    p.reverb = reverb;
    p.priority = mixer_priority(priority);
    p.bus = v.kind == Kind::Music ? gt3r::VoiceBus::Music : gt3r::VoiceBus::Effects;
    v.id = mixer_.key_on(p);
    if (!v.id.valid()) return nullptr;
    voices_.push_back(v);
    return &voices_.back();
}

void SoundDriver::update_voices() {
    for (size_t i = 0; i < voices_.size();) {
        Voice& v = voices_[i];
        if (!mixer_.playing(v.id)) {
            voices_[i] = voices_.back();
            voices_.pop_back();
            continue;
        }
        bool pitch_moved = false;
        if (v.slide.running) {
            Slide& s = v.slide;
            s.value += s.step;
            if ((s.step > 0 && s.target <= s.value) || (s.step < 0 && s.value <= s.target) || s.step == 0) {
                s.value = s.target;
                s.step = 0;
                s.running = false;
            }
            pitch_moved = true;
        }
        if (v.dirty) {
            s16 l, r;
            levels(v, l, r);
            mixer_.set_volume(v.id, l, r);
        }
        if (v.dirty || pitch_moved) mixer_.set_pitch(v.id, pitch(v));
        v.dirty = false;
        ++i;
    }
}

void SoundDriver::release(Kind kind, u32 owner, bool at_once) {
    for (Voice& v : voices_) {
        if (v.kind != kind || v.owner != owner) continue;
        if (at_once)
            mixer_.stop(v.id);
        else
            mixer_.key_off(v.id);
    }
}

bool SoundDriver::owns_voice(Kind kind, u32 owner) const {
    for (const Voice& v : voices_)
        if (v.kind == kind && v.owner == owner && mixer_.playing(v.id)) return true;
    return false;
}

// --- music ------------------------------------------------------------------------------

void SoundDriver::play_music(std::shared_ptr<const sound::InstBank> bank, std::shared_ptr<const sound::SqtBank> sequences, int number) {
    std::lock_guard lock(mutex_);
    stop_music_locked(true);
    if (!bank || !sequences || sequences->sequences.empty()) return;
    if (number < 0 || size_t(number) >= sequences->sequences.size()) number = 0;
    const sound::SqtSequence& seq = sequences->sequences[size_t(number)];
    Music& m = music_;
    m = Music{};
    m.bank = std::move(bank);
    m.sequences = std::move(sequences);
    m.data = m.sequences->data;
    m.number = number;
    m.start = m.pos = seq.events;
    m.resolution = float(seq.resolution);
    set_tempo(m, kStartTempo);
    m.state = 2;
}

void SoundDriver::stop_music_locked(bool at_once) {
    if (music_.state == 0) return;
    release(Kind::Music, 0, at_once);
    music_.state = 0;
    music_.number = -1;
}

void SoundDriver::stop_music(bool at_once) {
    std::lock_guard lock(mutex_);
    stop_music_locked(at_once);
}

void SoundDriver::fade_music(Fade mode, float target, float step) {
    std::lock_guard lock(mutex_);
    music_.fade = mode;
    music_.fade_target = int(target * 16383.0f);
    music_.fade_step = int(step * 16383.0f);
    music_.fade_value = music_.volume;
}

bool SoundDriver::music_playing() const {
    std::lock_guard lock(mutex_);
    return music_.state == 2;
}
bool SoundDriver::music_active() const {
    std::lock_guard lock(mutex_);
    return music_.state != 0;
}
int SoundDriver::music_number() const {
    std::lock_guard lock(mutex_);
    return music_.state ? music_.number : -1;
}
int SoundDriver::music_loops() const {
    std::lock_guard lock(mutex_);
    return music_.loops;
}
double SoundDriver::music_ticks() const {
    std::lock_guard lock(mutex_);
    return music_.time;
}

void SoundDriver::tick_music() {
    Music& m = music_;
    if (m.state == 2) {
        m.time += m.step;
        while (m.time - m.next >= 0.0f) {
            if (m.data && m.pos < m.data->size() && ((*m.data)[m.pos] & 0x80)) m.status = byte(m);
            music_event();
            if (m.state != 2) break;
            m.next += float(number(m));
        }
    }
    if (m.state == 3 && !owns_voice(Kind::Music, 0)) {
        m.state = 0;
        m.number = -1;
    }
    if (m.state == 0 || m.fade == Fade::None) return;
    int value = m.fade_value;
    if (m.fade == Fade::Up) {
        value += m.fade_step;
        if (value >= m.fade_target) {
            value = m.fade_target;
            m.fade = Fade::None;
        }
    } else {
        value -= m.fade_step;
        if (value <= m.fade_target) {
            value = m.fade_target;
            if (m.fade == Fade::DownAndStop) stop_music_locked(true);
            m.fade = Fade::None;
        }
    }
    m.fade_value = m.volume = value;
    for (Voice& v : voices_)
        if (v.kind == Kind::Music) {
            v.sequence_volume = value;
            v.dirty = true;
        }
}

void SoundDriver::music_event() {
    Music& m = music_;
    const int channel = m.status & 0x0F;
    switch (m.status >> 4) {
    case 0x8:
        music_note_off(channel, byte(m));
        break;
    case 0x9: {
        const int key = byte(m), velocity = byte(m);
        if (velocity)
            music_note_on(channel, key, velocity);
        else
            music_note_off(channel, key);
        break;
    }
    case 0xB: {
        const u8 controller = byte(m), value = byte(m);
        music_control(controller, value, channel);
        break;
    }
    case 0xC: {
        Channel& c = m.channels[size_t(channel)];
        c.program = byte(m);
        c.voice = m.bank->program(c.program);
        break;
    }
    case 0xE: {
        Channel& c = m.channels[size_t(channel)];
        c.bend = byte(m);
        for (Voice& v : voices_)
            if (v.kind == Kind::Music && v.channel == channel) {
                v.bend = c.bend;
                v.dirty = true;
            }
        break;
    }
    case 0xF: {
        if (m.status != 0xFF) break;
        const u8 type = byte(m), length = byte(m);
        if (type == 0x2F) {
            m.state = 3;
        } else if (type == 0x51) {
            u32 tempo = 0;
            for (int i = 0; i < 3; ++i) tempo = tempo << 8 | byte(m);
            if (tempo) set_tempo(m, 60000000.0f / float(tempo));
        } else {
            m.pos += length;
        }
        break;
    }
    default: // the opening time of the stream, and the statuses the game skips
        break;
    }
}

void SoundDriver::music_control(u8 controller, u8 value, int channel) {
    Music& m = music_;
    Channel& c = m.channels[size_t(channel)];
    const auto each = [&](auto&& f) {
        for (Voice& v : voices_)
            if (v.kind == Kind::Music && v.channel == channel) {
                f(v);
                v.dirty = true;
            }
    };
    switch (controller) {
    case sound::kCtlLfoRate: c.lfo_rate = value; break;
    case sound::kCtlLfoDepth: c.lfo_depth = value; break;
    case sound::kCtlVolume:
        c.volume = value;
        each([&](Voice& v) { v.channel_volume = value; });
        break;
    case sound::kCtlExpression:
        c.expression = value;
        each([&](Voice& v) { v.expression = value; });
        break;
    case sound::kCtlPan:
        c.pan = value;
        each([&](Voice& v) { v.pan_channel = value; });
        break;
    case sound::kCtlHold:
        c.hold = value;
        if (!value) each([&](Voice& v) { mixer_.key_off(v.id); });
        break;
    case sound::kCtlReverb: c.reverb = value; break;
    case sound::kCtlPhase:
        c.gain_l = value % 3 == 1 ? -kFull : kFull;
        c.gain_r = value % 3 == 2 ? -kFull : kFull;
        each([&](Voice& v) {
            v.gain_l = c.gain_l;
            v.gain_r = c.gain_r;
        });
        break;
    case sound::kCtlLoop:
        if (value == sound::kLoopStart) {
            m.loop_set = true;
            m.loop_pos = m.pos;
            m.loop_status = m.status;
            m.loop_time = m.time;
            m.loop_next = m.next;
        } else if (value == sound::kLoopEnd) {
            if (m.loop_count != 0x7F) --m.loop_count;
            if (m.loop_count != 0 && m.loop_set) {
                m.status = m.loop_status;
                m.pos = m.loop_pos;
                m.time = m.loop_time;
                m.next = m.loop_next;
                ++m.loops;
            }
        }
        break;
    case sound::kCtlLoopCount: m.loop_count = value; break;
    default: break;
    }
}

void SoundDriver::music_note_on(int channel, int key, int velocity) {
    Music& m = music_;
    const Channel& c = m.channels[size_t(channel)];
    if (!c.voice || !c.volume || !c.expression) return;
    for (const Tone* tone : c.voice->tones_for_key(key)) {
        Voice v;
        v.kind = Kind::Music;
        v.channel = channel;
        v.key = key;
        v.key16 = key << 4;
        v.sequence_volume = m.volume;
        v.channel_volume = c.volume;
        v.expression = c.expression;
        v.velocity = m.bank->velocity_map[size_t(velocity & 0x7F)];
        v.gain_l = c.gain_l;
        v.gain_r = c.gain_r;
        v.pan_tone = tone->pan;
        v.pan_channel = c.pan;
        v.bend = c.bend;
        key_on(*m.bank, m.bank->adpcm, *c.voice, *tone, v, tone->reverb() || c.reverb, 0);
    }
}

void SoundDriver::music_note_off(int channel, int key) {
    for (Voice& v : voices_)
        if (v.kind == Kind::Music && v.channel == channel && v.key == key) mixer_.key_off(v.id);
}

// --- effect sequences -------------------------------------------------------------------

SoundDriver::Effect* SoundDriver::effect(u32 id) {
    if (id)
        for (Effect& e : effects_)
            if (e.id == id && e.state) return &e;
    return nullptr;
}
const SoundDriver::Effect* SoundDriver::effect(u32 id) const { return const_cast<SoundDriver*>(this)->effect(id); }

u32 SoundDriver::play_effect(std::shared_ptr<const sound::InstBank> bank, int set, int entry, const SoundParams& p) {
    std::lock_guard lock(mutex_);
    const sound::EffectSequence* seq = bank ? bank->effect_sequence(set, entry) : nullptr;
    if (!seq) return 0;
    Effect* slot = nullptr;
    for (Effect& e : effects_)
        if (!e.state) {
            slot = &e;
            break;
        }
    if (!slot) { // all twelve are busy: the one started first makes way
        slot = &*std::min_element(effects_.begin(), effects_.end(), [](const Effect& a, const Effect& b) { return a.order < b.order; });
        release(Kind::Effect, slot->id, true);
    }
    Effect& e = *slot;
    e = Effect{};
    e.id = next_id_++;
    e.order = order_++;
    e.data = bank->head;
    e.bank = std::move(bank);
    e.start = e.pos = seq->offset;
    e.resolution = kEffectResolution;
    e.params = p;
    set_tempo(e, kStartTempo);
    e.state = 2;
    return e.id;
}

void SoundDriver::set_effect(u32 id, const SoundParams& p) {
    std::lock_guard lock(mutex_);
    Effect* e = effect(id);
    if (!e) return;
    e->params = p;
    for (Voice& v : voices_)
        if (v.kind == Kind::Effect && v.owner == id) {
            v.gain_l = q14(p.left);
            v.gain_r = q14(p.right);
            v.pan_sequence = p.pan;
            v.pitch_factor = int(p.pitch * 4096.0f);
            v.dirty = true;
        }
}

void SoundDriver::stop_effect(u32 id) {
    std::lock_guard lock(mutex_);
    if (Effect* e = effect(id)) {
        release(Kind::Effect, id, false);
        e->state = 3;
    }
}

bool SoundDriver::effect_playing(u32 id) const {
    std::lock_guard lock(mutex_);
    return effect(id) != nullptr;
}

void SoundDriver::tick_effect(Effect& e) {
    if (e.state == 2) {
        e.time += e.step;
        while (e.time - e.next >= 0.0f) {
            if (e.data && e.pos < e.data->size() && ((*e.data)[e.pos] & 0x80)) e.status = byte(e);
            effect_event(e);
            if (e.state != 2) break;
            if (e.skip_time)
                e.skip_time = false;
            else
                e.next += float(number(e));
        }
    }
    if (e.state == 3 && !owns_voice(Kind::Effect, e.id)) e.state = 0;
}

void SoundDriver::effect_event(Effect& e) {
    switch (e.status >> 4) {
    case 0xA: {
        const int key = byte(e), velocity = byte(e), program = byte(e);
        if (velocity) {
            effect_note_on(e, program, key, velocity);
        } else { // the oldest voice of that key lets go
            Voice* oldest = nullptr;
            for (Voice& v : voices_)
                if (v.kind == Kind::Effect && v.owner == e.id && v.channel == program && v.key == key && (!oldest || v.order < oldest->order))
                    oldest = &v;
            if (oldest) mixer_.key_off(oldest->id);
        }
        break;
    }
    case 0xB: {
        const u8 type = byte(e), a = byte(e), b = byte(e), c = byte(e);
        if (type == 0x60) { // jump to an offset in the sequence, `c` times (0: without end)
            if (c) {
                if (e.loop_count == c) {
                    e.loop_count = 0;
                    break;
                }
                ++e.loop_count;
            }
            e.time = 0;
            e.skip_time = true;
            e.next = float(number(e));
            e.pos = e.start + (size_t(b) << 8 | a);
            break;
        }
        if (type < 3) break; // lfo depth and rate: the effect banks carry no lfo tables
        const int key = byte(e);
        const int duration = int(a) * 1000 / 15; // milliseconds
        if (type == 0x07 || type == 0x0A) break; // level and pan slides: in no sequence of the discs
        // A pitch slide of `b` tenths of a semitone over the duration.
        const int amount = int(gt3r::s8(b)) * 1200000 / 1000000;
        for (Voice& v : voices_)
            if (v.kind == Kind::Effect && v.owner == e.id && v.channel == c && v.key == key) {
                v.slide.target += float(amount);
                v.slide.step = duration > 0 ? float(amount) * (1000.0f / (float(kUpdateRate) * float(duration))) : float(amount);
                v.slide.running = true;
            }
        break;
    }
    case 0xF:
        if (e.status == 0xFF) {
            const u8 type = byte(e);
            byte(e);
            if (type == 0x2F) e.state = 3;
        }
        break;
    default: break;
    }
}

void SoundDriver::effect_note_on(Effect& e, int program, int key, int velocity) {
    const Program* p = e.bank->effect_program(program);
    if (!p) return;
    const int n = key - p->key_base;
    if (n < 0 || size_t(n) >= p->tones.size() || p->tones[size_t(n)].empty()) return;
    const Tone& tone = p->tones[size_t(n)];
    Voice v;
    v.kind = Kind::Effect;
    v.owner = e.id;
    v.channel = program;
    v.key = key;
    v.key16 = key << 4;
    v.sequence_volume = e.volume;
    v.velocity = e.bank->velocity_map[size_t(velocity & 0x7F)];
    v.gain_l = q14(e.params.left);
    v.gain_r = q14(e.params.right);
    v.pan_sequence = e.params.pan;
    v.pan_channel = tone.pan;
    v.pitch_factor = int(e.params.pitch * 4096.0f);
    key_on(*e.bank, e.bank->adpcm, *p, tone, v, tone.reverb(), e.params.priority ? e.params.priority : tone.priority());
}

// --- held notes -------------------------------------------------------------------------

u32 SoundDriver::note_on(std::shared_ptr<const sound::InstBank> bank, int program, const SoundParams& p) {
    std::lock_guard lock(mutex_);
    const Program* g = bank ? bank->program(program) : nullptr;
    if (!g || g->tones.empty() || g->tones[0].empty()) return 0;
    const Tone& tone = g->tones[0];
    Voice v;
    v.kind = Kind::Note;
    v.owner = next_id_;
    v.key = 60;
    v.key16 = kNoteKey16;
    v.gain_l = note_gain(p.left);
    v.gain_r = note_gain(p.right);
    v.pan_sequence = p.pan;
    v.pan_channel = tone.pan;
    v.pitch_factor = int(p.pitch * 4096.0f);
    if (!key_on(*bank, bank->adpcm, *g, tone, v, tone.flags != 0 && !p.dry, p.priority)) return 0;
    notes_.push_back({next_id_, std::move(bank)});
    return next_id_++;
}

void SoundDriver::set_note(u32 id, const SoundParams& p) {
    std::lock_guard lock(mutex_);
    for (Voice& v : voices_)
        if (v.kind == Kind::Note && v.owner == id) {
            v.gain_l = note_gain(p.left);
            v.gain_r = note_gain(p.right);
            v.pan_sequence = p.pan;
            v.pitch_factor = int(p.pitch * 4096.0f);
            v.dirty = true;
            mixer_.set_priority(v.id, mixer_priority(p.priority));
        }
}

void SoundDriver::note_off(u32 id) {
    std::lock_guard lock(mutex_);
    release(Kind::Note, id, false);
}

bool SoundDriver::note_playing(u32 id) const {
    std::lock_guard lock(mutex_);
    return id && owns_voice(Kind::Note, id);
}

// --- the driver -------------------------------------------------------------------------

void SoundDriver::update() {
    std::lock_guard lock(mutex_);
    for (Effect& e : effects_) tick_effect(e);
    tick_music();
    update_voices();
    std::erase_if(notes_, [&](const Note& n) { return !owns_voice(Kind::Note, n.id); });
}

void SoundDriver::set_volume(Kind kind, float v) {
    std::lock_guard lock(mutex_);
    master_[size_t(kind)] = std::clamp(int(v * 16383.0f), 0, kFull);
    for (Voice& voice : voices_)
        if (voice.kind == kind) voice.dirty = true;
}

float SoundDriver::volume(Kind kind) const {
    std::lock_guard lock(mutex_);
    return float(master_[size_t(kind)]) / 16383.0f;
}

void SoundDriver::set_mono(bool mono) {
    std::lock_guard lock(mutex_);
    mono_ = mono;
    for (Voice& v : voices_) v.dirty = true;
}

void SoundDriver::stop_all() {
    std::lock_guard lock(mutex_);
    for (Voice& v : voices_) mixer_.stop(v.id);
    voices_.clear();
    notes_.clear();
    music_.state = 0;
    music_.number = -1;
    for (Effect& e : effects_) e.state = 0;
}

int SoundDriver::voices() const {
    std::lock_guard lock(mutex_);
    int n = 0;
    for (const Voice& v : voices_) n += mixer_.playing(v.id);
    return n;
}

} // namespace gt4r::audio
