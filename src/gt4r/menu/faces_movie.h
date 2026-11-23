#pragma once

// A movie face (the program's MMovieFace): what it plays and when it tells the scripts. The
// player itself is the app's (app/menu_movies.*); docs/formats/menu.md, "Movies".

#include "gt4r/menu/layout.h"

#include <string>

namespace gt4r::menu {

// The face's settings as its layout and the scripts give them, with the program's defaults for
// what they leave out (the settings' constructor at 0x220730 of the US program, read at 0x220A38).
struct MovieFaceSettings {
    std::string movie_name;  // a file of mpeg/<root>/ without ".pss"; "" : nothing plays
    int pixel_width = 640;
    int pixel_height = 0;
    bool interlace = true;
    bool audio = false;  // its sound played (else silent)
    bool repeat = true;  // starts again at its end (else the scripts' onMovieFinish is called)
    bool ac3 = false;
    int audio_track = 0;
};
MovieFaceSettings movie_face_settings(const Widget& face);

// The movie's file: "mpeg/<root>/<name>.pss". `root` is the configuration's MpegRoot ("main",
// "main_pal" for the UK), "preview" when the configuration has none.
std::string movie_path(const std::string& root, const std::string& name);

// The still the program has for a movie in mpeg/<root>/course.ipic (MMovieFace::loadIpic("course")).
std::string movie_stills_path(const std::string& root);

// What a movie face's per-picture update tells the scripts (the routine at 0x21F440):
// `hook` when the frame shown is the one set by setFrameHook (then the hook is cleared), and
// `finished` once, when a movie that does not repeat has ended.
struct MovieFaceEvents {
    bool hook = false;
    bool finished = false;
};
// `frame`: the picture shown, counted from 0; `hook_frame`: -1 when none is set;
// `finish_told`: onMovieFinish already sent for this playing.
MovieFaceEvents movie_face_events(const MovieFaceSettings& s, long frame, long hook_frame, bool ended, bool finish_told);

} // namespace gt4r::menu
