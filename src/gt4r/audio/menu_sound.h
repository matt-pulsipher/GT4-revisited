#pragma once

// The sound object of the menu scripts (`menu::MSound`, the scripts' `main::sound`): the
// calls a script makes, played through the sound driver.
//
//   sound.play("ok")                          play(name)
//   sound.load("ARCADE") / sound.unload()     load(group) / unload()
//   sound.startMusic(g) / keepMusic(g)        start_music(g) / keep_music(g)
//   sound.stopMusic()                         stop_music()
//   sound.fadeOutMusic(seconds)               fade_out_music(seconds)
//   sound.fadeInMusic(g, seconds)             fade_in_music(g, seconds)
//   sound.startStream(kind, code)             start_stream(kind, code)
//   sound.startStreamEx(kind, code, n, r, v)  start_stream(kind, code, n, r, v)
//   sound.stopStream() / pauseStream(p)       stop_stream() / pause_stream(p)
//   sound.isPlayingStream()                   is_playing_stream()
//
// What each does follows the game (docs/formats/sound.md "The menus' sound object"). An
// effect's name is looked up in sound/gt4se.inf; a music group is one of the sequenced
// music's (any case: "ARCADE" is music/<region>/arcade.ins and .sqt); a stream's kind is
// "pcm" (a group of the streamed music, by the game's name for it) or "narration".
//
// The driver is the caller's: the app updates it from the mixer (AppSound, app_sound.h); the
// tools and tests step it by hand (render.h). Streams need a StreamPlayer; without one the
// stream calls only answer where the stream is.

#include "gt4r/audio/sound_driver.h"
#include "gt4r/audio/sound_library.h"

#include <functional>
#include <memory>
#include <string>

namespace gt4r::audio {

// What plays a music stream for MenuSound (the app's is the shared engine's AudioEngine).
struct StreamPlayer {
    std::function<bool(const std::string& path, bool repeat, float level)> play;
    std::function<void()> stop;
    std::function<void(bool paused)> pause;
    std::function<bool()> playing;
    // Ours: one of the player's own songs (a host path: an MP3, FLAC, WAV or Ogg file, played once), its samples × `trim`
    // (its loudness gain, your_music.h) at `level`; and the playing song's trim changed while it plays.
    std::function<bool(const std::string& path, float level, float trim)> play_file;
    std::function<void(float trim)> set_trim;
};

class MenuSound {
public:
    // `library`: shared with the race's sounds when given, else its own over `vfs`.
    MenuSound(gt3r::Vfs& vfs, SoundDriver& driver, std::string region = {}, StreamPlayer streams = {});
    MenuSound(std::shared_ptr<SoundLibrary> library, SoundDriver& driver, StreamPlayer streams = {});

    SoundLibrary& library() { return *library_; }

    // --- effects ---
    // Plays the effect of that name; false when the index has none or its bank is missing.
    bool play(const std::string& name);

    // --- sequenced music ---
    // Readies the sequenced music and, with a group's name, reads that group's bank ahead
    // of its first note. unload() stops the music.
    void load(const std::string& group = "midi");
    void unload();
    // Stops what plays and starts the group's next piece (each start moves on to the group's
    // next piece; a group of one repeats it). False when there is no such group.
    bool start_music(const std::string& group);
    // Starts the group unless it is the one playing ("keep": leaves the music as it is).
    bool keep_music(const std::string& group);
    void stop_music();
    void fade_out_music(float seconds = 1.0f);
    bool fade_in_music(const std::string& group, float seconds = 1.0f);
    // The group whose music plays ("" for none) and the piece's number in it.
    std::string music_group() const;
    int music_number() const;

    // --- streams ---
    // `number`: the piece of the group, -1 for one drawn at random; adding 0x1000 asks for the
    // piece's short version (the "i_" file).
    bool start_stream(const std::string& kind, const std::string& code, int number = -1, bool repeat = false, float volume = 1.0f);
    void stop_stream();
    void pause_stream(bool pause);
    bool is_playing_stream() const;
    // The file start_stream() would play (for the random piece: the one it would draw next).
    std::string stream_file(const std::string& kind, const std::string& code, int number = -1) const;

    // The game's volume options, 0..1: the music's and the effects' (at start a new game's: 115 of 128,
    // which is what the original's menus measure at).
    void set_music_option(float v);
    void set_effect_option(float v);

private:
    bool start(int group, int number, bool restart);
    void stop(bool remember);
    int draw(int count) const;

    std::shared_ptr<SoundLibrary> library_;
    SoundDriver& driver_;
    StreamPlayer streams_;
    int group_ = -1;          // the group whose bank is in use
    std::vector<int> next_;   // per group: the piece its next start plays
    float music_volume_ = 1.0f; // the scripts' music volume (MMusic.volume)
    float music_option_ = 115.0f / 128.0f, effect_option_ = 115.0f / 128.0f;
    mutable u32 random_ = 0x2545F491;
};

} // namespace gt4r::audio
