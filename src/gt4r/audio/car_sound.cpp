#include "gt4r/audio/car_sound.h"

#include "base/log.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/equipment.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt4r::audio {

namespace {

constexpr const char* kWindBank = "sound/roadnoiz.es";
constexpr size_t kEngineSoundId = 0x18, kMufflerVariant = 6, kTurboKind = 9, kGearWhine = 0x22;

// A car's part as sold: from its row of default parts, or for a car without one its own
// first row of the table ("en_<car label>_a").
std::optional<u32> part(db::SpecDb& db, const std::string& car, const char* table, const char* prefix) {
    const db::Labels* l = db.labels(table);
    if (!l) return std::nullopt;
    if (const auto id = db.default_part(car, l->table_id)) return id;
    const db::Labels* defaults = db.labels("DEFAULT_PARTS");
    if (defaults && defaults->id("df_pt_" + car)) return std::nullopt; // (sold without the part)
    return l->id(prefix + car + "_a");
}

} // namespace

namespace {

// The files the four parts' rows name (an empty row: no such part).
CarSoundFiles files_of(ByteSpan engine, ByteSpan muffler, ByteSpan turbine, ByteSpan gear) {
    CarSoundFiles f;
    if (engine.size() < kEngineSoundId + 2) return f;
    f.id = rd16(engine, kEngineSoundId);
    if (muffler.size() > kMufflerVariant) f.muffler = muffler[kMufflerVariant] & 3;
    f.turbo = turbine.size() > kTurboKind && turbine[kTurboKind] != 0;
    f.whine = gear.size() > kGearWhine && gear[kGearWhine] != 0;
    f.variant = CarSoundFiles::variant_name(f.variant_index());
    char name[16];
    std::snprintf(name, sizeof name, "%05d", f.id);
    f.engine = std::string("sound/engine/") + name;
    f.exhaust = "sound/" + f.variant + "/" + name;
    return f;
}

} // namespace

CarSoundFiles car_sound_files(db::SpecDb& db, const std::string& car) {
    const auto row = [&](const char* table, const char* prefix) -> ByteSpan {
        const db::Table* t = db.table(table);
        const std::optional<u32> id = t ? part(db, car, table, prefix) : std::nullopt;
        return id ? t->row(*id) : ByteSpan();
    };
    return files_of(row("ENGINE", "en_"), row("MUFFLER", "mu_"), row("TURBINEKIT", "tk_"), row("GEAR", "ge_"));
}

CarSoundFiles car_sound_files(db::SpecDb& db, const sim::Equipment& eq) {
    using E = sim::Equipment;
    const auto row = [&](u32 slot) { return db::part_row(db, eq.part(slot)); };
    return files_of(row(E::Engine), row(E::Muffler), row(E::TurbineKit), row(E::Gear));
}

std::string CarSoundFiles::variant_name(int index) {
    index = std::clamp(index, 0, 7);
    return (index < 4 ? "normal" : "turbo") + std::to_string(index & 3);
}

CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const std::string& car) {
    try {
        db::SpecDb db([&vfs](const std::string& path) { return vfs.read(path); }, [&vfs](const std::string& dir) { return vfs.list_dir(dir); });
        return car_sound_files(db, car);
    } catch (const std::exception&) {
        return {};
    }
}

CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const sim::Equipment& equipment) {
    try {
        db::SpecDb db([&vfs](const std::string& path) { return vfs.read(path); }, [&vfs](const std::string& dir) { return vfs.list_dir(dir); });
        return car_sound_files(db, equipment);
    } catch (const std::exception&) {
        return {};
    }
}

