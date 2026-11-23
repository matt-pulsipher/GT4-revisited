#pragma once

// What the simulation asks of a course's road (RNW4): the ground under a point as the program's
// road query finds it (docs/specs/physics.md "The ground"), the surface a wheel is on, the walls and
// the distance along the course.

#include "gt4r/formats/course/runway.h"

#include <vector>

namespace gt4r::sim {

using course::Vec3;

// The road query's answer (the program's 0x30-byte record).
struct GroundHit {
    Vec3 point;          // +0x00 where the road was met (clamped into the triangle's box)
    u8 material = 0;     // +0x0C the triangle's material: its byte +6, bits 0-4
    u8 flags = 0;        // +0x10 the triangle's flags byte (+7): bit 0 the pit lane, bit 1 wet (the surface's wet variant)
    float attr_a = 1;    // +0x14 the corners' byte +0x0E / 255, interpolated
    float attr_b = 1;    // +0x18 the corners' byte +0x0F (bits 0-6) / 127, interpolated
    Vec3 normal{0, 1, 0}; // +0x1C the triangle's unit normal
    float plane_d = 0;   // +0x28 the plane: normal . p + plane_d = 0
    int triangle = -1;   // +0x2C
    int cluster = -1;    // +0x2E
};

// A wheel's surface index from the road's answer (0x3F4E40): the material, or with flags bit 1 the
// material's wet variant (0 and 7: 20; 1, 10, 11 and 12: 21; 9: 22).
int surface_index(u8 material, u8 flags);
constexpr int kSurfaces = 24;

class RoadGround {
public:
    explicit RoadGround(const course::Runway& road);
    const course::Runway& road() const { return *road_; }

    // 0x3E5A30: the first road triangle a line from `from` straight down meets (down to the road's
    // lowest point). False when the point is below the road's bounds or meets no triangle.
    bool cast_down(const Vec3& from, GroundHit& out) const;
    // The ground under a wheel as the vehicle step asks for it (0x395760): cast_down from 5 m above.
    bool under(const Vec3& p, GroundHit& out) const { return cast_down({p.x, p.y + 5.0f, p.z}, out); }

    // The distance along the course (Runway::Checkpoint::v) of a point: between the two checkpoint
    // lines it lies between, by its place along the line through their middles. `hint`: the
    // checkpoint found last time (-1: none); the search starts around it. False off the course.
    bool distance_along(const Vec3& p, float& v, int* hint = nullptr) const;
    // The course's middle line at distance v (wrapped into the lap): the point and the direction of travel.
    void middle_at(float v, Vec3& point, Vec3& direction) const;

    // The nearest wall segment within `reach` of p, in the horizontal plane: the closest point on
    // it and the unit normal from the wall towards p. False when none is that near.
    struct WallHit {
        Vec3 point, normal;
        float distance = 0;
    };
    bool nearest_wall(const Vec3& p, float reach, WallHit& out) const;

private:
    void walk(size_t node, size_t leaf, u32 depth, float lo[3], float hi[3], const Vec3& from, float y_low, GroundHit& best, float& best_t) const;
    void test_cluster(size_t leaf, const Vec3& from, float y_low, GroundHit& best, float& best_t) const;

    const course::Runway* road_;
    size_t first_ = 0; // the checkpoint with the least distance
    // The walls on a grid of cells for the wall search.
    struct WallSeg {
        Vec3 a, b;
    };
    std::vector<WallSeg> walls_;
    float cell_ = 16.0f;
    int nx_ = 0, nz_ = 0;
    std::vector<std::vector<u32>> wall_cells_;
};

} // namespace gt4r::sim
