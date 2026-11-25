#pragma once

// A photo location (slot 51 of a course file, `CRPM`): the map the photo page shows, the places
// the camera may stand in, the car's places, and where both start. docs/formats/photo.md.

#include "gt4r/base/types.h"

#include <array>
#include <vector>

namespace gt4r::photo {

// A place on the ground: the course's x and z (metres).
struct Vec2 {
    float x = 0, z = 0;
};

// The angle of (dx, dz) about the vertical in degrees, 0 .. 360, from +x toward +z (0x36ECD0).
float angle_deg(float dx, float dz);
// An angle brought into 0 .. 360 (0x36EAD8).
float wrap360(float deg);
// The turn from `from` to `to`, -180 .. 180, for two angles in 0 .. 360 (0x36EB98).
float turn_deg(float to, float from);

// A shape on the ground: closed outlines (a hole is an outline inside another).
struct Shape {
    std::vector<std::vector<Vec2>> outlines;
    bool contains(const Vec2& p) const;
    Vec2 middle(size_t outline = 0) const; // the mean of an outline's points
};

// Where a move inside a shape met its border, kept from one picture to the next (the camera slides
// along that edge): what the program keeps at the photo state's +0x88.
struct BorderTrack {
    int state = 0; // 0 free, 1 on an edge, 2 at an edge's end
    Vec2 at;       // where the last move came to
    int met = 0;
    int outline = -1, edge = -1, corner = -1;
    float distance = -1, angle = 0;
    void reset(const Vec2& p);
};

// A place the camera may stand in.
struct Cell {
    bool spot = false; // a single place (its shape's middle): the camera snaps to it
    int shape = 0;
    std::vector<Vec2> targets;   // the camera must look between them (none: anywhere)
    std::vector<u16> car_shapes; // where the car may stand while the camera is here
    Vec2 look;                   // what the camera looks at when it is put here
    float angle = 0;             // ❓ (25 and 35 on Times Square): not read by the routines known
    float eye_low = 0, eye_high = 0; // the eye's lowest place above the ground, its highest
};

class PhotoCourse {
public:
    // `section`: the course's slot 51. Throws FormatError.
    static PhotoCourse parse(ByteSpan section);

    std::vector<Cell> cells;
    std::vector<Shape> shapes;
    std::array<float, 4> drag{}; // x0, z0, x1, z1 the pointer may be moved in
    Vec2 camera_start, car_start;
    float car_heading = 0;       // degrees
    std::array<float, 4> view{}; // x0, z0, x1, z1 the map may show
    u32 map_offset = 0;          // the map's model set in the section

    // The cell `p` lies in, or the spot nearer than `radius` (p moves onto it); -1 none (0x3FECB0).
    int cell_at(Vec2& p, float radius) const;
    // The car's shape of a cell's that holds `p`; -1 none (0x3FF0B8).
    int car_shape_at(const Vec2& p, int cell) const;
    // Where a move from `from` to `to` ends inside a cell's shape (0x3FEE08).
    Vec2 move(int cell, const Vec2& from, const Vec2& to, BorderTrack& track) const;
    // The headings the camera may look to from `p` in a cell: [to the cell's look place, least, greatest] (0x3FEF60).
    std::array<float, 3> limits(int cell, const Vec2& p) const;
    // The heading a spot faces (0x3705A0), for its mark on the map.
    float spot_heading(int cell) const;
};

} // namespace gt4r::photo
