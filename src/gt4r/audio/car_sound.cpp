#include "gt4r/audio/car_sound.h"

#include "base/log.h"
#include "gt4r/formats/db/specdb.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt4r::audio {

namespace {

constexpr const char* kWindBank = "sound/roadnoiz.es";
constexpr const char* kSqueal = "on_skill";
constexpr size_t kEngineSoundId = 0x18, kMufflerVariant = 6, kTurboKind = 9;

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

CarSoundFiles car_sound_files(db::SpecDb& db, const std::string& car) {
    CarSoundFiles f;
    const db::Table* engines = db.table("ENGINE");
    const std::optional<u32> engine = part(db, car, "ENGINE", "en_");
    if (!engines || !engine) return f;
    const ByteSpan row = engines->row(*engine);
    if (row.size() < kEngineSoundId + 2) return f;
    f.id = rd16(row, kEngineSoundId);
    if (const db::Table* t = db.table("MUFFLER"))
        if (const auto id = part(db, car, "MUFFLER", "mu_")) {
            const ByteSpan r = t->row(*id);
            if (r.size() > kMufflerVariant) f.muffler = r[kMufflerVariant] & 3;
        }
    if (const db::Table* t = db.table("TURBINEKIT"))
        if (const auto id = part(db, car, "TURBINEKIT", "tk_")) {
            const ByteSpan r = t->row(*id);
            f.turbo = r.size() > kTurboKind && r[kTurboKind] != 0;
        }
    f.variant = gt3r::EngineSound::variant_dir(f.variant_index());
    char name[16];
    std::snprintf(name, sizeof name, "%05d", f.id);
    f.engine = std::string("sound/engine/") + name;
    f.exhaust = "sound/" + f.variant + "/" + name;
    return f;
}

CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const std::string& car) {
    try {
        db::SpecDb db([&vfs](const std::string& path) { return vfs.read(path); }, [&vfs](const std::string& dir) { return vfs.list_dir(dir); });
        return car_sound_files(db, car);
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

CarSound::CarSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver* effects, std::shared_ptr<SoundLibrary> library)
    : vfs_(vfs), mixer_(mixer), effects_(effects), library_(std::move(library)) {}

CarSound::~CarSound() { stop(); }

bool CarSound::load(const std::string& car_label) {
    files_ = car_sound_files(vfs_, car_label);
    if (files_.engine.empty()) return false;
    return load(files_.id, files_.variant_index());
}

bool CarSound::load(int sound_id, int variant) {
    stop();
    files_.id = sound_id;
    files_.muffler = variant & 3;
    files_.turbo = (variant & 4) != 0;
    files_.variant = gt3r::EngineSound::variant_dir(variant);
    char name[16];
    std::snprintf(name, sizeof name, "%05d", sound_id);
    files_.engine = std::string("sound/engine/") + name;
    files_.exhaust = "sound/" + files_.variant + "/" + name;
    const auto bank = [&](const std::string& path) -> std::shared_ptr<const gt3r::sound::EngineBank> {
        try {
            if (const auto d = vfs_.read(path)) return std::make_shared<const gt3r::sound::EngineBank>(parse_engine_bank(*d));
        } catch (const std::exception& e) {
            LOG_WARN("%s: %s", path.c_str(), e.what());
        }
        return nullptr;
    };
    engine_.set_banks(bank(files_.engine), bank(files_.exhaust));
    if (!wind_tried_) {
        wind_tried_ = true;
        if (const auto w = bank(kWindBank)) wind_.set_bank(w);
    }
    return engine_.loaded();
}

CarSoundInput CarSound::input_from(const race::CarState& car, const race::CarState* listener) {
    CarSoundInput in;
    in.rpm = car.engine_rpm;
    in.throttle = car.throttle;
    in.speed = std::fabs(car.speed);
    for (const race::WheelState& w : car.wheels)
        if (w.on_ground) in.slip = std::max(in.slip, w.slip);
    if (listener && listener != &car) {
        const race::Vec3 d{car.position.x - listener->position.x, car.position.y - listener->position.y, car.position.z - listener->position.z};
        in.x = d.x * listener->right.x + d.y * listener->right.y + d.z * listener->right.z;
        in.z = d.x * listener->forward.x + d.y * listener->forward.y + d.z * listener->forward.z;
        // Doppler: (c + listener's speed toward the source) / (c + source's speed away from it).
        const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist > 0.01f) {
            constexpr float c = 331.45f;
            const float toward = (listener->velocity.x * d.x + listener->velocity.y * d.y + listener->velocity.z * d.z) / dist;
            const float away = (car.velocity.x * d.x + car.velocity.y * d.y + car.velocity.z * d.z) / dist;
            in.doppler = std::clamp((c + toward) / (c + away), 0.5f, 2.0f);
        }
    }
    return in;
}

void CarSound::update(const race::CarState& car, const race::CarState* listener) { update(input_from(car, listener)); }

void CarSound::update(const CarSoundInput& in) {
    if (!engine_.loaded()) return;
    const bool placed = in.x != 0 || in.z != 0;
    if (placed)
        engine_.set_position(in.x, in.z);
    else
        engine_.set_pan(0);
    engine_.update(mixer_, in.rpm, in.throttle, 1.0 / SoundDriver::kUpdateRate, in.doppler);
    const float distance = placed ? std::max(std::sqrt(in.x * in.x + in.z * in.z), 0.25f) : 0.0f;
    const float near = distance > 32.0f ? 32.0f / distance : 1.0f;
    if (wind_.loaded()) {
        const gt3r::car_sound::Wind w = gt3r::car_sound::wind(in.speed, 0.0f, in.drag);
        wind_.update(mixer_, float(w.rpm), w.gain * near, placed ? gt3r::EngineSound::pan_for_offset(in.x, in.z) : 0.0f, in.doppler);
    }
    if (effects_ && library_) {
        const float gain = std::clamp(in.slip, 0.0f, 1.0f) * near;
        if (gain > 0.02f) {
            SoundParams p;
            const float pan = placed ? gt3r::EngineSound::pan_for_offset(in.x, in.z) : 0.0f;
            const float angle = std::clamp((pan + 1.0f) * 0.78539816f, 0.0f, 1.5707963f);
            p.left = gain * std::cos(angle) * 1.41421356f;
            p.right = gain * std::sin(angle) * 1.41421356f;
            p.left = std::min(p.left, 1.0f);
            p.right = std::min(p.right, 1.0f);
            p.pitch = in.doppler;
            if (squeal_ && effects_->note_playing(squeal_)) {
                effects_->set_note(squeal_, p);
            } else if (const sound::EffectEntry* e = library_->effects().find(kSqueal); e && !e->sequence) {
                if (const auto bank = library_->effect_bank(e->bank)) squeal_ = effects_->note_on(bank, e->index, p);
            }
        } else if (squeal_) {
            effects_->note_off(squeal_);
            squeal_ = 0;
        }
    }
}

void CarSound::stop() {
    engine_.stop(mixer_);
    wind_.stop(mixer_);
    if (squeal_ && effects_) effects_->note_off(squeal_);
    squeal_ = 0;
}

} // namespace gt4r::audio
