#pragma once

// A model stream of Prologue (mpeg/prologue/<name>.lmv, the program's MModelStream): the camera and
// the two lights a car face shows its car with while a film plays, one frame a field (60 a second).
// docs/formats/prologue.md, "The new-car page".

#include "gt4r/menu/faces_car.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

struct ModelStream {
    struct Light {
        float pos[4] = {}, shadow[4] = {}, diffuse[4] = {}, ambient[4] = {}, specular[4] = {};
    };
    struct Frame {
        float eye[3] = {}, rotate[3] = {}, view_angle = 0; // degrees
        float light_scale[2] = {1, 1};
    };
    Light lights[2];
    std::vector<Frame> frames;

    static constexpr float kRate = 60;
    // The frame at `seconds` (0x1FFE70: past the end the last one).
    size_t frame_at(double seconds) const;
    bool ended(double seconds) const { return frames.empty() || seconds * kRate >= double(frames.size()); }
    // The face's camera and lights at a frame (0x1FFD00).
    void apply(size_t frame, FaceView& v) const;
};
// nullopt: not a model stream.
std::optional<ModelStream> read_model_stream(ByteSpan file);
std::string model_stream_path(const std::string& root, const std::string& name);

} // namespace gt4r::menu
