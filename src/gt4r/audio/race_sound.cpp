#include "gt4r/audio/race_sound.h"

#include "base/log.h"
#include "gt4r/audio/sim_sound_state.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/formats/sound/sound_index.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <cmath>

namespace gt4r::audio {

namespace {

constexpr float kFadeIn = 0.3f;            // the master's fade as the race begins, seconds
constexpr float kQuiet = 1.0f / 16383.0f;  // under this both sides are silent

int count_beep(int f) { return f > 0 && f < 240 && f % 60 == 0; }

} // namespace

RaceSoundOptions race_sound_options(const std::map<std::string, std::variant<s32, float, std::string>>& options) {
    RaceSoundOptions o;
    const auto number = [&](const char* name, int fallback) {
        const auto it = options.find(name);
        if (it == options.end()) return fallback;
        if (const s32* i = std::get_if<s32>(&it->second)) return int(*i);
        if (const float* f = std::get_if<float>(&it->second)) return int(*f);
        return fallback;
    };
    o.effects_on = number("race_se", 1) != 0, o.effects = number("race_se_volume_raw", o.effects);
    o.music_on = number("race_bgm", 1) != 0, o.music = number("race_bgm_volume_raw", o.music);
    o.replay_effects_on = number("replay_se", 1) != 0, o.replay_effects = number("replay_se_volume_raw", o.replay_effects);
    o.replay_music_on = number("replay_bgm", 1) != 0, o.replay_music = number("replay_bgm_volume_raw", o.replay_music);
    if (const auto it = options.find("sound_type"); it != options.end())
        if (const std::string* t = std::get_if<std::string>(&it->second))
            o.mode = *t == "MONOPHONIC" ? OutputMode::Mono : *t == "PROLOGIC2" ? OutputMode::Surround : OutputMode::Stereo;
    return o;
}

const char* race_start_group(RaceMusic k) {
    switch (k) {
    case RaceMusic::Championship: return "champ_start";
    case RaceMusic::TimeTrial: return "timeattack_start";
    case RaceMusic::License: return "license_start";
    case RaceMusic::Coffee: return "coffee_start";
    case RaceMusic::Other: return "etc_start";
    case RaceMusic::Last: return "last_start";
    default: return "start";
    }
}

const char* race_goal_group(RaceMusic k) {
    switch (k) {
    case RaceMusic::Championship: return "champ_goal";
    case RaceMusic::TimeTrial: return "timeattack_goal";
    case RaceMusic::License: return "license_gold";
    case RaceMusic::Coffee: return "coffee_goal";
    case RaceMusic::Other: return "etc_goal";
    case RaceMusic::Last: return "last_goal";
    default: return "goal";
    }
}

RaceSound::RaceSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver& driver, std::shared_ptr<SoundLibrary> library,
                     const RaceSoundTables& tables, StreamPlayer music)
    : vfs_(vfs), mixer_(mixer), driver_(driver), library_(std::move(library)), music_(std::move(music)), mix_(std::make_shared<RaceMix>()) {
    mix_->tables = tables;
    reverb_before_ = mixer_.reverb_depth();
    fade(1.0f, kFadeIn);
    set_options(options_);
    if (library_)
        for (int c = 0; c < CourseSound::kChannels; ++c) ambience_[c] = held(CourseSound::effect_name(c));
    if (library_) {
        tunnel_[0] = held("tunnel1_l"), tunnel_[1] = held("tunnel1_r"), tunnel_[2] = held("tunnel2_l"), tunnel_[3] = held("tunnel2_r");
        heri_ = held("heri");
    }
}

RaceSound::~RaceSound() {
    stop();
    mixer_.set_reverb_depth(reverb_before_, reverb_before_);
}

bool RaceSound::add_car(const std::string& label) { return add_car(label, car_sound_files(vfs_, label)); }

bool RaceSound::add_car(db::SpecDb& db, const std::string& label) { return add_car(label, car_sound_files(db, label)); }

bool RaceSound::add_car(const std::string& label, const CarSoundFiles& files) {
    auto s = std::make_unique<CarSound>(vfs_, mixer_, &driver_, library_);
    s->set_mix(mix_);
    s->share_banks(banks_);
    const bool ok = s->load(files);
    if (!ok) LOG_WARN("Race sound: no engine sound for %s", label.c_str());
    cars_.push_back(std::move(s));
    contact_.push_back(0), contact_kind_.push_back(0);
    return ok;
}

bool RaceSound::add_car(int sound_id, int variant) {
    auto s = std::make_unique<CarSound>(vfs_, mixer_, &driver_, library_);
    s->set_mix(mix_);
    s->share_banks(banks_);
    const bool ok = s->load(sound_id, variant);
    cars_.push_back(std::move(s));
    contact_.push_back(0), contact_kind_.push_back(0);
    return ok;
}

