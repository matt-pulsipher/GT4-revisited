#pragma once

// A film: what the photo pause keeps of a photo to develop it again (Photo Lab's Film folder), as the program writes it
// (0x334E38 -> 0x334FC8): chunks of a four-letter tag and a u32 size with its 8-byte head (0x45B4D8 opens one, 0x45B548 closes
// it), holding the photo's settings (PMDI, FILM), the race's cars (RHDR, CDTL) and one or two frames of the race (SFRM: the
// camera PCAM, a CARD a car with its CPRM). docs/formats/photo.md "Films".

#include "gt4r/base/types.h"
#include "gt4r/race/posture_solver.h"
#include "gt4r/race/simulation.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt4r::photo::film {

using Vec3 = course::Vec3;

// The chunk stream's writer (0x45AF18 .. 0x45B548): little-endian fields, chunks opened and closed.
class Writer {
public:
    void u8_(u8 v) { out_.push_back(v); }
    void u16_(u16 v) { u8_(u8(v)), u8_(u8(v >> 8)); }
    void u32_(u32 v) { u16_(u16(v)), u16_(u16(v >> 16)); }
    void f32_(float v);
    void vec(const Vec3& v) { f32_(v.x), f32_(v.y), f32_(v.z); }
    void text(const std::string& s); // NUL-ended (0x45B040)
    void bytes(ByteSpan b) { out_.insert(out_.end(), b.begin(), b.end()); }
    void open(const char tag[5]);
    void close();
    const Bytes& data() const { return out_; }

private:
    Bytes out_;
    std::vector<size_t> open_;
};

// A reader over one chunk's payload (0x45B5C8 .. 0x45B7B0): fields in order, sub-chunks found by tag.
class Reader {
public:
    Reader(ByteSpan d, size_t at = 0, size_t end = size_t(-1)) : d_(d), at_(at), end_(std::min(end, d.size())) {}
    bool ok() const { return ok_; }
    size_t at() const { return at_; }
    size_t end() const { return end_; }
    u8 u8_();
    u16 u16_();
    u32 u32_();
    float f32_();
    Vec3 vec();
    std::string text();
    void bytes(u8* to, size_t n);
    // The next chunk if it has this tag (the reader moved past it), else none and the reader stays.
    std::optional<Reader> chunk(const char tag[5]);
    // The next chunk's tag ("" at the end).
    std::string next_tag() const;
    // The next chunk whatever its tag, its payload raw.
    std::optional<std::pair<std::string, Bytes>> raw_chunk();

private:
    ByteSpan d_;
    size_t at_, end_;
    bool ok_ = true;
};

// The photo's settings (FILM: the photo object of 0x333A00).
struct Photo {
    bool pmdi = true;               // PMDI: the photo's +0x124
    u16 tile_w = 640, tile_h = 480; // +0x04, +0x08
    u8 tiles_x = 2, tiles_y = 2;    // +0x0C, +0x10
    float aspect = 4.0f / 3.0f;     // +0x14
    bool upright = false;           // +0x18
    u8 pictures = 32;               // +0x118
    bool p124 = true;               // +0x124
    float shutter_frames = 0;       // +0x120: the shutter's time in 60ths of a second (at most 1)
    s32 filter = 0;                 // +0x128: 0 none, 1 Mature, 2 black and white (the pause's +0x1A8)
    float p12c = 0;                 // +0x12C (0 in the pause)
    float saturation = 1;           // +0x130: 1 + the option photo_saturation / 10
    float exposure = 0;             // +0x134: EV (the camera's virtual 16)
    float kelvin = 5500;            // +0x138: the white balance (the camera's virtual 17)
    float p13c = 0;                 // +0x13C
    std::array<u8, 32> frame{};     // +0x144: the car's frame in the picture, the JPEG's G4PI record {u32 1, u32 3, f32 x, y, x0, y0, x1, y1}
};

// A car of the race (RHDR, CDTL).
struct Car {
    std::string label, paint; // the car's label, its paint's
    u8 b = 0;
    // CDTL: its two driver texts, two more, a byte (the entry's +0x345C)
    std::array<std::string, 4> looks{"", "", "default", "default"};
    u8 c = 0;
};

// The camera of a frame (PCAM, 0x33AEA0 -> 0x33ADE8: the camera manager at the frame's time).
struct Camera {
    Vec3 eye, target, up{0, 1, 0}; // the target: the eye and the focus distance (at least 1 m) along the view
    float fov = 60;                // degrees across a 4:3 picture's width (2 atan of the projection's x scale)
    float aspect = 0.75f;          // the projection's x scale over its y
    float near = 0.1f, far = 5000;
    float f_number = 2.8f, shutter = 1.0f / 125, exposure = 0, kelvin = 5500;
};

