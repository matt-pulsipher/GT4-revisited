#pragma once

// `STRB` ("strobe"): the race's converted Flash movies (the pause menu menu/common/pause.strb, the
// results page result.strb): timelines of placed shapes, text fields, buttons and nested movies,
// with their scripts (AVM1 bytecode). A reader, and a player that runs a movie's timelines and
// scripts, moves the focus between its buttons, and lists what to draw. docs/formats/strobe.md.

#include "gt4r/base/types.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::race {

struct StrobeMovie {
    struct Key {
        float rotation = 0, skew = 0, sx = 1, sy = 1, x = 0, y = 0;
        u32 mul = 0x80808080, add = 0; // 0xAABBGGRR, 0x80 = x1
    };
    // 7: a picture the program supplies by the instance's name ("_EXT_CARICON1": a car's icon);
    // anything else: a shape.
    enum TrackKind : u16 { kSprite = 2, kText = 5, kButton = 6, kExternal = 7 };
    struct Track {
        u16 kind = 0;
        int first = 0, end = 0; // frames [first, end)
        u16 index = 0;
        u32 depth = 0; // the draw order (the low half of the track's depth word)
        u16 clip = 0;  // its high half: a mask of the tracks above it up to this depth (0x480518: the object drawn as
                       // a mask, then those tracks through it); 0 none
        std::vector<std::pair<u32, int>> clip_actions; // event mask (0x40: a key pressed), action block
        std::vector<Key> keys;                         // one a frame of the span (the last holds)
        std::string name;
        const Key& key_at(int frame) const;
    };
    struct Sprite {
        std::vector<Track> tracks;
        std::vector<std::pair<int, int>> frame_actions; // frame, action block
        std::vector<std::pair<int, std::string>> labels;
        int frames = 1;
        int label(const std::string& name) const; // the frame (names compared without case); -1 none
    };
    struct Fill {
        u16 kind = 0;  // 0 solid, 1 gradient, 4 texture
        u16 index = 0; // the gradient's or texture's
        u32 value = 0; // a solid's colour; a gradient's or texture's matrix
    };
    struct Point {
        float x = 0, y = 0;
    };
    struct Shape {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        struct Group {
            u16 fill = 0, flags = 0;
            std::vector<Point> triangles; // the filled area, three points a triangle
        };
        std::vector<Group> groups;
    };
    struct Gradient {
        std::vector<std::pair<float, u32>> stops;
    };
    struct TextField {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        u32 color = 0x80FFFFFF;
        int font = 0;
        float height = 16; // pixels
        u8 align = 0;      // 0 left, 1 right, 2 centred
        std::string text;
    };
    struct Button {
        std::array<u16, 4> states{}; // sprites: up, over, down, hit area
        std::vector<std::pair<u16, int>> conditions; // mask (0x01 the focus arrives, 0x04 pressed), action block
    };
    struct Texture {
        u32 offset = 0, size = 0;
        u16 width = 0, height = 0;
    };

    float width = 640, height = 480, fps = 30;
    std::vector<Sprite> sprites; // 0: the root
    std::vector<Shape> shapes;
    std::vector<Fill> fills;
    std::vector<Gradient> gradients;
    std::vector<std::array<float, 16>> matrices; // a fill's: texture u = x m[0] + y m[4] + m[12], v = x m[1] + y m[5] + m[13]
    std::vector<std::string> fonts;              // font strings ("hm_16*80,80")
    std::vector<TextField> texts;
    std::vector<Button> buttons;
    std::vector<Bytes> actions;
    std::vector<Texture> textures;
    // The pictures the program draws (header +0xB8: 0x18 bytes each, f32 x0, y0, x1, y1 the box about
    // the track's origin, u32 name, u32 length): an external track's index picks one; its name
    // tells the program what to draw there ("screen", "TropyPanel").
    struct External {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        std::string name;
    };
    std::vector<External> externals;
    Bytes data; // the file (the textures' sets are read from it)

