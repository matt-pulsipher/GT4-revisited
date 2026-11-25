#pragma once

// The driver in a race car (HumanModel / RaceDriverModel): the car file's own driver set (+0x34:
// on most cars a dark head-and-shoulders board bound to the node CRANIUM, on the open race cars a
// whole skinned figure), or, for the few cars without one, a character file chosen by the car's
// cockpit kind. Each model of the set named by a symbol is drawn at the skeleton's node of that
// name, the skinned model ("SKINNING") by the bones. The skeleton is posed by driver_posture.h.
// docs/formats/model.md "The driver in the car".

#include "gt4r/base/types.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "gt4r/race/driver_posture.h"
#include "render/gpu_model.h"
#include "render/math3d.h"
#include "render/view3d.h"
#include "scene/model.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::race {

namespace render = gt3r::render;

// The character file the race gives a car without a driver set of its own (0x3B5438), by its
// cockpit kind; `index` picks one of three for kinds with a choice (the car's number). Empty: no
// figure.
std::string character_driver_file(int cockpit_kind, bool co_driver, int index);
// Info +0x02 and +0x03 of a car file. Throws FormatError on a file without an info block.
struct CockpitBytes {
    int kind = 0;
    bool co_driver = false;
};
CockpitBytes read_cockpit_bytes(ByteSpan car_file);

// The skeleton of a driver set (its extension record 0x800000). Throws FormatError.
posture::Skeleton driver_skeleton(ByteSpan set);
// A node's matrix.
render::Mat4 node_matrix(const posture::Bone& b);

// A set's models and how they go on the skeleton.
struct DriverPart {
    u32 model = 0;
    int node = -1;     // drawn at this node (posture::Node)
    bool skinned = false; // drawn by the bones ("SKINNING"; CRANIUM2 by the cranium bone's matrix)
};
std::vector<DriverPart> driver_parts(ByteSpan set);

// One figure: the models on the GPU (a set's models once, shared between the figures of a car), a
// skeleton, and the skinned model's posed geometry.
class DriverModels {
public:
    // `keep`: the models are handed to it instead of being sent to the GPU now (a car built away from
    // the GPU's thread); otherwise null.
    using Keep = std::vector<std::pair<std::unique_ptr<render::GpuModel>*, gt3r::scene::Model>>;
    // Throws FormatError.
    DriverModels(render::GpuContext& ctx, ByteSpan set, const std::string& name, Keep* keep = nullptr);
    const std::vector<DriverPart>& parts() const { return parts_; }
    const posture::Skeleton& skeleton() const { return skeleton_; }

private:
    friend class DriverFigure;
    render::GpuContext& ctx_;
    std::vector<DriverPart> parts_;
    posture::Skeleton skeleton_;
    std::vector<std::unique_ptr<render::GpuModel>> models_; // per part
    std::vector<gt3r::scene::Model> stored_;                // the skinned parts' stored pose
    mdls::SkinTable skin_;
    std::vector<mdls::Bone> bones_;
    // 0x3B1750: the parts' size on screen below which the figure takes its second and third level of
    // detail (extension record 0x800001).
    float level_size_[2] = {0.5f, 0.25f};
};

class DriverFigure {
public:
    explicit DriverFigure(const DriverModels& models) : models_(models), skeleton_(models.skeleton()) {}
    // Solves the figure in the car's own frame and (for a skinned set) moves its vertices.
    void pose(const posture::PostureInput& in);
    // The figure's items with the car's body matrix.
    void add_items(std::vector<render::DrawItem>& items, const render::Mat4& body) const;
    const posture::Skeleton& skeleton() const { return skeleton_; }

private:
    const DriverModels& models_;
    posture::Skeleton skeleton_;
    std::vector<std::unique_ptr<render::GpuModel>> posed_; // per part
};

} // namespace gt4r::race