// A car's state as the car model's parameter sponsor gives it (CPRM, 0x459E30: CarParamSponsor virtuals 4 .. 27 at the frame's
// time). Course space; angles in degrees.
struct Params {
    Vec3 ground_turn, ground_at; // virtuals 4, 5: the frame the body and the driver's figure are drawn in (pitch, yaw, roll) and its place
                                 // (car + 0x888: ✔ the original's film developed at it gives its photo); a state's `position`
    Vec3 velocity;               // virtual 6
    Vec3 turn, at;               // virtuals 7, 8: the body's angles (pitch, yaw, roll) and a point CarState::ground_drop below virtual 5's
                                 // (car + 0x894), the wheels' places measured from it
    float v15 = 0, v16 = 0.02f;  // virtuals 15, 16
    bool v18 = false;            // virtual 18
    float v19 = 0, v20 = 0, v21 = 0, v22 = 0;
    struct Wheel {
        Vec3 turn;          // virtual 9: spin, steer, camber
        Vec3 at;            // virtual 10: the wheel's place in the body's frame (x right, y up, z back)
        float v11 = 0, v12 = 0, v13 = 0, v14 = 0;
        float spun = 0;     // virtual 9's spin over the step (now less the step before)
        float v24 = 0, v25 = 0;
    };
    std::array<Wheel, 4> wheels;
    std::array<float, 4> v26{-1, -1, -1, -1};
    float rpm = 0;          // virtual 23
    std::array<Vec3, 4> v27{};
};

// A car in a frame (CARD): its parameters; the driver's and the co-driver's figures, its skid marks (kept as read).
struct CarFrame {
    Params params;
    std::optional<Bytes> driver, navigator, skids; // DRVR, NAVI, SMOK payloads
};

struct Frame {
    Camera camera;
    std::vector<CarFrame> cars;
    std::optional<Bytes> gadgets, particles; // GDGT, PTCL payloads
};

struct Film {
    Photo photo;
    std::string course;            // RHDR: the course's label
    std::string kind = "photo_drive"; // and the race's (the sequence)
    std::vector<Car> cars;
    std::optional<Bytes> display;  // ENVP: the race's display state (0x17F4 bytes) when kept
    std::vector<Frame> frames;     // one, or two (the shown step's and the next's) for a shutter with time

    Bytes encode() const;
    static std::optional<Film> decode(ByteSpan data);
};

// A frame's cones and boards (GDGT, 0x3E5408), by the rigid bodies' slots (RigidBodyManager: 160 of 0x60 bytes, 0x3E4CE0; the
// course's bodies take them in order, 0x3E4E50): u8 0, u16 the slots; each a bool, the slot in use (0x3E4F60: on the manager's
// list), and for one in use a bool, the body moving (0x3E31E0: fewer than three of its hull points' counts +0x58 at 2), and for
// a moving one u8 its kind (+0x44), its three angles (+0x04 .. +0x0C) each as u8 of trunc(a x 128 / pi), its place (+0x10 .. +0x18).
struct GadgetSlot {
    bool used = false, moving = false;
    u8 kind = 0;
    std::array<float, 3> angles{}; // radians
    Vec3 pos;
};
constexpr u16 kGadgetSlots = 160;
Bytes gadgets_payload(std::span<const GadgetSlot> slots);
// A frame's particle emitters (PTCL, 0x3C2990): u8 0, each emitter's kind + 1 and its own record, u8 0 at the end.
Bytes no_particles();

// A driver's or co-driver's figure (DRVR / NAVI, 0x339AD0, 952 bytes): u8 1; the 20 bones in the name table's order (0x41A988),
// each its place and quaternion (x, y, z, w) in course space (Bone 0x41B678), its vector in its own frame (StickBone 0x41BCC0),
// and for the pelvis and the scapula their two joints (PlateBone 0x41C0B8); the figure's four floats at +0x680; the posture
// caller's fields (a bool, a word, a bool, two floats, a 4 x 4 matrix, a bool, 8 bytes). The figure's frame in course space:
// `at` its place (the body's point at the center of gravity's height: CPRM's virtual 5) and the car's axes; the skeleton is in
// the car's frame (CarVisual::body_matrix's).
constexpr size_t kFigureBytes = 952;
Bytes figure_payload(const race::posture::Skeleton& s, const Vec3& at, const Vec3& right, const Vec3& up, const Vec3& forward);
// A figure back in its frame; `base` gives what the payload does not hold (the bones' lengths). None when it does not read.
std::optional<race::posture::Skeleton> figure_of(ByteSpan payload, const Vec3& at, const Vec3& right, const Vec3& up, const Vec3& forward,
                                                 const race::posture::Skeleton& base);

// The parameters of a car drawn from a state (the drawn body and wheels, course space), and back.
Params params_of(const race::CarState& s, const race::CarState* before = nullptr);
race::CarState state_of(const Params& p);

// The body's angles (pitch, yaw, roll; degrees) of its axes, and back: the body is turned by yaw about y, pitch about x, roll
// about z, in that order from the course (front -z, right +x, up +y).
Vec3 angles_of(const Vec3& right, const Vec3& up, const Vec3& forward);
void axes_of(const Vec3& angles, Vec3& right, Vec3& up, Vec3& forward);

} // namespace gt4r::photo::film