void RaceSound::set_course(ByteSpan crss, std::shared_ptr<const course::Runway> road) {
    quiet_course();
    road_ = std::move(road);
    ground_ = road_ ? std::make_unique<sim::RoadGround>(*road_) : nullptr;
    course_.clear();
    if (crss.empty()) return;
    try {
        const gt3r::sound::CrssData data = gt3r::sound::CrssData::parse(crss);
        const auto distance = [this](const race::Vec3& p, float* d) { return ground_ && ground_->distance_along(p, *d); };
        course_.load(data, distance, road_ ? road_->length : 0.0f);
    } catch (const std::exception& e) {
        LOG_WARN("Race sound: the course's ambience: %s", e.what());
    }
}

void RaceSound::set_options(const RaceSoundOptions& o) {
    options_ = o;
    mix_->mode = o.mode;
}

void RaceSound::fade(float target, float seconds) {
    fade_target_ = target;
    fade_step_ = seconds > 0 ? std::fabs(target - fade_) / (seconds * 60.0f) : 1.0f;
    if (fade_step_ <= 0) fade_step_ = 1.0f;
}

RaceSound::Held RaceSound::held(const char* name) {
    Held h;
    const sound::EffectEntry* e = name ? library_->effects().find(name) : nullptr;
    if (!e || e->sequence) return h;
    h.bank = library_->effect_bank(e->bank);
    h.program = h.bank && h.bank->program(e->index) ? e->index : -1;
    return h;
}

void RaceSound::send(Held& h, float left, float right, float pitch) {
    if (!h.bank || h.program < 0) return;
    if (paused_ || (std::fabs(left) < kQuiet && std::fabs(right) < kQuiet)) {
        if (h.id) driver_.note_off(h.id), h.id = 0;
        return;
    }
    SoundParams p;
    p.left = left, p.right = right, p.pitch = pitch;
    p.priority = std::min(255, int(std::min(std::max(std::fabs(left), std::fabs(right)), 1.0f) * 255.0f));
    if (h.id && driver_.note_playing(h.id))
        driver_.set_note(h.id, p);
    else
        h.id = driver_.note_on(h.bank, h.program, p);
}

bool RaceSound::play_effect(const char* name, float level) {
    if (!library_ || level <= 0) return false;
    const sound::EffectEntry* e = library_->effects().find(name);
    if (!e) return false;
    const auto bank = library_->effect_bank(e->bank);
    if (!bank) return false;
    SoundParams p;
    p.left = p.right = level;
    if (e->sequence) return driver_.play_effect(bank, e->index, e->key, p) != 0;
    return driver_.note_on(bank, e->index, p) != 0;
}

float RaceSound::stream_level(bool racing) const {
    if (!(replay_ ? options_.replay_music_on : options_.music_on)) return 0;
    const float table = library_ ? library_->levels().level(int(options_.mode), sound::VolumeTable::Stream) : 1.0f;
    return racing ? table * float(replay_ ? options_.replay_music : options_.music) / 128.0f : table;
}

bool RaceSound::play_stream(const char* group, int number, bool racing) {
    if (!library_) return false;
    const int g = sound::stream_group(group);
    const std::string file = g >= 0 ? library_->stream_file(g, number) : std::string();
    if (file.empty()) return false;
    report_.music = file;
    const float level = stream_level(racing);
    if (!music_.play || level <= 0) return false;
    return music_.play(file, false, level);
}

void RaceSound::intro() { play_stream(race_start_group(kind_), 0, false); }

void RaceSound::count(int frames_left) {
    if (started_) return;
    if (frames_left <= 0) {
        start();
        return;
    }
    if (count_beep(frames_left) && play_effect("count_d", mix_->master)) ++report_.beeps;
}

// The next racing piece: the play list's, or the group's next.
bool RaceSound::play_racing() {
    racing_piece_ = playlist_ ? playlist_->next() : racing_piece_ + 1;
    if (racing_piece_ < 0) return false;
    if (first_piece_ < 0) first_piece_ = racing_piece_;
    return play_stream("racing", racing_piece_, true);
}

void RaceSound::start() {
    start_signal();
    rolling();
}

void RaceSound::start_signal() {
    if (started_) return;
    started_ = true;
    if (play_effect("count_s", mix_->master)) ++report_.starts;
}

void RaceSound::rolling() {
    if (racing_) return;
    racing_ = true;
    if (!play_racing() && !music_.play) racing_ = false;
}

void RaceSound::duck() { fade(designer_shot_ && replay_ ? 0.45f : free_run_duck_ ? 0.2f : 1.0f, 1.0f); }

void RaceSound::set_free_run_duck(bool on) {
    if (on == free_run_duck_) return;
    free_run_duck_ = on;
    duck();
}

void RaceSound::replay() {
    replay_ = true;
    started_ = true;
    // A replay's master rises from 0 over 3 s (0x391D98 in the race's modes 2 and 3).
    fade_ = 0;
    fade(1.0f, 3.0f);
    for (auto& c : cars_) c->stop();
    replay_step_ = 0;
    racing_ = false;
    if (music_.stop) music_.stop();
    if (first_piece_ >= 0) racing_ = play_stream("racing", first_piece_, true);
}

