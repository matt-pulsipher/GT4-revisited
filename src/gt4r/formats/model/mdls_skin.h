#pragma once

// Skinned model sets (MDLS): the GT Auto mechanic, the car wash's cloths, the drivers and pit
// crews. Their bones (header +0x2E / +0x48), and the vertex block that follows each skinned
// packet's VIF data: the positions, normals, bones and weights the program blends on the EE
// (0x49DA60) before the packet goes to VU1. docs/formats/model.md "Skinned sets".
//
// to_gtm1 (mdls.h) writes a skinned packet's stored (bind) pose as plain positions and normals,
// so the shared decoder makes the set's still model; SkinTable and pose() move that model's
// vertices by a pose of the bones.

#include "gt4r/base/types.h"
#include "render/math3d.h"
#include "scene/model.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt4r::mdls {

// Shape flags (shape +0x08): 0x8000 a vertex block follows each packet; with 0x2000 the packed
// layout, without it the float one.
constexpr u32 kShapeSkinned = 0x8000, kShapeSkinPacked = 0x2000;

struct Bone {
    std::string name;
    u32 index = 0; // the palette slot the packets' bone bytes name
    // Model space -> the bone's space in the stored pose (column vectors; the file's 16 floats
    // are this matrix in the program's row-vector form, which is the same storage).
    gt3r::render::Mat4 inverse_bind;
};
// The bones, in the file's order (sorted by name). Throws FormatError.
std::vector<Bone> read_bones(ByteSpan mdls);

// Header +0x2A / +0x58: {name, value} (a skinned set's "SKINNING" = the model drawn with the
// bones; "careerPos" = a model drawn at the motion node of that name).
struct Symbol {
    std::string name;
    u32 value = 0;
};
std::vector<Symbol> read_symbols(ByteSpan mdls);

struct SkinVertex {
    float pos[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    std::array<u8, 4> bones{};
    std::array<float, 4> weights{};
};
struct SkinBlock {
    u32 dest = 0; // where the normals go in VU memory: 0x80 (lit), 0x40 (reflection pass), 0 none
    std::vector<SkinVertex> vertices;
    size_t end = 0; // the byte after the block
};
// The block at `at` (a packet's offset + its quadwords x 16). Throws FormatError.
SkinBlock read_skin_block(ByteSpan mdls, size_t at, bool packed);

// Every skinned vertex of a set by its stored position and normal (the floats to_gtm1 writes), so
// the vertices of the set's decoded model can be found again.
class SkinTable {
public:
    static SkinTable of(ByteSpan mdls);
    const SkinVertex* find(const float pos[3], const float normal[3]) const;
    size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }

private:
    struct Key {
        u32 w[6];
        bool operator==(const Key& o) const;
    };
    struct Hash {
        size_t operator()(const Key& k) const;
    };
    std::unordered_map<Key, SkinVertex, Hash> map_;
};

// The bones' matrices for a pose: palette[bone.index] = world(bone) x inverse_bind(bone), world the
// bone's matrix in the drawing's space. A bone the pose does not name keeps the identity.
using Palette = std::vector<gt3r::render::Mat4>;

// `bind`'s meshes (the set's model as decoded, each with its transform) moved by the palette into
// `out` (each transform the identity): a vertex found in `table` is sum_k w_k (palette[b_k] p),
// its normal turned the same way and made unit length; the others are left at their place (by
// their mesh's transform). Returns how many vertices were moved.
size_t pose(const std::vector<gt3r::scene::Mesh>& bind, const SkinTable& table, const Palette& palette, std::vector<gt3r::scene::Mesh>& out);

} // namespace gt4r::mdls
