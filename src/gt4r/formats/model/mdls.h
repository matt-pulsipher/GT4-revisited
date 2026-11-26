#pragma once

// "MDLS" model sets: Gran Turismo 4's geometry container (car bodies, wheels, tires, wings,
// course scenery, prize objects, drivers). docs/formats/model.md has the layout.
//
// It is the successor of Gran Turismo 3's "GTM1": the same draw programs (with more commands),
// shape tables, 0x50-byte materials and Tex1 texture sets under a new header, and vertex data
// that may be packed (16-bit positions with a scale per shape, 8-bit normals, 16-bit texture
// coordinates). to_gtm1() rewrites a model set as the GTM1 block that says the same, which
// the shared engine's decoder (formats/model/gtm1.h) turns into a scene::Model.

#include "formats/model/gtm1.h"
#include "gt4r/base/types.h"
#include "scene/model.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace gt4r::mdls {

// The header's counts and tables (offsets from the block's start).
struct Header {
    u32 size = 0; // of the block
    u32 models = 0, shapes = 0, materials = 0;
    u32 texture_sets = 0;  // per list
    u32 texture_lists = 0; // lists of sets (+0x1E)
    u32 variations = 0;    // material sets (+0x2C)
    u32 bones = 0;         // bind matrices (+0x2E)
    u32 model_table = 0, shape_table = 0, material_table = 0, texture_table = 0, variation_table = 0;
    bool script = false; // the set has a script (the header's register and method tables)
};

bool is_mdls(ByteSpan data);
// Throws FormatError.
Header read_header(ByteSpan data);
// Offsets of the model sets inside a file (a car, a course): "MDLS" at 16-byte steps whose header
// fits the file.
std::vector<size_t> find_model_sets(ByteSpan file);
// Operand bytes of a draw-program command (-1: a branch, read by its own layout; -2: no such command).
int command_operand_bytes(u8 op);

// What a model's draw program reads of its script's registers to place and blend its shapes: each matrix command by a
// register (0x44 .. 0x4B but 0x47, once per register: the first on any path) with the program's matrix just before it (row
// vectors, as the shapes are decoded: Scene::turned splits off the shapes under it), and the registers its morph weights
// come from (0x4C). Throws FormatError.
struct RegisterTurn {
    u8 op = 0;
    u16 cell = 0;
    std::array<float, 16> pivot{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct RegisterReads {
    std::vector<RegisterTurn> turns;
    std::vector<u16> weights;
};
RegisterReads register_reads(ByteSpan mdls, u32 model);
// The matrix command 0x48 makes of its four registers (an angle in degrees, then the axis), in row vectors (v' = v M).
std::array<float, 16> register_turn(float degrees, float ax, float ay, float az);

struct ConvertStats {
    size_t shapes = 0, packets = 0;
    size_t packed_packets = 0;   // rewritten with float vertex data
    size_t unknown_unpacks = 0;  // vertex data at addresses this reader does not know (left out)
    std::map<std::string, size_t> unknown_kinds; // the same by format and address ("V2-8 @51")
    size_t commands = 0;         // draw-program commands read
    size_t replaced_commands = 0; // commands only this format has (see to_gtm1)
};

// What the game sets up around a model set's draw, for a still picture of it.
struct Scene {
    // Command 0x50 draws with a texture set the game binds to one of 16 slots before the draw (a
    // wheel's face picture in slot 2, by the wheel's speed; slot 0 is the reflection picture). An
    // entry >= 0 picks a set of the model's own list for the slot instead; -1 leaves the texture
    // as it was.
    std::array<int, 16> outside_sets{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // The factor on the vertex colours of pre-lit shapes (a course's scenery is drawn brighter than
    // its stored colours: course_file.h).
    float color_scale = 1.0f;
    // A program's 0x06 switches ask the set's callback for a value by id (a car: 0 the brake
    // lamps, 7 the lamp level, 11-14 a wheel's frame, 47 the spoiler; docs/formats/model.md).
    // With `resolve_callbacks` every such switch is settled when the set is converted: the id's
    // value here (`callback_default` for an id without one) takes that case when the switch has
    // it, and any other value goes on after the table, at the construct's end.
    bool resolve_callbacks = false;
    std::map<u16, int> callback_values;
    int callback_default = 0;
    // >= 0: only the shapes drawn inside the constructs of the switches with this id are kept
    // (a car's brake parts, drawn in the wheel's frame under id 11 + wheel).
    int only_inside = -1;
    // >= 0: a shape with two key shapes is written as the one between them at this weight (0 the
    // first, 1 the second), as the game blends them for a draw (commands 0x35 and 0x4C).
    float morph_weight = -1.0f;
    // The weight by the register the draw takes it from (0x4C's cell), for a program with several (a sky's
    // aeroplanes); another register's is morph_weight.
    std::map<u16, float> morph_weights;
    // Not empty: only these models' programs and the shapes they draw are converted (the others
    // are left as they are and must not be decoded): one animated model's pose without the
    // whole set's work.
    std::vector<u32> only_models;
    // With it: the other models draw nothing in the block, so the block can be decoded whole.
    bool others_empty = false;
    // The values of the script's registers the draw programs read (segment 0 cells), when the
    // set's script has been run for this draw: a 0x4D switch takes its register's case (none: it
    // goes on after the table). Empty: every register reads 0.
    std::vector<u32> registers;
    // The draws under a turn or move by a register (0x44 .. 0x4B: a car's flywheel, from the command to where the stack
    // level it changes is popped; the innermost such): -2 all kept, -1 only those under none, >= 0 only those under the
    // one by this register.
    int turned = -2;
};
// A wheel file's sets: [0, 1] the rim band, [2] an empty set, [3 ..] face pictures, still and
// turning by turns (its symbols "NRM" and "ROT" number them 0 .. 3). A still wheel's face:
constexpr int kWheelFaceSet = 3;
constexpr int kWheelFaceSlot = 2;

// The model set as a GTM1 block. Its draw programs keep their layout (but those that switch the reflection
// factors, which are copied with GTM1's commands for them); commands GTM1 lacks are replaced by no-ops of the same size, so a still model is drawn: no callbacks (a car's
// wheels), script registers read as 0 (a register switch takes its first branch), outside
// texture sets as `scene` says. Throws FormatError.
// A reflection pass drawn with the program's constant factors (command 0x57: C0 = 1, C1 = 0) has C1 = -0
// (std::signbit); the others C0 = 1, C1 = +0, where the game takes the draw's factors.
Bytes to_gtm1(ByteSpan mdls, ConvertStats* stats = nullptr, const Scene& scene = {});

// The set's draw of its models (all, or one) through the shared decoder.
struct Options {
    int model = -1; // -1: every model
    int lod = 0;
    bool textures = true;
    int texture_list = 0; // which list of texture sets (+0x1E)
    Scene scene;
};
gt3r::scene::Model build_model(ByteSpan mdls, const Options& opt = {}, const std::string& name = {});
// The same with the shared decoder's own options (courses: its render-state and alpha-test handling).
gt3r::scene::Model build_model(ByteSpan mdls, const gt3r::gtm1::Options& opt, const Scene& scene, const std::string& name);

} // namespace gt4r::mdls
