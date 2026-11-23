#pragma once

// A menu project's layout (.mproject, and the widget files .mwidget): text, a tree of widgets
// with their properties; docs/formats/menu.md.
//
//   RootWindow {
//     name string {"LanguageRoot"}
//     children [2] {
//       ImageFace { name string {"bg"}  image_path string {"image/boot/lang_bg_4.png"} }
//       TextFace { geometry rectangle {0 0 640 56}  text_color RGBA {154 154 161 255} ... }
//     }
//   }

#include "gt4r/base/types.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt4r::menu {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};
struct Rgba {
    u8 r = 255, g = 255, b = 255, a = 255;
};

struct Widget;

// A property's value: `digit {1}`, `rectangle {0 0 640 56}`, `string {"bg"}`, or a list
// (`color [1] { RGBA {0 0 0 255} }`, `children [2] { ... }`).
struct Property {
    std::string name;
    std::string type;                // "digit", "string", "rectangle", "RGBA", ...; empty for a list
    std::vector<std::string> values; // a value's items as written (a string without its quotes)
    std::vector<Property> items;     // a list's values
    std::vector<Widget> widgets;     // a list's widgets
    bool is_list = false;
};

struct Widget {
    std::string type; // "RootWindow", "TextFace"
    std::vector<Property> properties;

    const Property* find(std::string_view name) const;
    std::string name() const { return text("name"); }
    std::string text(std::string_view property, std::string_view fallback = {}) const;
    float number(std::string_view property, float fallback = 0) const;
    bool has(std::string_view property) const { return find(property) != nullptr; }
    std::optional<Rect> rect(std::string_view property) const;
    std::optional<Rgba> color(std::string_view property) const; // an RGBA value
    std::vector<Rgba> colors(std::string_view property) const;  // a list of them (a face's "color")
    const std::vector<Widget>& children() const;
    // A child by name; a path's parts by "/" or "::" ("Title/label").
    const Widget* child(std::string_view path) const;
    size_t count() const; // itself and every widget below
};

// Throws FormatError.
Widget parse_layout(std::string_view text);
inline Widget parse_layout(ByteSpan file) { return parse_layout(std::string_view(reinterpret_cast<const char*>(file.data()), file.size())); }

} // namespace gt4r::menu
