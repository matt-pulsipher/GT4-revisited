#pragma once

// The game's fonts (font/*.fnt, "FT01"): Gran Turismo 3's font file with wider glyph records
// and one glyph picture shared by a file's fonts; docs/formats/menu.md. They are given as the
// shared engine's font structures, so its text layout lays them out.

#include "formats/text/fnt.h"
#include "gt4r/base/types.h"

namespace gt4r::menu {

// Throws FormatError. Fonts: "hm_16", "hb_24", "hc_20", "hn_14", "tr_30", ...
gt3r::text::FontFile read_fonts(ByteSpan file);

} // namespace gt4r::menu
