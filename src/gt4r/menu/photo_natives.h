#pragma once

// The photo mode's widgets as the scripts see them (MPhotoMapWindow 0x1965A0, MPhotoRenderFace
// 0x1991D8) and the shoot behind them (docs/formats/photo.md). The app draws what is here
// (app/screen_photo.cpp); the photos on the card are game_photos.cpp.

#include "gt4r/menu/runtime.h"
#include "gt4r/photo/photo_camera.h"
#include "gt4r/photo/picture.h"

#include <memory>
#include <optional>
#include <vector>

namespace gt4r::menu {

// A picture a PhotoRenderFace shows: the photo just taken, or one of the card's.
struct PhotoPicture {
    int width = 0, height = 0; // upright
    std::vector<u32> rgba;     // R, G, B, A bytes in memory order
    Bytes jpeg;                // the file of it, when it is one
    std::optional<JpegPicture::Focus> focus;
    u64 version = 0;           // changes with the picture
    float ofs_x = 0, ofs_y = 0, scale = 1; // the page's move and zoom
};

// A photo shoot: what the photo page's scripts set, beside the camera.
struct PhotoShoot {
    PhotoShoot(const photo::PhotoCourse& course, photo::PhotoCamera::Ground ground) : camera(course, std::move(ground)) {}
    photo::PhotoCamera camera;
    photo::Mode mode = photo::Mode::None, last_mode = photo::Mode::None;
    float view_angle = 70, aperture = 7.5f; // the lens as the page set it: mm, f-number
    bool stand = false;                     // the camera held upright (portrait)
    float wheel_angle = 0;                  // degrees, as the steering dialog has it
    photo::Filters filters;
    float blink = 0;     // the map's blink (0x194470: 0 .. 1, falling 0.03 a picture)
    bool window_set = false, sent_disabled = false;
    float zoom_seen = 0; // the zoom slider's value last seen
    bool rendering = false;   // the photo is being taken (the preview page waits)
    float depth_at_middle = 0; // what the finder's middle shows, metres (the manual focus's `doFocus`); 0: not known
    // What the app is to do: the shutter's photo (the preview page opened), or its end.
    bool take = false;
    std::string course, car_label; // the location's course file, the car's label
    photo::Correction correction() const { return photo::correction(filters); }
    photo::Aspect aspect; // the option's
    std::vector<float> apertures; // the program's f-numbers (0x620E70), 16
};

// A member of a PhotoMapWindow or PhotoRenderFace; null for another name.
std::shared_ptr<script::Reference> photo_member(Runtime& rt, const std::shared_ptr<LiveWidget>& w, const std::string& name);

// After a shoot: its day in the diary (0x1C79A8: a course run of kind 5, no result, no time).
void photo_day(Runtime& rt, const std::string& course);
// The program's f-numbers (16), empty when its tables are not known.
std::vector<float> photo_apertures(Runtime& rt);
// One picture of the photo page's map window (slot 70, 0x194470): the pad to the mode the scripts set, the lens and
// filters to the shoot, the map's marks placed. The page shown must be the photo page.
void step_photo_map(Runtime& rt, PhotoShoot& shoot);
// The map window of the pages shown, null when none is.
std::shared_ptr<LiveWidget> photo_map_window(Runtime& rt);
// The PhotoRenderFace of the pages shown.
std::shared_ptr<LiveWidget> photo_render_face(Runtime& rt);
// The picture of an MMemoryCardFile whose load read one (game_photos.cpp) given to the photo faces.
void photo_file_picture(Runtime& rt, Object& file);
// The shoot's lens and filters as the page has them now (also without its map: 0x194470's end).
void apply_photo_lens(PhotoShoot& shoot);

} // namespace gt4r::menu
