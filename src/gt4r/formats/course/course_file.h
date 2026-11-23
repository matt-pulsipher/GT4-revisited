#pragma once

// A course file (crs/<name>): Gran Turismo 3's table of section offsets grown to 64 slots, with
// the same slot roles; docs/formats/course.md.

#include "gt4r/base/types.h"
#include "scene/model.h"

#include <string>
#include <vector>

namespace gt4r::course {

enum Slot : int {
    kSlotWorld = 1,          // MDLS: the scenery of the race view
    kSlotWorldVis = 2,       // Vls0: its visibility lists (3: the mirror's, 4: a two-player half's)
    kSlotEnvironment = 5,    // MDLS: the scenery drawn into the cars' reflection picture (6: its lists)
    kSlotReflection = 9,     // MDLS: the scenery mirrored below a wet road (10: its lists)
    kSlotReflectionMask = 13, // MDLS: its mask (14)
    kSlotAfter = 17,         // MDLS: drawn after the cars (18)
    kSlotSky = 21,           // MDLS: the sky dome
    kSlotFar = 22,           // MDLS: the distant backdrop
    kSlotEnvSky = 23,        // MDLS: the reflection picture's sky
    kSlotMirrorSky = 24,     // MDLS: the mirror's sky
    kSlotSmoke = 26,         // Tex1: tire smoke and dust
    kSlotCourseLine = 27,    // MDLS: the driving line of the license tests
    kSlotEffects = 29,       // GTFX: lamp glows
    kSlotBillboards = 30,    // BLBd
    kSlotEnv = 32,           // Env0: fog, lights, sun
    kSlotMiniMap = 33,       // GTCM
    kSlotSound = 35,         // CRSS: ambience emitters
    kSlotFlareShape = 37,
    kSlotReplay = 44,        // REP4: replay cameras (45, 46: the start's; 48: the preview's)
    kSlotGadgets = 47,       // GDGT: cones and boards
    kSlotSkyReflection = 50, // MDLS
    kSlotRunway = 55,        // RNW4: the road surface, walls, checkpoints, start grid
    kSlotCount = 64,
};

struct Section {
    int slot = 0;
    u32 offset = 0;
    u32 size = 0;    // up to the next section
    std::string tag; // its first four bytes when printable ("MDLS"), else ""
};

class CourseFile {
public:
    // Slot 0 is empty and slot 1, the scenery, starts right after the table.
    static bool looks_like_course(ByteSpan data);
    // Throws FormatError.
    static CourseFile parse(ByteSpan data);

    const std::vector<Section>& sections() const { return sections_; }
    const Section* find(int slot) const;
    // A slot's bytes; empty when the file has none.
    ByteSpan section(ByteSpan data, int slot) const;

private:
    std::vector<Section> sections_;
};

struct ModelOptions {
    bool world = true;
    bool after = true;
    bool far = true;
    bool sky = false;
    bool course_line = false;
    bool environment = false; // the reflection picture's scenery (overlaps the world)
    bool road = false;        // the road's triangles by kind, its walls and start grid (runway.h)
    bool textures = true;
    int lod = 0;
};
// The factor the scenery's stored vertex colours are drawn with: Env0 +0x38 (2.0 on every course
// seen; 1 when the block has none). Not yet compared with the original running.
float scenery_color_scale(ByteSpan env);
// The course's scenery as one model, a mesh per model of each set ("world/...", "far/...").
gt3r::scene::Model build_course_model(ByteSpan data, const ModelOptions& opt = {}, const std::string& name = {});

} // namespace gt4r::course
