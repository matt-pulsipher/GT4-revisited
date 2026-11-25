#pragma once

// A save's icon file (icon0.ico, the console's icon format): the picture a replay's or a photo's icon carries, which is the
// top left 64 x 64 of its 128 x 128 texture (docs/formats/save.md "The icon's picture").

#include "gt4r/base/types.h"

#include <optional>
#include <vector>

namespace gt4r::save {

inline constexpr int kIconPicture = 64;

// The 64 x 64 pixels as RGBA8 (opaque, as the program makes them: 0x176FF0); none when the file is no such icon.
std::optional<std::vector<u32>> icon_picture(ByteSpan icon);

} // namespace gt4r::save
