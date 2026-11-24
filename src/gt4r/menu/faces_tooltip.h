#pragma once

// A tool tip's text in motion (mToolTipFace, update 0x24A840 / 0x24A2F8, docs/formats/menu.md
// "Tool tips"): a new text rises into the face; one that fits 95 % of the face's width stays in
// the middle, a longer one waits, scrolls left until it is gone and comes back from the start.

namespace gt4r::menu {

class ToolTipScroll {
public:
    // A new text `text_w` wide in a face `face_w` x `face_h` (page units).
    void start(float text_w, float face_w, float face_h);
    // One picture of the menus (1/60 s).
    void step();
    // Where the text's left and top stand in the face (eased as the program draws them).
    float x() const { return shown_x_; }
    float y() const { return shown_y_; }
    bool fits() const { return fits_; }

    // The program's constants: the speed (+0x148 x +0x14C, 8 x 10 units a second), the rise
    // (10,000 units a second: at once, eased by 0.3 a picture), the ease across (0.1).
    static constexpr float kSpeed = 80.0f, kRise = 10000.0f;

private:
    int state_ = 100; // 100: still
    int wait_ = 0;    // pictures
    float text_w_ = 0, face_w_ = 0, face_h_ = 0;
    float x_ = 0, y_ = 0;            // where the text is going (+0x128, +0x12C)
    float shown_x_ = 0, shown_y_ = 0; // where it is drawn (+0x138, +0x144)
    bool fits_ = true;
};

} // namespace gt4r::menu
