#pragma once

// A skeleton's motion (Mot1, the GT Auto mechanic's and cloths' .mot files): the motion library's
// scene program (0x42B440) run at a time of a clip, giving each named node its matrix, and the
// bones' palette from it (0x42A660). Also the time rule of a car face's model motions (0x21B4F8,
// 0x21B528). docs/formats/menu.md "Model motions".

#include "gt4r/base/types.h"
#include "gt4r/formats/model/mdls_skin.h"
#include "render/math3d.h"

#include <map>
#include <string>
#include <vector>

namespace gt4r::menu {

struct SkeletonMotion {
    // A value of the program: a constant, or a channel of the clip (`channel` >= 0).
    struct Value {
        float constant = 0;
        int channel = -1;
    };
    struct Op {
        u8 code = 0;      // 2 push, 3 pop, 4 a node, 5 translate, 6 / 7 / 8 rotate X / Y / Z, 9 scale,
                          // 0x13 rotate X, Y, Z, 0x10 a named value (others: kept, not evaluated)
        std::string name; // 4, 0x10
        std::vector<Value> values;
    };
    struct Channel {
        float start = 0, first = 0; // the first key's time and value
        struct Piece {
            float c1 = 0, c2 = 0, end_time = 0, end_value = 0;
        };
        std::vector<Piece> pieces;
        float at(float t) const; // a cubic Bezier in value between each two keys
    };
    struct Clip {
        std::string name;
        float length = 0, loop_start = 0; // seconds
        std::vector<Channel> channels;
    };
    std::vector<Clip> clips;
    std::vector<Op> program;

    static bool is_motion(ByteSpan d);
    // Throws FormatError.
    static SkeletonMotion parse(ByteSpan file);

    float length() const { return clips.empty() ? 0 : clips[0].length; }
    float loop_start() const { return clips.empty() ? 0 : clips[0].loop_start; }
    float at(const Value& v, float t) const;
    // The named value (op 0x10) at t, `otherwise` without one of that name.
    float named(const std::string& name, float t, float otherwise) const;
    // Each node's matrix (column vectors) at t of the first clip, the program started from `start`:
    // every operation multiplies onto the current matrix on the right (the program's row form on the
    // left), a node takes the current matrix, a node after a node without a push is its child.
    std::map<std::string, gt3r::render::Mat4> nodes(float t, const gt3r::render::Mat4& start) const;
};

// The bones' palette for nodes (bones matched to nodes by name; palette[index] = node x inverse
// bind); a bone without a node keeps the identity, so it stays where it is stored.
mdls::Palette palette(const std::vector<mdls::Bone>& bones, const std::map<std::string, gt3r::render::Mat4>& nodes);

// A model motion's clock (mModelMotion: playing, time, loop): stepped 1/60 s a picture while it
// plays; past the clip's length L it goes back by L - S (S the loop start) when it loops, else it
// stops and holds there. Before it is played its time is 0.
struct MotionClock {
    bool running = false;
    bool loop = true; // the program's default
    u64 since = 0;    // the picture it was played from
    u32 base = 0;     // steps played before that
    void play(u64 frame);
    void stop(u64 frame);
    u32 steps(u64 frame) const { return base + (running && frame > since ? u32(frame - since) : 0); }
    // The time to draw at, and whether the motion still plays.
    float time(u64 frame, float length, float loop_start, bool* playing = nullptr) const;
};

} // namespace gt4r::menu