    static bool is_movie(ByteSpan d) { return d.size() >= 0xC0 && gt3r::has_magic(d, "STRB"); }
    // Throws FormatError.
    static StrobeMovie parse(ByteSpan file);
    ByteSpan texture_set(size_t i) const;
};

// A movie playing. The host answers the functions the scripts call that the movie does not
// define (EXIT_MENU, PLAY_SOUND, RETRYENABLE, ...; names as the scripts write them).
class StrobePlayer {
public:
    struct Value;
    using Host = std::function<Value(const std::string& name, const std::vector<Value>& args)>;

    struct Clip; // a sprite's instance
    struct Function;
    struct Value {
        enum Kind : u8 { Undefined, Null, Bool, Number, String, ClipRef, FunctionRef, ArrayRef } kind = Undefined;
        double n = 0;
        std::string s;
        Clip* clip = nullptr;
        std::shared_ptr<Function> fn;
        std::shared_ptr<std::vector<Value>> array;
        static Value number(double v) { Value x; x.kind = Number, x.n = v; return x; }
        static Value boolean(bool v) { Value x; x.kind = Bool, x.n = v ? 1 : 0; return x; }
        static Value string(std::string v) { Value x; x.kind = String, x.s = std::move(v); return x; }
        double to_number() const;
        bool truthy() const;
        std::string to_string() const;
    };

    // What is drawn, in order: a shape's group, or a text field, with its place and colour.
    struct Draw {
        const StrobeMovie::Shape::Group* group = nullptr;
        const StrobeMovie::TextField* text = nullptr;
        std::string words;    // a text field's text now
        std::string external; // a picture the host draws (its instance's name, else its clip's, else "_EXT"), centred on the origin
        float m[6] = {1, 0, 0, 1, 0, 0}; // x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5 (canvas units)
        float mul[4] = {1, 1, 1, 1}, add[4] = {0, 0, 0, 0}; // colour = c x mul + add (0..1)
        bool clipped = false;                             // only inside `clip` (a mask track's box, canvas units)
        float clip[4] = {0, 0, 0, 0};                     // x0, y0, x1, y1
    };

    StrobePlayer(std::shared_ptr<const StrobeMovie> movie, Host host);
    ~StrobePlayer();

    // From frame 0 again (the scripts of frame 0 run).
    void restart();
    // The root timeline plays on from where it stopped (a page the program starts).
    void play();
    // Time passes: a movie frame for every 1/fps seconds.
    void advance(float seconds);
    // A key (Flash's codes: 0x25 left, 0x26 up, 0x27 right, 0x28 down, 0x20 accept, 0x1B cancel).
    void key(int code);
    // Which directions move the focus (the scripts' NavigateAll / NavigateVertical).
    void navigate(bool vertical, bool horizontal) { nav_v_ = vertical, nav_h_ = horizontal; }
    // What to draw now.
    std::vector<Draw> draw_list() const;
    // The root's frame, and the label of the state shown (the last label at or before it).
    int frame() const;
    std::string state() const;
    // The focused button's instance path ("DEFAULT"), for tests; empty: none.
    std::string focus() const;
    // A clip's variable by path from the root ("BRIGHTNESS.v"), for tests.
    Value variable(const std::string& path) const;
    // What the program does to a movie's instances by name (strobe's API): a clip's member by its path
    // from the root ("BAR1.NAME", "text" / "_visible" / a variable); a clip's method ("gotoAndStop").
    // False / undefined when the path names no clip.
    bool set(const std::string& path, const std::string& member, const Value& v);
    // Every instance on the stage now: "path  kind index frame n [hidden] [\"text\"]", for tools and tests.
    std::vector<std::string> tree() const;
    Value call(const std::string& path, const std::string& method, const std::vector<Value>& args);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool nav_v_ = true, nav_h_ = true;
};

} // namespace gt4r::race