gt3r::sound::EngineBank parse_engine_bank(ByteSpan d) {
    try {
        return gt3r::sound::EngineBank::parse(d);
    } catch (const FormatError&) {
        // Below: the same file read without asking for the layers in data order.
    }
    if (!gt3r::has_magic(d, "ENGN") || d.size() < 0x30) throw FormatError("not an ENGN bank");
    const u32 data_size = rd32(d, 0x10), data_off = rd32(d, 0x14), count = rd32(d, 0x18), table = rd32(d, 0x1C);
    if (u64(data_off) + data_size > d.size()) throw FormatError("ENGN: data out of range");
    if (count == 0 || count > 64 || u64(table) + 16ull * count > d.size()) throw FormatError("ENGN: bad layer table");
    gt3r::sound::EngineBank b;
    b.unknown_28 = rd32(d, 0x28);
    b.unknown_2c = rd32(d, 0x2C);
    b.adpcm = std::make_shared<const Bytes>(d.begin() + data_off, d.begin() + data_off + data_size);
    std::vector<u32> starts;
    for (u32 i = 0; i < count; ++i) {
        const size_t o = table + 16 * i;
        gt3r::sound::EngineLayer l;
        l.base_rpm = rd16(d, o);
        l.lo_rpm = s16(rd16(d, o + 2));
        l.hi_rpm = s16(rd16(d, o + 4));
        l.volume = s16(rd16(d, o + 6));
        l.pitch = rd32(d, o + 8);
        l.offset = rd32(d, o + 12);
        if (l.offset >= data_size || l.offset % 16) throw FormatError("ENGN: bad layer offset");
        starts.push_back(l.offset);
        b.layers.push_back(l);
    }
    std::sort(starts.begin(), starts.end());
    for (auto& l : b.layers) {
        const auto next = std::upper_bound(starts.begin(), starts.end(), l.offset);
        l.size = (next == starts.end() ? data_size : *next) - l.offset;
    }
    return b;
}

// --- the mix ----------------------------------------------------------------------------

