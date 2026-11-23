#pragma once

// A car's sounds in a race: the engine and exhaust notes, the wind, and the tires' squeal.
//
// The engine and exhaust are two "ENGN" banks played by the shared engine's engine-sound model
// (audio/engine_sound.h: layers recorded at set revs, crossfaded and pitched by the revs), as
// in Gran Turismo 3. Which banks a car plays comes from the car database as the game reads it:
//   sound/engine/<id>           id: the car's ENGINE row, u16 at +0x18
//   sound/<variant>/<id>        variant: "normal" or "turbo" (the car's TURBINEKIT row has a
//                               non-zero byte at +9) and the MUFFLER row's byte at +6 (0..3)
// The wind is the bank sound/roadnoiz.es on a third layer player. The tire squeal is the held
// note "on_skill" of the effect index (sound/gt4race.ins). The levels and curves of the wind
// and the squeal are Gran Turismo 3's and not yet checked against this game's program.

#include "audio/car_sound_model.h"
#include "audio/engine_sound.h"
#include "gt4r/audio/sound_driver.h"
#include "gt4r/audio/sound_library.h"
#include "gt4r/race/simulation.h"

#include <memory>
#include <string>

namespace gt4r::db {
class SpecDb;
}

namespace gt4r::audio {

// The sound files of a car as sold.
struct CarSoundFiles {
    int id = 0;          // the engine sound's number
    int muffler = 0;     // 0..3
    bool turbo = false;
    std::string variant; // "normal0" .. "turbo3"
    std::string engine;  // "sound/engine/00123"; empty when the car is not in the database
    std::string exhaust; // "sound/normal0/00123"
    int variant_index() const { return muffler + (turbo ? 4 : 0); }
};
CarSoundFiles car_sound_files(db::SpecDb& db, const std::string& car_label);
// The same, opening the database of the disc behind `vfs`; empty when it has none.
CarSoundFiles car_sound_files(gt3r::Vfs& vfs, const std::string& car_label);

// An "ENGN" bank. The shared engine's reader, and for the few banks whose layers are not
// stored in the order of the table, each layer's size from the next sample in the data.
gt3r::sound::EngineBank parse_engine_bank(ByteSpan file);

// What the sounds follow, one step of 1/60 s.
struct CarSoundInput {
    float rpm = 0;
    float throttle = 0; // 0..1
    float speed = 0;    // metres per second
    float slip = 0;     // 0 gripping .. 1 sliding: the strongest of the wheels on the ground
    // The car in the listener's space, metres: x to the right, z ahead. The player's own car
    // heard from its own camera: 0, 0.
    float x = 0, z = 0;
    float doppler = 1.0f;
    float drag = 0.3f; // the body's drag coefficient (wind)
};

class CarSound {
public:
    // `effects` and `library`: for the tire squeal; without them only the engine, the exhaust
    // and the wind sound.
    CarSound(gt3r::Vfs& vfs, gt3r::VoiceMixer& mixer, SoundDriver* effects = nullptr, std::shared_ptr<SoundLibrary> library = nullptr);
    ~CarSound();

    // The banks of a car of the database, or by number. False when neither bank reads.
    bool load(const std::string& car_label);
    bool load(int sound_id, int variant);
    bool loaded() const { return engine_.loaded(); }
    const CarSoundFiles& files() const { return files_; }

    void set_view(gt3r::EngineSound::View view) { engine_.set_view(view); }
    void set_volume(float v) { engine_.set_volume(v); }

    // One step: 1/60 s.
    void update(const CarSoundInput& in);
    // The same from a simulated car, heard from `listener` (the camera's car; null or the car
    // itself: from its own place).
    void update(const race::CarState& car, const race::CarState* listener = nullptr);
    static CarSoundInput input_from(const race::CarState& car, const race::CarState* listener);
    void stop();

    const gt3r::EngineSound& engine() const { return engine_; }

private:
    gt3r::Vfs& vfs_;
    gt3r::VoiceMixer& mixer_;
    SoundDriver* effects_;
    std::shared_ptr<SoundLibrary> library_;
    CarSoundFiles files_;
    gt3r::EngineSound engine_;
    gt3r::EngineUnit wind_;
    bool wind_tried_ = false;
    u32 squeal_ = 0;
};

} // namespace gt4r::audio
