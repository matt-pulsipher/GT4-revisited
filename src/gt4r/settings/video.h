#pragma once

// The display's settings ([video] in config.ini): what is read at the start and what the Settings page changes. Pure (no window):
// the app applies them (app/app.cpp).
//
//   [video]
//   fullscreen = off         ; on | off (F11 changes it too)
//   aspect = fill            ; fill | 16:9 | 4:3 (a config without it, or with another word: fill)
//   vsync = on               ; on | off
//   limit_60fps = off        ; on: displays faster than 60 Hz get about 60 frames a second
//   show_fps = off           ; on: the frames drawn a second in the top left corner
//   msaa = 4                 ; 1 | 2 | 4 | 8 (phones and tablets: 2)
//   anisotropy = 4           ; 1 | 2 | 4 | 8 | 16
//   render_scale = auto      ; auto | 0.5 .. 4.0, the 3D views' size beside the screen's
//   dynamic_resolution = off ; on: lowered to keep the frame rate (phones and tablets: on)
//   gamma = 1.00             ; 0.50 .. 2.00, above 1 lighter
//   menu_models = screen     ; screen | console (640x448): the cars and prizes in the menus
//   gpu = auto               ; auto | gles | gles_driver | off (the 2D pages only); taken at the start
//   texture_packs = on       ; off: the game's own textures

#include "platform/aspect.h"
#include "platform/render_scale.h"

#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class IniDocument;
}

namespace gt4r::settings {

enum class Gpu { Automatic, Gles, GlesDriver, Off };

struct Video {
    bool fullscreen = false, vsync = true, limit_60fps = false, show_fps = false;
    bool dynamic_resolution = gt3r::platform::is_mobile(), sharp_menu_models = true, texture_packs = true;
    int msaa = gt3r::platform::default_msaa(gt3r::platform::is_mobile()), anisotropy = 4;
    std::optional<float> render_scale; // none: automatic
    float gamma = 1.0f;
    gt3r::platform::Aspect aspect = gt3r::platform::Aspect::Fill; // (16:9 and 4:3 are the player's choices)
    Gpu gpu = Gpu::Automatic;

    bool operator==(const Video&) const = default;
    static Video load(const gt3r::IniDocument& config);
    // Writes what differs from what the document holds (the rest stays as it is, also where it is not written).
    void save(gt3r::IniDocument& config) const;
};

// The game's picture in a window of pw x ph pixels: its width in UI units (360 tall, even). 640 at 16:9 and 480 at 4:3, letterboxed;
// with fill the window's shape up to 4:1. `view`: a race's or replay's picture, which fill takes down to 1:1 (the 3D view and the
// display fill a screen squarer than 4:3); else (the menus' pages) never narrower than 4:3, with bars above and below.
int picture_width(gt3r::platform::Aspect a, int pw, int ph, bool view);
constexpr int kNarrowestView = 360; // 1:1

// The frames a second to draw on a display of `hz` with the limit: every second, third ... refresh, as near 60 as that comes from
// above (144 Hz: 72, 120: 60, 90: 90). 0: every refresh.
double limited_rate(double hz, bool limit);

// The 3D resolution's choices on a screen: 50% to what it allows (`limit`, platform::supersample_limit), the automatic one at its
// own place among them. `auto_top`: how far up dynamic resolution may take the automatic one.
struct Resolution {
    std::optional<float> scale; // none: automatic
    std::string name;           // "75%", "Automatic (100%)"
};
std::vector<Resolution> resolutions(float auto_scale, float auto_top, float limit);
// The choice nearest a configured scale.
size_t resolution_index(const std::vector<Resolution>& list, std::optional<float> scale);

std::string percent(float v);

} // namespace gt4r::settings
