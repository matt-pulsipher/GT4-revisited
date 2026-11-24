#include "gt4r/menu/faces_tooltip.h"

namespace gt4r::menu {

namespace {

int pictures(float seconds) { return int(seconds * 60.0f); } // 0x24A250 (60 Hz)

} // namespace

// State 0 (0x24A380): the text measured against the face; in the middle when it fits 95 % of
// the width, else from 5 % in; it starts below the face and waits (0 s by default, +0x10C).
void ToolTipScroll::start(float text_w, float face_w, float face_h) {
    text_w_ = text_w, face_w_ = face_w, face_h_ = face_h;
    fits_ = text_w <= face_w * 0.95f;
    x_ = fits_ ? (face_w - text_w) * 0.5f : face_w * 0.05f;
    y_ = face_h;
    shown_x_ = x_, shown_y_ = y_;
    wait_ = pictures(0);
    state_ = 1;
}

void ToolTipScroll::step() {
    switch (state_) {
    case 1: // the wait before it rises
        if (--wait_ < 0) state_ = 2;
        break;
    case 2: // it rises; a text that fits stays, a longer one waits W x 0.4 / 80 seconds
        y_ -= kRise / 60.0f;
        if (y_ <= 0) {
            y_ = 0;
            if (fits_) state_ = 100;
            else wait_ = pictures(face_w_ * 0.4f / kSpeed), state_ = 3;
        }
        break;
    case 3:
        if (--wait_ < 0) state_ = 10;
        break;
    case 10: // across to the left until it has gone, then a second's wait
        x_ -= kSpeed / 60.0f;
        if (x_ < -text_w_) wait_ = pictures(1.0f), state_ = 11;
        break;
    case 11: // back from the start, below, after a second
        if (--wait_ < 0) {
            x_ = face_w_ * 0.05f, y_ = face_h_;
            shown_x_ = x_, shown_y_ = y_;
            wait_ = pictures(1.0f);
            state_ = 1;
        }
        break;
    default: break;
    }
    // Drawn where it is going, eased (0x57B088): across by 0.1, up by 0.3 a picture.
    shown_x_ += (x_ - shown_x_) * 0.1f;
    shown_y_ += (y_ - shown_y_) * 0.3f;
}

} // namespace gt4r::menu
