#pragma once

// "RNW4": a course's road (course slot 55): the surface triangles, the walls, the checkpoints
// across the track, the start grid and pit stops; docs/formats/course.md "Road".

#include "gt4r/base/types.h"
#include "scene/model.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace gt3r::course {
struct Road;
}

namespace gt4r::course {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Runway {
    struct Vertex {
        Vec3 pos;
        s16 a = 0; // +0x0C
        u8 b = 0, c = 0;
    };
    struct Triangle {
        std::array<u16, 3> v{};
        u8 bits = 0, kind = 0; // +0x06, +0x07
        u32 flags = 0;         // +0x08
    };
    // A line across the track at a distance along it.
    struct Checkpoint {
        Vec3 left, middle, right;
        float v = 0; // metres from the course's origin
    };
    struct Place {
        Vec3 pos;
        float heading = 0; // radians about +Y
    };
    struct WallVertex {
        Vec3 pos;
        u32 tag = 0;
    };
    // The triangles under a cell of the lookup tree (indices into `triangles`).
    struct Cluster {
        std::vector<u16> triangles;
        u16 checkpoint_first = 0, checkpoint_count = 0; // a run of `checkpoint_lookup`
    };

    u32 flags = 0, version = 0;
    float length = 0;  // of a lap, metres
    float start_v = 0, goal_v = 0;
    Vec3 min, max;     // the road's bounds
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Checkpoint> checkpoints;
    std::vector<u16> checkpoint_lookup;
    std::vector<Place> grid, pit_stops;
    std::vector<WallVertex> wall_vertices;
    // The walls are chains of `wall_vertices` in order. A chain's first vertex has its length in
    // the tag's low 16 bits, its last vertex minus (length - 1) there (the link back to the first:
    // the chains are closed); bit 16 marks a few vertices (not yet understood).
    std::vector<s16> wall_indices; // wall vertices by lookup cell (not yet understood)
    // The walls' segments as pairs of wall vertices.
    std::vector<std::pair<u32, u32>> wall_segments() const;
    std::vector<Cluster> clusters;
    // The lookup tree, one byte per node in depth-first order (left subtree first): bits 0-5 the
    // split place, bits 6-7 the axis (0 x, 1 y, 2 z). A node at depth d splits its box at
    // min + (max - min) (place + 0.5) / 64; the leaves, left to right, are `clusters`.
    Bytes tree;
    u32 tree_depth = 0;
    // A road read from Gran Turismo 3's "GTRW" (Prologue's courses; runway_gtrw.h): that road as it reads, for the queries
    // that run on it as the program does.
    std::shared_ptr<const gt3r::course::Road> gtrw;

    static bool is_runway(ByteSpan d) { return d.size() >= 0xA0 && gt3r::has_magic(d, "RNW4"); }
    // Throws FormatError.
    static Runway parse(ByteSpan section);

    // The cluster whose cell holds (x, z), by walking the tree (y at the box's middle); -1 when
    // the road has no tree.
    int cluster_at(float x, float z) const;
    // The height of the road under (x, z) and the triangle there: the highest surface at or
    // below `y_max`. False when no triangle is under the point.
    bool height_at(float x, float z, float y_max, float& y, int* triangle = nullptr) const;

    // Made by parse(): the triangles by square cells of the ground, for a height no triangle of the point's cluster gives
    // (a point beside the road would ask them all). Without it (a road put together by hand) all are asked.
    void index_triangles();
    struct Cells {
        float x = 0, z = 0, size = 0; // the first cell's corner, a cell's side
        int nx = 0, nz = 0;
        std::vector<u32> start; // nx x nz + 1, into `list`
        std::vector<u16> list;  // ascending within a cell
        std::vector<u16> thin;  // triangles with hardly any area seen from above: asked everywhere
        size_t triangles = 0;   // of the road it was made from
    } cells;
};

// The road as a mesh: its triangles flat shaded by kind, a little above the surface.
gt3r::scene::Mesh make_road_mesh(const Runway& r);
// The walls as thin vertical ribbons and the start grid as markers.
gt3r::scene::Mesh make_wall_mesh(const Runway& r, float height = 1.0f);

} // namespace gt4r::course
