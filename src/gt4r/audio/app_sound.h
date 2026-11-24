#pragma once

// The game's sound in the app: a sound driver on the app's voice mixer, updated 60 times a
// second from the mixer itself (so its timing is exact to the sample), with the menus' sound
// object on it. One per open disc; made after the AudioEngine and gone before it.
//
//   audio::AppSound sound(app.audio(), *app.vfs());
//   sound.menu().play("ok");            // the scripts' calls: menu_sound.h
//   audio::CarSound car(vfs, app.audio().voices(), &sound.driver(), sound.library_ptr());
//   audio::RaceSound race(vfs, app.audio().voices(), sound.driver(), sound.library_ptr(), tables, sound.streams());
//
// While it lives the app's voice mixer plays at the game's output level (sound_driver.h).

#include "gt4r/audio/menu_sound.h"

#include <memory>
#include <string>

namespace gt3r {
class AudioEngine;
class Vfs;
} // namespace gt3r

namespace gt4r::audio {

// The app's music streams for the game's sound objects (MenuSound, RaceSound): the shared engine's
// AudioEngine at the game's output level (sound_driver.h, stream_request).
StreamPlayer stream_player(gt3r::AudioEngine& audio, gt3r::Vfs& vfs);

class AppSound {
public:
    AppSound(gt3r::AudioEngine& audio, gt3r::Vfs& vfs);
    ~AppSound();
    AppSound(const AppSound&) = delete;
    AppSound& operator=(const AppSound&) = delete;

    SoundDriver& driver() { return driver_; }
    MenuSound& menu() { return menu_; }
    SoundLibrary& library() { return *library_; }
    std::shared_ptr<SoundLibrary> library_ptr() { return library_; }
    // The music streams (stream_player() on the app's engine and disc).
    StreamPlayer streams() { return stream_player(audio_, vfs_); }

    // For the asset browser: plays what a sound file holds and says what that is (", sequence
    // 2 of 5, ..."). A sequence bank (.sqt, or its .ins) plays a sequence, the next one each
    // time it is asked again; an effect bank plays its effect sequences and then its programs'
    // notes in turn. Empty when the file is not one of these.
    std::string preview(const std::string& path);
    // Stops the music, the effects and the notes.
    void stop();

private:
    gt3r::AudioEngine& audio_;
    gt3r::Vfs& vfs_;
    SoundDriver driver_;
    std::shared_ptr<SoundLibrary> library_;
    MenuSound menu_;
    std::string preview_path_;
    int preview_next_ = 0;
    u32 preview_note_ = 0;
};

} // namespace gt4r::audio