namespace {

constexpr float kPi = 3.14159265f;
// Sound's way in a step of 1/60 s, metres (331.45 m/s).
constexpr float kSoundStep = 5.5241666f;
// Where the engine, the cabin and the exhaust sit from the driver's head, metres ahead.
constexpr float kAhead = 1.25f, kHere = 0.01f;
// A held note's level rises at once and falls by this much a step; a rise of less is topped up.
constexpr float kFall = 0.05f, kRise = 0.025f;
constexpr float kNoteThreshold = 0.001f; // a single note weaker than this is not played
constexpr float kBackfire = 3.0f, kLandingGain = 2.5f;

float lerp_clamped(float v, float lo, float hi, float at_lo, float at_hi) {
    if (v < lo) return at_lo;
    if (hi < v) return at_hi;
    return at_lo + (v - lo) * (at_hi - at_lo) / (hi - lo);
}

float dot(const race::Vec3& a, const race::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
race::Vec3 sub(const race::Vec3& a, const race::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

bool tarmac(int surface) { return surface == 0 || (surface >= 7 && surface <= 9) || surface == 12; }


} // namespace

int sound_surface(u8 material, u8 flags) {
    if (!(flags & 2)) return material;
    switch (material) {
    case 0:
    case 7: return 20;
    case 1:
    case 10:
    case 11:
    case 12: return 21;
    case 9: return 22;
    default: return material;
    }
}

SoundView sound_view(SoundListener::Kind kind, bool target) {
    switch (kind) {
    case SoundListener::Kind::InCar: return target ? SoundView::InCarTarget : SoundView::InCarOther;
    case SoundListener::Kind::Chase: return target ? SoundView::ChaseTarget : SoundView::ChaseOther;
    default: return SoundView::Other;
    }
}

Placement place_sound(const RaceSoundTables::Mic& mic, float x, float z, bool cue_moved) {
    Placement p;
    const float z0 = z;
    float d = std::sqrt(x * x + z * z);
    if (d < 0.25f) { // (a sound at the camera is moved a quarter metre along its axis)
        z = z + 0.25f - d;
        d = std::sqrt(x * x + z * z);
    }
    p.gain = mic.slope > 0 ? 1.0f / (1.0f + mic.slope * std::max(d - mic.reach, 0.0f)) : 32.0f / d;
    const float angle = std::clamp((x / d + 1.0f) * (kPi / 4), 0.0f, kPi / 2);
    p.left = std::cos(angle), p.right = std::sin(angle);
    // The surround cue's side: Gran Turismo 4 (0x391AF0) takes the place before the quarter metre over the
    // distance after it, Prologue (0x3A18A8) the moved place.
    p.x = x / d, p.z = (cue_moved ? z : z0) / d;
    return p;
}

SoundListener listener_at(const race::CarState& car, SoundListener::Kind kind) {
    SoundListener l;
    l.kind = kind;
    l.right = car.right, l.up = car.up, l.forward = car.forward;
    l.velocity = car.velocity;
    l.position = car.position;
    if (kind != SoundListener::Kind::InCar) { // (a chase camera's usual place: behind and above)
        const float back = 6.0f, above = 1.5f;
        l.position = {car.position.x - car.forward.x * back + car.up.x * above, car.position.y - car.forward.y * back + car.up.y * above,
                      car.position.z - car.forward.z * back + car.up.z * above};
    }
    return l;
}

float contact_strength(int kind, float value) {
    float f = 0;
    if (kind == 0) f = value / 50000.0f;
    else if (kind == 1) f = value + value;
    else if (kind == 2) f = std::max(std::fabs(value / 50.0f), 0.2f);
    f = std::min(f, 1.0f);
    return f < 0.2f ? 0.0f : f;
}

CarSoundState sound_state(const race::CarState& car) {
    CarSoundState s;
    // The seam's contact is the body's strength (simulation.h): the step's impulses |j| / 100,000
    // against a wall, / 15,000 between cars. The hit sound records each contact's own: a wall's |j|
    // (kind 0, over 50,000), a car's |j| / 15,000 (kind 1, doubled): twice the body's either way.
    if (car.contact_kind == 1) s.contact_kind = 0, s.contact = contact_strength(0, car.contact * 100000.0f);
    else if (car.contact_kind == 2) s.contact_kind = 1, s.contact = contact_strength(1, car.contact);
    s.position = car.position, s.velocity = car.velocity;
    s.course_distance = car.course_v;
    s.rpm = car.engine_rpm;
    s.throttle = car.throttle;
    s.speed = car.speed;
    s.clutch = car.gear == 0 ? 0 : 1;
    for (size_t w = 0; w < 4; ++w) {
        const race::WheelState& in = car.wheels[w];
        WheelSoundState& o = s.wheels[w];
        o.position = in.center;
        o.surface = sound_surface(in.material, in.surface);
        o.rolling = in.on_ground;
        o.slip = in.slip;
        // The skid's pitch: three quarters rolling straight on tarmac, a half on other ground,
        // rising as the tire slides.
        o.pitch = (tarmac(o.surface) ? 0.75f : 0.5f) + 0.45f * in.slip;
        o.rim_speed = std::fabs(car.speed);
        if (car.landed && in.on_ground) o.landing = std::min(1.0f, std::max(0.0f, -car.velocity.y) / 5.0f);
    }
    return s;
}

CarSound::CarSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver* effects, std::shared_ptr<SoundLibrary> library)
    : vfs_(vfs), mixer_(mixer), effects_(effects), library_(std::move(library)), mix_(std::make_shared<RaceMix>()) {}

CarSound::~CarSound() { stop(); }

void CarSound::set_mix(std::shared_ptr<const RaceMix> mix) {
    if (mix) mix_ = std::move(mix);
}

bool CarSound::load(const std::string& car_label) {
    return load(car_sound_files(vfs_, car_label));
}

bool CarSound::load(const CarSoundFiles& files) {
    files_ = files;
    if (files_.engine.empty()) return false;
    return load(files_.id, files_.variant_index());
}

bool CarSound::load(const sim::Equipment& equipment) {
    files_ = car_sound_files(vfs_, equipment);
    if (files_.engine.empty()) return false;
    return load(files_.id, files_.variant_index());
}

bool CarSound::load(int sound_id, int variant) {
    stop();
    files_.id = sound_id;
    files_.muffler = variant & 3;
    files_.turbo = (variant & 4) != 0;
    files_.variant = CarSoundFiles::variant_name(variant);
    char name[16];
    std::snprintf(name, sizeof name, "%05d", sound_id);
    files_.engine = std::string("sound/engine/") + name;
    files_.exhaust = "sound/" + files_.variant + "/" + name;
    const auto bank = [&](const std::string& path) -> std::shared_ptr<const gt3r::sound::EngineBank> {
        if (banks_)
            if (const auto it = banks_->find(path); it != banks_->end()) return it->second;
        std::shared_ptr<const gt3r::sound::EngineBank> b;
        try {
            if (const auto d = vfs_.read(path)) b = std::make_shared<const gt3r::sound::EngineBank>(parse_engine_bank(*d));
        } catch (const std::exception& e) {
            LOG_WARN("%s: %s", path.c_str(), e.what());
        }
        if (banks_) (*banks_)[path] = b;
        return b;
    };
    engine_.set_bank(bank(files_.engine));
    exhaust_.set_bank(bank(files_.exhaust));
    engine_level_ = exhaust_level_ = 0;
    if (!wind_tried_) {
        wind_tried_ = true;
        if (const auto w = bank(kWindBank)) wind_.set_bank(w);
    }
    return loaded();
}

const char* CarSound::wheel_note_name(int note) {
    static const char* const kNames[kWheelNotes] = {"on_skill", "on_road", "rain_noiz", "off_roadnoiz", "off_skill",
                                                    "pebble",   "sand",    "rumble",    "rumble2",      "rumble3"};
    return note >= 0 && note < kWheelNotes ? kNames[note] : "";
}

CarSound::Effect CarSound::effect(const char* name) {
    Effect e;
    const sound::EffectEntry* entry = library_->effects().find(name);
    if (!entry || entry->sequence) return e;
    e.bank = library_->effect_bank(entry->bank);
    e.program = e.bank && e.bank->program(entry->index) ? entry->index : -1;
    return e;
}

void CarSound::resolve() {
    resolved_ = true;
    if (!effects_ || !library_) return;
    turbine_fx_ = effect("turbine"), whine_fx_ = effect("turbine2");
    cabin_fx_ = effect("in_car"), nitrous_fx_ = effect("nos");
    blow_off_fx_ = effect("b_off"), landing_fx_ = effect("on_bound");
    backfire_fx_[0] = effect("backfire"), backfire_fx_[1] = effect("backfire2");
    heartbeat_fx_ = effect("heartbeat");
    clash_fx_[0] = effect("clash"), clash_fx_[1] = effect("clash2"), clash_fx_[2] = effect("clash4");
    for (int n = 0; n < kWheelNotes; ++n) wheel_fx_[size_t(n)] = effect(wheel_note_name(n));
}

float CarSound::ease(Loop& loop, float level) {
    loop.held = loop.held < level ? loop.held + kRise : loop.held - kFall;
    if (loop.held < level) loop.held = level;
    return loop.held;
}

void CarSound::send(Loop& loop, const Effect& e, float level, const Placement& at, float pitch) {
    if (!effects_ || !e) return;
    const float l = at.left * level, r = at.right * level;
    if (std::fabs(l) * 16383.0f < 1.0f && std::fabs(r) * 16383.0f < 1.0f) {
        if (loop.id) effects_->note_off(loop.id), loop.id = 0;
        return;
    }
    SoundParams p;
    p.left = l, p.right = r;
    p.pitch = pitch;
    p.priority = level_priority(level);
    if (loop.id && effects_->note_playing(loop.id))
        effects_->set_note(loop.id, p);
    else
        loop.id = effects_->note_on(e.bank, e.program, p);
}

void CarSound::play_once(const Effect& e, float strength, float gain, const Placement& at) {
    if (!effects_ || !e || strength < kNoteThreshold) return;
    const float level = strength * gain;
    if (level == 0) return;
    SoundParams p;
    p.left = at.left * level, p.right = at.right * level;
    p.priority = level_priority(level);
    if (sound_log()) LOG_INFO("Sound: a car's single note of program %d at %.4f", e.program, double(level));
    if (effects_->note_on(e.bank, e.program, p)) ++report_.notes;
}

void CarSound::update(const CarSoundState& car, const SoundListener& listener, bool target) {
    if (!resolved_) resolve();
    const RaceMix& mix = *mix_;
    const RaceSoundTables& t = mix.tables;
    const RaceSoundTables::Params& k = t.params;
    const SoundView view = sound_view(listener.kind, target);
    const auto gain = [&](SoundChannel c) { return t.view_gain(view, c); };
    report_ = Report{};
    report_.view = view;

    // The car from the camera, and the Doppler factor of everything it sounds.
    const race::Vec3 rel = sub(car.position, listener.position);
    float doppler = 1.0f;
    {
        float length = std::sqrt(dot(rel, rel));
        if (length == 0) length = 1.0f;
        const race::Vec3 to{rel.x / length, rel.y / length, rel.z / length};
        const float camera = dot(to, listener.velocity) / 60.0f, source = dot(to, car.velocity) / 60.0f;
        doppler = (camera + kSoundStep) / (source + kSoundStep);
    }
    report_.doppler = doppler;

    // The three places: the engine's, the body's and the exhaust's. From the car's own in-car
    // view they are around the driver's head; from anywhere else all three are the car's.
    float px[3], pz[3];
    if (view == SoundView::InCarTarget) {
        const float engine = t.prologue ? 0.0f : car.engine_place == 2 ? kHere : car.engine_place == 3 ? -kAhead : kAhead;
        const float ahead[3] = {engine, t.prologue ? 0.0f : kHere, t.prologue ? 0.0f : -kAhead};
        const float c = std::cos(listener.look), s = std::sin(listener.look);
        for (int i = 0; i < 3; ++i) px[i] = -ahead[i] * s, pz[i] = ahead[i] * c;
    } else {
        const float x = dot(rel, listener.right), z = dot(rel, listener.forward);
        for (int i = 0; i < 3; ++i) px[i] = x, pz[i] = z;
    }
    const OutputMode mode = mix.mode;
    Placement at_engine = place_sound(t.mic(mode, 0), px[0], pz[0], t.prologue);
    Placement at_body = place_sound(t.mic(mode, 2), px[1], pz[1], t.prologue);
    Placement at_exhaust = place_sound(t.mic(mode, 1), px[2], pz[2], t.prologue);
    const float master = mix.master;
    const auto held = [&](const Placement& p) {
        return t.prologue ? std::clamp(p.gain, -1.0f, 1.0f) * master : std::clamp(p.gain * master, -1.0f, 1.0f);
    };
    const float g_engine = held(at_engine), g_body = held(at_body), g_exhaust = held(at_exhaust);
    report_.engine = at_engine, report_.body = at_body, report_.exhaust = at_exhaust;
    // The surround cue (the panner's two flags, 0x39A7B0): a sound behind the camera has one side
    // inverted. The program does it in every output mode; here when the mixer's option is on.
    const bool cue = mixer_.surround_cue();
    const auto behind = [&](gt3r::SurroundCue& c, Placement& p) {
        if (!cue) return;
        c.update(p.x, p.z);
        if (c.invert_l) p.left = -p.left;
        if (c.invert_r) p.right = -p.right;
    };
    behind(cue_[0], at_engine), behind(cue_[1], at_body), behind(cue_[2], at_exhaust);

    // The two banks' levels: each moves half way to its target every step.
    if (!car.live || (car.hybrid && car.rpm < car.idle_rpm)) {
        engine_level_ = exhaust_level_ = 0;
    } else if (car.engine_level >= 0 && car.exhaust_level >= 0) {
        engine_level_ = car.engine_level, exhaust_level_ = car.exhaust_level;
    } else {
        const float sum = car.torque_full + car.torque_friction;
        const float quiet = sum > 0 ? car.torque_friction / sum : 0.0f; // the throttle that just holds the revs
        const float over = std::min(car.throttle, 1.0f) - quiet;
        float e = std::fabs(over) + quiet;
        if (car.clutch != 1) e *= 0.5f;
        if (car.hybrid) e *= 0.5f;
        engine_level_ = (engine_level_ + std::min(127, int(e * 127.0f * k.engine_level))) / 2;
        float load = quiet < 1.0f ? std::max(over, 0.0f) / (1.0f - quiet) : 0.0f;
        load = 1.0f - load;
        load *= load, load *= load, load *= load, load *= load; // the sixteenth power
        float x = (1.0f - load) * 0.6f + 0.4f;
        if (car.hybrid) x *= 0.5f;
        exhaust_level_ = (exhaust_level_ + std::min(127, int(x * 127.0f * k.exhaust_level))) / 2;
    }
    report_.engine_level = engine_level_, report_.exhaust_level = exhaust_level_;

    // The three layer players.
    const float speed = car.live ? car.speed : 0.0f;
    const float wind = car.live ? std::min(k.wind_gain * car.drag * speed * speed * (1.0f - car.slipstream), k.wind_max) : 0.0f;
    const float wind_rpm = lerp_clamped(std::fabs(speed), k.wind_speed_lo, k.wind_speed_hi, k.wind_rpm_lo, k.wind_rpm_hi);
    report_.engine_gain = g_engine * gain(SoundChannel::Engine) * float(engine_level_) / 64.0f;
    report_.exhaust_gain = g_exhaust * gain(SoundChannel::Exhaust) * float(exhaust_level_) / 64.0f;
    report_.wind_gain = g_engine * gain(SoundChannel::Wind) * wind;
    report_.wind_rpm = wind_rpm;
    feed(engine_, car.rpm, report_.engine_gain, at_engine, doppler);
    feed(exhaust_, car.rpm, report_.exhaust_gain, at_exhaust, doppler);
    feed(wind_, wind_rpm, report_.wind_gain, at_engine, doppler);
    const bool mono = mode == OutputMode::Mono;
    engine_.step(&mixer_, mono, effects_), exhaust_.step(&mixer_, mono, effects_), wind_.step(&mixer_, mono, effects_);
    if (!effects_) return;

    // The turbo's spool and the gearbox's whine, at the engine.
    report_.turbine = g_engine * gain(SoundChannel::Turbine) * float(car.live ? car.turbine_level : 0) / 128.0f;
    send(turbine_, turbine_fx_, report_.turbine, at_engine, float(car.turbine_pitch) / 6144.0f);
    {
        float level = 0, pitch = 1.0f;
        if (car.live && car.whine_given) {
            level = car.whine_level, pitch = car.whine_pitch;
        } else if (car.live && (car.gear_whine || files_.whine) && car.clutch != 0 && k.whine_torque > 0 && car.low_ratio != 0) {
            const float torque = std::min(std::fabs(car.drive_torque), k.whine_torque);
            const float x = std::fabs(torque * car.gear_ratio / (k.whine_torque * car.low_ratio));
            level = k.whine_gain_lo + (k.whine_gain_hi - k.whine_gain_lo) * x;
            pitch = k.whine_pitch_lo + k.whine_pitch_rate * std::fabs(car.shaft_speed);
        }
        report_.whine = g_engine * gain(SoundChannel::GearWhine) * level;
        send(whine_, whine_fx_, report_.whine, at_engine, doppler * pitch);
    }

    // The cabin's rush and nitrous, at the body.
    {
        const float level = car.live ? std::min(k.incar_gain * car.drag * speed * speed * (1.0f - car.cabin_wake), k.incar_max) : 0.0f;
        const float pitch = lerp_clamped(std::fabs(speed), k.incar_speed_lo, k.incar_speed_hi, k.incar_pitch_lo, k.incar_pitch_hi);
        const float rush = g_body * gain(SoundChannel::InCar) * level;
        report_.cabin = t.prologue ? rush : ease(cabin_, rush);
        send(cabin_, cabin_fx_, report_.cabin, at_body, doppler * pitch);
        const float flowing = !t.prologue && view == SoundView::InCarTarget && car.live && car.nitrous ? 1.0f : 0.0f;
        send(nitrous_, nitrous_fx_, ease(nitrous_, g_body * flowing), at_body, 1.0f);
    }

    // The wheels: each sounds from its own place, at the master level once more.
    for (size_t w = 0; w < 4; ++w) {
        const WheelSoundState& wheel = car.wheels[w];
        const race::Vec3 from = sub(wheel.position, listener.position);
        Placement at = place_sound(t.mic(mode, 2), dot(from, listener.right), dot(from, listener.forward), t.prologue);
        behind(wheel_cue_[w], at);
        const float g = t.prologue ? held(at) : held(at) * master;
        float level[kWheelNotes] = {}, pitch[kWheelNotes];
        std::fill(pitch, pitch + kWheelNotes, doppler);
        if (car.live) {
            const int s = wheel.surface;
            const auto slip = [&](int note, SoundChannel c) { level[note] = gain(c) * wheel.slip, pitch[note] = doppler * wheel.pitch; };
            if (tarmac(s)) {
                slip(OnSkill, SoundChannel::OnSkill);
                // The road's roar: by the rim's speed and the tire's width.
                float roar = 0, tone = k.roar_pitch_lo;
                if (wheel.rolling) {
                    const float width = wheel.width < 0.135f ? k.roar_narrow
                                        : wheel.width > 0.355f ? k.roar_wide
                                        : k.roar_narrow + (wheel.width - 0.135f) * (k.roar_wide - k.roar_narrow) / 0.22f;
                    const float v = std::fabs(wheel.rim_speed);
                    roar = std::min((v / 100.0f) * (v / 100.0f) * k.roar_gain * width, k.roar_max);
                    tone = lerp_clamped(v, k.roar_speed_lo, k.roar_speed_hi, k.roar_pitch_lo, k.roar_pitch_hi);
                }
                level[OnRoad] = gain(SoundChannel::OnRoad) * roar, pitch[OnRoad] = doppler * tone;
            }
            if (s == 6 || s == 20 || s == 22) slip(Rain, SoundChannel::Rain);
            if (s == 5) slip(OffRoad, SoundChannel::OffRoad);
            if (s >= 2 && s <= 4) slip(OffSkill, SoundChannel::OffSkill);
            if (s == 13) slip(Pebble, SoundChannel::OffRoad);
            if (s == 14) slip(Sand, SoundChannel::OffRoad);
            if (s == 1 || s == 21) slip(Rumble, SoundChannel::Rumble);
            if (s == 10) slip(Rumble2, SoundChannel::Rumble);
            if (s == 11) slip(Rumble3, SoundChannel::Rumble);
        }
        for (int n = 0; n < kWheelNotes; ++n) {
            Loop& loop = wheels_[w][size_t(n)];
            float v = g * level[n];
            if (n > Rain && !t.prologue) v = ease(loop, v); // (the squeal, the roar and the rain follow at once)
            report_.wheel[w][size_t(n)] = v;
            send(loop, wheel_fx_[size_t(n)], v, at, pitch[n]);
        }
    }

    // Single notes.
    if (!car.live) return;
    play_once(blow_off_fx_, gain(SoundChannel::BlowOff) * car.blow_off, g_engine, at_engine);
    if (target && car.contact > 0)
        play_once(clash_fx_[size_t(std::clamp(car.contact_kind, 0, 2))], gain(SoundChannel::Clash) * car.contact * k.clash_scale, g_body, at_body);
    // The heartbeat (a car of category 3, the Nike One: 0x355868 each step, 0x355950 in the frame): a beat
    // whenever the phase passes 1, the phase rising (0.8 + 2.2 x) / 60 a step with x = |speed| / 66.67 m/s
    // (at most 1), at the strength 0.6 + 0.4 x at the body; heard from the car's own in-car view only.
    report_.heartbeats = heart_count_;
    if (t.prologue) return;
    if (car.heart) {
        const float x = std::min(std::fabs(car.speed) / 66.666664f, 1.0f);
        heart_phase_ += (x * 2.2f + 0.8f) * (1.0f / 60.0f);
        if (heart_phase_ >= 1.0f) {
            heart_phase_ -= 1.0f;
            ++heart_count_;
            report_.heartbeats = heart_count_;
            if (view == SoundView::InCarTarget) play_once(heartbeat_fx_, x * 0.4f + 0.6f, g_body, at_body);
        }
    }
    if (view == SoundView::InCarTarget) {
        float landing = 0;
        for (const WheelSoundState& wheel : car.wheels) {
            const int s = wheel.surface;
            if (s == 1 || s == 10 || s == 11 || (s >= 2 && s <= 4) || s == 6 || s == 13 || s == 14) continue;
            if (wheel.landing >= 0.5f) landing = std::max(landing, std::min(wheel.landing, 1.0f));
        }
        play_once(landing_fx_, landing * gain(SoundChannel::OnBound) * kLandingGain, g_body, at_body);
    }
    // An afterfire: given, or by the program's rule.
    bool flame = car.afterfire;
    const bool strong = car.shift_flame && car.afterfire_chance > 0.1f;
    if (strong && !was_afterfire_) {
        random_ = random_ * 1103515245u + 12345u;
        flame |= float(random_ >> 8) / 16777216.0f < car.afterfire_chance;
    }
    was_afterfire_ = strong;
    if (flame) {
        random_ = random_ * 1103515245u + 12345u;
        play_once(backfire_fx_[(random_ >> 16) & 1], kBackfire, g_exhaust, at_exhaust);
    }
}

void CarSound::feed(LayerPlayer& player, float rpm, float level, const Placement& at, float doppler) {
    if (!player.loaded()) return;
    LayerPlayer::Input& in = player.input();
    const auto side = [](float v) {
        const int i = int(v * 16383.0f);
        return s16(i >= 0x3FFF ? 0x3FFE : std::max(i, -0x3FFF));
    };
    in.left = side(at.left * level), in.right = side(at.right * level);
    in.priority = u32(level_priority(level));
    in.doppler = std::clamp(int(doppler * 4096.0f), 0, 0x3FFF);
    in.rpm = int(s16(int(rpm)));
    in.adsr1 = mix_->tables.layer_adsr1;
}

void CarSound::stop_loops() {
    if (!effects_) return;
    const auto off = [&](Loop& l) {
        if (l.id) effects_->note_off(l.id);
        l = Loop{};
    };
    off(turbine_), off(whine_), off(cabin_), off(nitrous_);
    for (auto& wheel : wheels_)
        for (Loop& l : wheel) off(l);
}

void CarSound::stop() {
    engine_.stop(mixer_);
    exhaust_.stop(mixer_);
    wind_.stop(mixer_);
    stop_loops();
}

} // namespace gt4r::audio
