#pragma once

// The full-body solver that poses the driver's figure (the program's "pdiRiderman" library, 0x414000-0x424000;
// docs/formats/model.md "The posture's solver"). A skeleton of 20 bones is solved from a target (the pelvis, the chest, the
// hands and the feet) made of the cockpit's places and a block of controls.

#include "gt4r/race/posture_math.h"

#include <array>

namespace gt4r::race::posture {

// In the order of the program's name table (0x622A50).
enum Node : int {
    kPelvis, kLThigh, kLShank, kLFoot, kRThigh, kRShank, kRFoot, kLowerSpine, kMiddleSpine, kScapula, kLUpperArm, kLForearm,
    kRUpperArm, kRForearm, kCranium, kLClavicle, kRClavicle, kLHand, kRHand, kNeck, kNodes
};
extern const char* const kNodeNames[kNodes];

struct Bone {
    V3 pos{};
    Quat q = identity();
    V3 vec{}; // to the bone's end, in its own frame
    F len{}, inv{};
    V3 left{}, right{}; // the pelvis's and the scapula's two joints
};
V3 bone_end(const Bone& b);       // 0x41B898
V3 bone_direction(const Bone& b); // 0x41B9E0

struct Skeleton {
    std::array<Bone, kNodes> bone;
    Bone& operator[](int n) { return bone[size_t(n)]; }
    const Bone& operator[](int n) const { return bone[size_t(n)]; }
};
// From the 15 sizes of a driver set's extension record 0x800000 (0x419F80).
Skeleton build_skeleton(const std::array<F, 15>& sizes);

// Where things are, in the frame the figure is solved in (x and z of the car negated): +0x00 .. +0x138 of the
// program's block (0x414C90).
struct Places {
    V3 pos{};                 // +0x00: the frame's place and turn
    Quat turn{};              // +0x10
    V3 foot[2]{};             // +0x20, +0x30
    V3 seat{};                // +0x40
    V3 wheel{};               // +0x50: the steering wheel's centre
    V3 lever[3]{};            // +0x60: the gear lever, and two more places of its gesture
    V3 grip[2]{};             // +0x90: handlebars' grips
    V3 column{f(0), f(0), f(1)};   // +0xB0: the steering column's axis
    V3 grip_axis[2]{{f(0), f(0), f(1)}, {f(0), f(0), f(1)}}; // +0xC0
    F wheel_radius{};         // +0xE0
    Mat3 wheel_frame{};       // +0xF0
    int shift_hand = 0;       // +0x120: 1 left, 2 right, 0 neither
    F steer_low{};            // +0x124
    F steer_high = f(kPi);    // +0x128
    int handlebars = 0;       // +0x12C
    F gesture{};              // +0x130: the gear change's phase, 0 .. 3
};
void set_column(Places& p, V3 axis); // 0x415240

// The controls (0x360 bytes, 0x416B58), named by their offset's meaning as far as it was followed.
struct Controls {
    F elbow[2] = {f(0.5f), f(0.5f)};      // +0x00
    F knee[2] = {f(0.5f), f(0.5f)};       // +0x08
    F shank_in[2]{};                      // +0x10
    F hand_turn[2]{};                     // +0x18: how far the hand takes the grip's direction
    F grip_side[2] = {f(0.5f), f(0.5f)};  // +0x20
    F wrist[2]{};                         // +0x28
    F heel_roll[2] = {f(0.5f), f(0.5f)};  // +0x30
    F heel_tilt[2]{};                     // +0x38
    F clavicle[2]{};                      // +0x40
    F clavicle_forward[2]{};              // +0x48
    F twist{};                            // +0x50
    F pelvis_roll{};                      // +0x54
    F pelvis_twist{};                     // +0x58
    F lean{};                             // +0x5C
    F nod = f(0.5f);                      // +0x60
    F head_pitch{};                       // +0x64
    F head_up_part = f(1);                // +0x68
    F head_roll{};                        // +0x6C
    F head_yaw_range = f(1);              // +0x70
    F gaze_twist = f(1);                  // +0x74
    F head[3]{};                          // +0x78: pitch, yaw, roll (what the head's solve leaves)
    V3 head_up{f(0), f(1), f(0)};         // +0x90
    int auto_clavicle = 1;                // +0xA8
    int keep_hands = 0;                   // +0xAC
    int hand_raised[2] = {1, 1};          // +0xB0
    int spine = 1, arms = 1, legs = 1, head_on = 1; // +0xB8 ..
    int no_twist = 0;                     // +0xC8
    int plain_hands = 0;                  // +0xCC
    int keep_neck = 0;                    // +0xD4
    int raw_lean = 0;                     // +0x200
    F phase_steer[3]{};                   // +0x210: the steering hand's part over the gear change
    F wheel_angle{};                      // +0x21C
    F grip_from[2] = {f(-kHalfPi), f(kHalfPi)}; // +0x220
    F grip_to[2] = {f(-kHalfPi), f(kHalfPi)};   // +0x228
    F grip_blend[2]{};                    // +0x230
    F grip_out[2]{};                      // +0x238
    F grip_back[2]{};                     // +0x240
    F wrist_base[2]{};                    // +0x248
    F wrist_phase[3][2]{};                // +0x250
    F recline{};                          // +0x268
    F recline_max = f(0.785398126f), recline_min = f(-0.785398126f); // +0x26C, +0x270
    V3 foot_offset[2]{};                  // +0x280, +0x290
    F clavicle_base[2]{};                 // +0x2A0
    F clavicle_forward_base[2]{};         // +0x2A8
    V3 chest_offset{};                    // +0x2B0
    V3 chest_offset_world{};              // +0x2C0
    F spine_length{};                     // +0x2D0
    int free_hands = 0;                   // +0x2D4
    int fixed_recline = 0;                // +0x2D8
    int raise_choice[2]{};                // +0x2DC
    int grip_kind = 0;                    // +0x2E4
    V3 gesture_offset[3]{};               // +0x2F0
    F gesture_grip[3][2]{};               // +0x320
    V3 seat_offset{};                     // +0x340
    F phase{};                            // +0x350
};
// The single-seat racers' reclined posture (0x416F88).
void set_reclined(Controls& c);

// What a solve aims at (0xC0 bytes): the pelvis's turn and place, the feet, the chest, the hands and their directions.
struct Target {
    Quat turn = identity();       // +0x00
    V3 pelvis{};                  // +0x10
    V3 foot[2]{};                 // +0x20
    V3 chest{};                   // +0x40
    V3 hand[2]{};                 // +0x50
    V3 palm[2] = {axis_z(), -axis_z()}; // +0x70 (0x622B20, 0x622B50)
    V3 finger[2]{};               // +0x90
    V3 chest_forward = -axis_z(); // +0xB0
};

// Where the figure looks (0x416FD8's fourth argument): a place in the solver's frame, or in the car's.
struct Gaze {
    int kind = 0; // 0: the frame's own axes, 1: x and z negated, else the world
    V3 at{};
};

// Solves the skeleton (0x416FD8).
void solve(Controls& c, Skeleton& s, const Places& p, const Gaze* gaze);

} // namespace gt4r::race::posture
