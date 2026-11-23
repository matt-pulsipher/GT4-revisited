#include "gt4r/menu/faces_movie.h"

namespace gt4r::menu {

MovieFaceSettings movie_face_settings(const Widget& face) {
    MovieFaceSettings s;
    s.movie_name = face.text("movie_name");
    s.pixel_width = int(face.number("pixel_width", float(s.pixel_width)));
    s.pixel_height = int(face.number("pixel_height", float(s.pixel_height)));
    s.interlace = face.number("interlace", s.interlace ? 1.0f : 0.0f) != 0;
    s.audio = face.number("audio", s.audio ? 1.0f : 0.0f) != 0;
    s.repeat = face.number("repeat", s.repeat ? 1.0f : 0.0f) != 0;
    // The scripts write "AC3"; the layout's name is "ac3".
    s.ac3 = face.number("AC3", face.number("ac3", 0)) != 0;
    s.audio_track = int(face.number("audio_track", 0));
    return s;
}

std::string movie_path(const std::string& root, const std::string& name) { return "mpeg/" + (root.empty() ? std::string("preview") : root) + "/" + name + ".pss"; }

std::string movie_stills_path(const std::string& root) { return "mpeg/" + (root.empty() ? std::string("preview") : root) + "/course.ipic"; }

MovieFaceEvents movie_face_events(const MovieFaceSettings& s, long frame, long hook_frame, bool ended, bool finish_told) {
    MovieFaceEvents e;
    e.hook = hook_frame >= 0 && frame == hook_frame;
    e.finished = !s.repeat && ended && !finish_told;
    return e;
}

} // namespace gt4r::menu
