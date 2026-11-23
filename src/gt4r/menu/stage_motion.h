#pragma once

// The car-selection stage's motions (menu/select/camera.mot, light.mot): a Mot1 file whose
// scene is a byte program (the motion library's interpreter, 0x42B440) naming a camera, a
// perspective, lights and named values, each value a constant or a channel of the clip
// (fn 0x42D688). docs/formats/menu.md "The car-selection stage".

#include "gt4r/base/types.h"
#include "gt4r/race/start_camera.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

struct StageMotion {
    // A value of the program: a constant, or a channel of the clip (`channel` >= 0).
    struct Value {
        float constant = 0;
        int channel = -1;
    };
    struct Light {
        std::array<Value, 3> position{};
        std::array<Value, 4> ambient{}, diffuse{}, specular{}; // opcodes 0x0C, 0x0D, 0x0E
        bool has_position = false;
    };
    std::optional<std::array<Value, 9>> camera; // eye, point looked at, up (opcode 0x0B)
    std::optional<Value> fov;                   // radians (opcode 0x0A)
    std::map<int, Light> lights;                // by index (opcodes 0x0C to 0x0F)
    std::map<std::string, Value> named;         // opcode 0x10: "Alpha0", "BlackLevel", ...
    race::CameraMotion motion;                  // the clips and their channels

    static bool is_motion(ByteSpan d) { return race::CameraMotion::is_motion(d); }
    // Throws FormatError.
    static StageMotion parse(ByteSpan file);

    // The first clip's length in seconds (0 without one).
    float length() const;
    // A value at time t of the first clip.
    float at(const Value& v, float t) const;
    // A named value at t; `otherwise` when the program has none of that name.
    float named_at(const std::string& name, float t, float otherwise) const;
};

} // namespace gt4r::menu
