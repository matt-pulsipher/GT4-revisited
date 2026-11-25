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
    // The stored pose as floats (to_gtm1 writes these; SkinTable keys on them).
    float pos[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    std::array<u8, 4> bones{};
    std::array<float, 4> weights{};
    // What the program's blend (VU0 0x2E8 / 0x460) starts from, as the bits it converts or loads:
    // the packed layout's integers (s16 position, s8 normal, s8 weights, the block's position
    // scale), or the float layout's floats (scale 1.0).
    bool packed = false;
    s32 ipos[3] = {0, 0, 0}, inormal[3] = {0, 0, 0}, iweight[4] = {0, 0, 0, 0};
    float scale = 1.0f;
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
// `out` (each transform the identity): a vertex found in `table` is blended as the program's VU0
// micro-routine 0x2E8 does (blend() below); the others are left at their place (by their mesh's
// transform). Returns how many vertices were moved.
// One vertex blended in the program's order and arithmetic (VU0 0x2E8 / 0x308; the PS2's floats,
// ee::F): weights = int x (1/127) (a multiply by 0x3C010204, never renormalized), the position =
// int x scale, each input component multiplied by the weight first, then one multiply-add chain
// over the bones 0..3 (rows x, y, z, then the translation row x the weight), the normal the same
// chain without translations, renormalized by 1/sqrt((x*x + y*y) + z*z). A bone past the palette
// draws with the identity.
void blend(const SkinVertex& v, const Palette& palette, float pos[3], float normal[3]);

size_t pose(const std::vector<gt3r::scene::Mesh>& bind, const SkinTable& table, const Palette& palette, std::vector<gt3r::scene::Mesh>& out);

} // namespace gt4r::mdls
