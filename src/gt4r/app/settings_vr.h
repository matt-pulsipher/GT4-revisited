#pragma once

// Settings > Display in a VR headset's build (settings_vr.cpp): the headset's rows ([vr] in config.ini, the engine's
// xr::Settings) at the top, and the rows of a screen it has no use for shown as the headset's.

#include "gt4r/app/app.h"
#include "gt4r/app/page.h"

namespace gt4r {

#if GT3R_XR
void vr_display_rows(App& app, PageRows& rows);
#else
inline void vr_display_rows(App&, PageRows&) {}
#endif

} // namespace gt4r