void RaceSound::finish() {
    racing_ = false;
    if (music_.stop) music_.stop();
    play_stream(race_goal_group(kind_), 0, false);
}

void RaceSound::contact(size_t car, int kind, float force) {
    if (car >= contact_.size()) return;
    const float strength = contact_strength(kind, force);
    if (strength <= 0 || strength < contact_[car]) return;
    contact_[car] = strength, contact_kind_[car] = kind;
}

void RaceSound::pause(bool paused) {
    if (paused == paused_) return;
    paused_ = paused;
    if (music_.pause) music_.pause(paused);
    if (paused) {
        for (auto& c : cars_) c->stop();
        quiet_course();
    }
}

void RaceSound::quiet_course() {
    const auto off = [&](Held& h) {
        if (h.id) driver_.note_off(h.id), h.id = 0;
    };
    for (Held& h : ambience_) off(h);
    for (Held& h : tunnel_) off(h);
    off(heri_);
}

void RaceSound::stop() {
    for (auto& c : cars_) c->stop();
    quiet_course();
    racing_ = false;
    if (music_.stop) music_.stop();
}

void RaceSound::update(std::span<const CarSoundState> states, const SoundListener& camera, int target) {
    if (paused_) return;
    // The master level. In a replay the duck is asked for every step (RaceBase virtual 75): the
    // step toward it is worked out again from where the master is.
    if (replay_) duck();
    fade_ = fade_ < fade_target_ ? std::min(fade_ + fade_step_, fade_target_) : std::max(fade_ - fade_step_, fade_target_);
    const float table = library_ ? library_->levels().level(int(options_.mode), sound::VolumeTable::Race) : 1.0f;
    const bool on = replay_ ? options_.replay_effects_on : options_.effects_on;
    const float option = !on ? 0.0f : mix_->tables.option_level ? float(replay_ ? options_.replay_effects : options_.effects) / 128.0f : 1.0f;
    mix_->master = option * fade_ * table;
    const float master = mix_->master;

    // The cars.
    std::vector<float> distances;
    distances.reserve(states.size());
    for (size_t i = 0; i < states.size() && i < cars_.size(); ++i) {
        CarSoundState s = states[i];
        // (the recorder keeps the step's strongest of every kind)
        if (contact_[i] > s.contact) s.contact = contact_[i], s.contact_kind = contact_kind_[i];
        contact_[i] = 0;
        cars_[i]->update(s, camera, int(i) == target);
        distances.push_back(s.course_distance);
    }

    // The course.
    CourseSound::Listener at;
    at.position = camera.position, at.velocity = camera.velocity, at.right = camera.right;
    if (ground_) {
        sim::GroundHit hit;
        if (ground_->under(camera.position, hit)) at.road_flags = hit.flags;
    }
    course_.update({at}, distances);
    for (int c = 0; c < CourseSound::kChannels; ++c) {
        const CourseSound::Out& o = course_.channel(c);
        const float l = std::min(o.left * master, 1.0f), r = std::min(o.right * master, 1.0f);
        report_.ambience[c] = std::max(l, r);
        send(ambience_[c], l, r, o.pitch);
    }
    const float t1 = course_.tunnel1() * master, t2 = course_.tunnel2() * master;
    send(tunnel_[0], t1, 0, 1), send(tunnel_[1], 0, t1, 1);
    send(tunnel_[2], 0, t2, 1), send(tunnel_[3], t2, 0, 1); // (the second pair's halves cross)
    send(heri_, helicopter_ ? 1.0f : 0.0f, helicopter_ ? 1.0f : 0.0f, 1);
    report_.tunnel1 = t1, report_.tunnel2 = t2, report_.reverb = course_.reverb();
    const s16 depth = s16(course_.reverb() * 32767.0f);
    mixer_.set_reverb_depth(depth, depth);

    // The racing music: the playlist's next piece when one ends.
    if (racing_ && music_.playing && !music_.playing()) play_racing();
}

void RaceSound::update(const sim::Session& session, const SoundListener& camera, int target) {
    std::vector<CarSoundState> s;
    s.reserve(session.cars().size());
    for (size_t i = 0; i < session.cars().size(); ++i) s.push_back(sound_state(session, i, &mix_->tables));
    if (!replay_ && recorded_.size() < kRecordSteps) recorded_.push_back(s);
    update(s, camera, target);
}

void RaceSound::update(std::span<const race::CarState> cars, const SoundListener& camera, int target) {
    // A replay of the run just recorded: each step's sound inputs as the session gave them (its contacts, landings,
    // wheels, the engine's torques), in step with the recording.
    if (replay_ && replay_step_ + 1 < recorded_.size() && recorded_[replay_step_ + 1].size() == cars.size()) {
        ++replay_step_;
        const std::vector<CarSoundState> s = recorded_[replay_step_];
        update(s, camera, target);
        return;
    }
    if (replay_) ++replay_step_;
    std::vector<CarSoundState> s;
    s.reserve(cars.size());
    for (const race::CarState& c : cars) s.push_back(sound_state(c));
    update(s, camera, target);
}

} // namespace gt4r::audio
