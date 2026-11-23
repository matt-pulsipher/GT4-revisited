// The program's actors (what moves, fades and blinks a widget each frame), transitions (the
// colour that covers a page while it changes) and watchers (a script function every so many
// frames), as the program's own routines (docs/formats/menu.md has their addresses).

#include "gt4r/menu/runtime_internal.h"

#include <algorithm>
#include <cmath>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

struct Actor : Object {
    std::weak_ptr<LiveWidget> widget;
    using Object::Object;
    virtual void step() = 0;
    virtual void rewind() {}
    virtual void attached(LiveWidget&) {}
};

// MFadeActor(widget, source = 1, destination = 0): the widget's opacity between two values.
// `out` 1 goes to the destination, 0 back to the source, `velocity` a frame; `period` sets the
// velocity so the whole way takes that many seconds. `over` (`src_over`, `dest_over`) lets the
// value run past an end (the opacity shown stays within the two), which holds it there a while.
struct FadeActor : Actor {
    float source = 1, destination = 0, now = 1;
    float velocity = 1.0f / 30.0f;
    float slope = 0, src_over = 0, dest_over = 0;
    bool out = false, repeat = false, done = false;

    using Actor::Actor;
    void apply() const {
        if (const auto w = widget.lock()) {
            const float lo = std::min(source, destination), hi = std::max(source, destination);
            w->set_property("opacity", Value::number(std::clamp(std::clamp(now, lo, hi), 0.0f, 1.0f)));
        }
    }
    // The end the value runs to, past the source or the destination by the overshoot.
    float end(bool to_destination, float over) const {
        const float away = std::fabs(over);
        if (to_destination) return source < destination ? destination + away : destination - away;
        return source < destination ? source - away : source + away;
    }
    void warp() {
        // The program's warp reads the other end's overshoot than its update does.
        now = out ? end(true, src_over) : end(false, dest_over);
        apply();
    }
    void step() override {
        if (done) return;
        const float target = out ? end(true, dest_over) : end(false, src_over);
        const float v = (source < destination) == out ? velocity : -velocity;
        now += v;
        if (v < 0 ? now < target : target < now) {
            now = target;
            if (repeat) out = !out;
            else done = true;
        }
        apply();
    }
    void rewind() override {
        now = source;
        apply();
    }
};

// MMoveActor(widget, x, y[, scale]): the widget's place (and scale) between where it is when
// the actor is made and the destination. Each frame it moves `ratio` of what is left
// (`linear`: `ratio` of the whole way, until it arrives).
struct MoveActor : Actor {
    float source[4] = {0, 0, 1, 1}, destination[4] = {0, 0, 1, 1}, now[4] = {0, 0, 1, 1};
    float ratio = 0.2f;
    bool out = false, repeat = false, linear = false, flip = false;

    using Actor::Actor;
    void apply() const {
        if (const auto w = widget.lock()) {
            w->set_property("x", Value::number(now[0]));
            w->set_property("y", Value::number(now[1]));
            if (source[2] != destination[2] || source[3] != destination[3]) {
                w->set_property("sx", Value::number(now[2]));
                w->set_property("sy", Value::number(now[3]));
            }
        }
    }
    void warp() {
        const float* to = out ? destination : source;
        std::copy(to, to + 4, now);
        apply();
    }
    void step() override {
        const float* to = out ? destination : source;
        bool there = true;
        for (int i = 0; i < 4; ++i) {
            if (now[i] == to[i]) continue;
            if (linear) {
                const float stride = std::fabs((destination[i] - source[i]) * ratio);
                if (std::fabs(to[i] - now[i]) <= stride) now[i] = to[i];
                else now[i] += to[i] > now[i] ? stride : -stride;
            } else {
                const float before = now[i];
                now[i] += (to[i] - now[i]) * ratio;
                if (now[i] == before) now[i] = to[i]; // what is left no longer changes the value
            }
            there = there && now[i] == to[i];
        }
        if (there && repeat) {
            if (flip) out = !out;
            else {
                const float* from = out ? source : destination;
                std::copy(from, from + 4, now);
            }
        }
        apply();
    }
};

// MBlinkActor(widget): the widget's opacity as a wave. A phase runs from `source` to
// `destination` in `period` seconds; the value shown is the destination less the phase
// (`reverse_phase`: the phase itself), back and forth with `wave_form`, a square wave with
// `flick`.
struct BlinkActor : Actor {
    float phase = 0, stride = 1.0f / 30.0f, destination = 1, source = 0, flick = 0, shown_before = 1;
    int cycles = 0;
    bool repeat = true, reverse_phase = false, wave_form = false;

    using Actor::Actor;
    void attached(LiveWidget& w) override { shown_before = w.data.number("alpha", 1); }
    void set_period(float seconds) {
        stride = (destination - source) / std::max(seconds * 60.0f, 1e-3f);
        rewind();
    }
    void rewind() override {
        phase = source;
        set_face_alpha(shown_before);
    }
    void step() override {
        phase += stride;
        if (stride > 0 ? destination <= phase : phase <= destination) {
            if (repeat) phase = 0, ++cycles;
            else phase = destination;
        }
        float v = reverse_phase ? phase : destination - phase;
        if (wave_form && (cycles & 1)) v = destination - v;
        if (source != 0) v = v * (destination - source) / destination + source;
        if (flick != 0) {
            if (flick <= 1) v = destination * flick < v ? destination : 0;
            else v = std::min(v / (flick - 1), destination);
        }
        set_face_alpha(v);
    }
    // The face's own alpha (mWidget slot 58): a colour, picture, frame or text face's; a
    // composite has none and stays as it is.
    void set_face_alpha(float v) const {
        if (const auto w = widget.lock())
            if (w->is("ColorFace") || w->is("ImageFace") || w->is("FrameImageFace") || w->is("TextFace"))
                w->set_property("alpha", Value::number(std::clamp(v, 0.0f, 1.0f)));
    }
};

// A widget's rectangle as the layout and the scripts have it.
Rect rect_of(const LiveWidget& w) {
    const auto num = [&w](const char* n) { return w.get(n).is_number() ? w.get(n).as_float() : 0.0f; };
    return {num("x"), num("y"), num("w"), num("h")};
}
void set_rect(LiveWidget& w, const Rect& r) {
    w.set_property("x", Value::number(r.x)), w.set_property("y", Value::number(r.y));
    w.set_property("w", Value::number(r.w)), w.set_property("h", Value::number(r.h));
}

// MRotateActor(widget, destination, cw) (0x2CCBE8, update 0x2CDCE0): the widget's `rotate`
// toward `destination` (`out` on) or the source, 0 (off): by `ratio` of the way each picture the
// way round `cw` says, kept in 0..360; with `velocity` set, by velocity / 60 degrees a picture.
struct RotateActor : Actor {
    float source = 0, destination = 0, angle = 0, velocity = 360, ratio = 0.2f;
    bool out = false, cw = true, by_velocity = false, repeat = false, flip = true;
    using Actor::Actor;
    float target() const { return out ? destination : source; }
    void apply() const {
        if (const auto w = widget.lock()) w->set_property("rotate", Value::number(angle));
    }
    void warp() {
        angle = target();
        apply();
    }
    void rewind() override { warp(); }
    void step() override {
        const float to = target();
        if (by_velocity) {
            const float stride = velocity / 60.0f;
            if (cw) angle = (to - angle >= 0 && to - angle <= stride) ? to : angle + stride;
            else angle = (angle - to >= 0 && angle - to <= stride) ? to : angle - stride;
        } else {
            float d = to - angle;
            if (cw)
                while (d < 0) d += 360;
            else
                while (d > 0) d -= 360;
            angle += d * ratio;
            angle = std::fmod(angle, 360.0f);
            if (angle < 0) angle += 360;
        }
        apply();
    }
};

// MChaseActor(widget) (0x2841C0, update 0x284270): the widget hops in place: from where it was
// made, up by |sin| of a phase that steps 360 / (period x 60) degrees a picture, the fraction
// dropped each time (`floating_range_y` up, `floating_range_x` across by sin). `doStart`,
// `doStop` (back to its place).
struct ChaseActor : Actor {
    float base_x = 0, base_y = 0, range_x = 0, range_y = 16, stride = 360.0f / (8.5f * 60.0f), period = 8.5f;
    int phase = 0;
    bool stopped = true;
    using Actor::Actor;
    void attached(LiveWidget& w) override {
        const Rect r = rect_of(w);
        base_x = r.x, base_y = r.y;
    }
    void step() override {
        const auto w = widget.lock();
        if (!w) return;
        float x = base_x, y = base_y;
        if (!stopped) {
            phase = int(float(phase) + stride) % 360;
            const float s = std::sin(float(phase) * 0.017453292f);
            x = base_x + s * range_x, y = base_y - std::fabs(s) * range_y;
        }
        w->set_property("x", Value::number(x)), w->set_property("y", Value::number(y));
    }
};

// MAnchorActor(widget, anchor, follow_x, follow_y) (0x278630, update 0x2786D0): the widget kept
// at the distance it had from the anchor when made, along the axes it follows.
struct AnchorActor : Actor {
    std::weak_ptr<LiveWidget> anchor;
    float dx = 0, dy = 0;
    bool follow_x = false, follow_y = false;
    using Actor::Actor;
    void step() override {
        const auto w = widget.lock();
        const auto a = anchor.lock();
        if (!w || !a) return;
        const Rect at = rect_of(*a);
        if (follow_x) w->set_property("x", Value::number(at.x + dx));
        if (follow_y) w->set_property("y", Value::number(at.y + dy));
    }
};

// MMagnifyActor (0x2B9750, update 0x2BA2B0): the rectangle it was made with, grown about its
// middle by s, s stepping 1/60 a picture (`reverse_phase`: shrinking); with `repeat`, back to 1
// at 0.5 or 2.
struct MagnifyActor : Actor {
    Rect home;
    float s = 1, stride = 1.0f / 60.0f;
    bool repeat = false, reverse_phase = false;
    using Actor::Actor;
    void attached(LiveWidget& w) override { home = rect_of(w); }
    void rewind() override {
        s = 1;
        if (const auto w = widget.lock()) set_rect(*w, home);
    }
    void step() override {
        const auto w = widget.lock();
        if (!w) return;
        s += reverse_phase ? -stride : stride;
        if (repeat && (s <= 0.5f || s >= 2)) s = 1;
        const float nw = home.w * s, nh = home.h * s;
        set_rect(*w, {home.x + (home.w - nw) / 2, home.y + (home.h - nh) / 2, nw, nh});
    }
};

// MButtonActor (0x27E748, update 0x27E978): while the widget is active its rectangle pulses
// (1 + 0.15 v about its middle) and its child `Active` shows v, v falling from 1 to 0 over
// `period` (1/2 s) and starting again.
struct ButtonActor : Actor {
    Rect home;
    float active_alpha = 1, t = 0, stride = 1.0f / 30.0f;
    using Actor::Actor;
    void attached(LiveWidget& w) override {
        home = rect_of(w);
        if (const auto a = w.child("Active")) active_alpha = a->data.number("alpha", 1);
    }
    void step() override {
        const auto w = widget.lock();
        if (!w) return;
        const auto face = w->child("Active");
        if (!(w->state & kActive)) {
            if (t != 0) {
                t = 0, set_rect(*w, home);
                if (face) face->set_property("alpha", Value::number(active_alpha));
            }
            return;
        }
        t += stride;
        if (t > 1) t = 0;
        const float v = 1 - t, k = 1 + 0.15f * v;
        set_rect(*w, {home.x + home.w * (1 - k) / 2, home.y + home.h * (1 - k) / 2, home.w * k, home.h * k});
        if (face) face->set_property("alpha", Value::number(v));
    }
};

// MTextActor (0x2E4358, update 0x2E4390): a text shown a letter more each picture from
// `start_point`, then whole.
struct TextActor : Actor {
    int start_point = 0, end_point = 0, length = 0;
    std::string whole;
    using Actor::Actor;
    void attached(LiveWidget& w) override {
        whole = w.data.text("text");
        length = int(whole.size());
    }
    void step() override {
        const auto w = widget.lock();
        if (!w) return;
        if (start_point == -1 && end_point == -1) return;
        const int from = std::clamp(start_point, 0, length), to = std::clamp(std::max(start_point, end_point), 0, length);
        w->set_property("text", Value::string(whole.substr(size_t(from), size_t(to - from))));
        ++end_point;
        if (start_point == 0 && end_point >= length) {
            start_point = end_point = -1;
            w->set_property("text", Value::string(whole));
        }
    }
};

// MSwitchActor (0x2E2908, update 0x2E2D18; Prologue's license buttons): t toward 1 (`start`)
// or 0 (`end`) by 0.1 a picture; the button pushed out from its parent's middle by
// k = 1 + 0.6 (1 - t^2), its children `base` and `text` at alpha t, `ring` too unless the
// button is insensitive. ❓ The children's colours by the state are not written.
struct SwitchActor : Actor {
    Rect home;
    float t = 0, stride = 0.1f, period = 0;
    using Actor::Actor;
    void attached(LiveWidget& w) override { home = rect_of(w); }
    void step() override {
        const auto w = widget.lock();
        if (!w) return;
        t = std::clamp(t + stride, 0.0f, 1.0f);
        const LiveWidget* up = w->parent_widget();
        const Rect parent = up ? rect_of(*up) : Rect{0, 0, 640, 480};
        const float pw = parent.w / 2, ph = parent.h / 2, k = 1 + (1 - t * t) * 0.6f;
        w->set_property("x", Value::number((home.x + home.w / 2 - pw) * k + pw - home.w / 2));
        w->set_property("y", Value::number((home.y + home.h / 2 - ph) * k + ph - home.h / 2));
        for (const char* name : {"base", "text", "ring"})
            if (const auto c = w->child(name))
                if (std::string(name) != "ring" || !w->insensitive()) c->set_property("alpha", Value::number(t));
    }
};


template <class T> void number_field(Object& o, const std::string& name, T* field, std::function<void()> changed = {}) {
    o.property(name, [field] { return Value::number(float(*field)); }, [field, changed](const Value& v) {
        if (v.is_number()) *field = T(v.as_float());
        if (changed) changed();
    });
}

void flag_field(Object& o, const std::string& name, bool* field, std::function<void()> changed = {}) {
    o.property(name, [field] { return Value::integer(*field); }, [field, changed](const Value& v) {
        *field = v.truthy();
        if (changed) changed();
    });
}

void common_methods(const std::shared_ptr<Actor>& a) {
    std::weak_ptr<Actor> weak = a;
    a->method("rewind", [weak](Vm&, const Value&, Args) {
        if (auto s = weak.lock()) s->rewind();
        return Value{};
    });
    a->method("initialize", [weak](Vm&, const Value&, Args) {
        if (auto s = weak.lock()) s->rewind();
        return Value{};
    });
    a->set("name", Value::string({}));
}

// ---------------------------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------------------------

// The transitions (mTransition, 0x24D1B8 on). `state` 0 going out, 1 out (waiting for the
// page), 2 coming in, 3 done; `mode` says which of panOut, panIn, panOutIn started it.
// MColorTransition (update 0x287348): a colour covers the page by 1/15 a picture and uncovers
// it. MCrossTransition (0x28A6B0, 0x28A5B8): its out half is done at once; coming in, t falls
// from 1 by 1/30 a picture and every page is drawn at opacity 1 - t, grown by 1 + t^2 about its
// middle (one more picture at t = 0). MBlockTransition (0x27B2A8, 0x27AEE0): the screen as it
// was is held in 32 x 24 blocks; coming in, each block's timer falls by 2/15 a picture from
// 1 + a random part of `range` + its column's delay (`slope`), and a block shows at alpha
// min(timer, 1).
struct Transition : Object {
    enum { kOut = 0, kIn = 1, kOutIn = 2, kNone = 3 };
    enum Kind { kColor, kCross, kBlock } kind = kColor;
    int mode = kNone, state = 3;
    bool active = false;
    float cover = 0, stride = 1.0f / 15.0f;
    float t = 0;      // cross: the in half's progress, 1 to 0
    bool at_end = false;
    float range = 0.125f, slope = 2;
    std::vector<float> timers; // block: 32 x 24, row by row
    bool capture_wanted = false;
    u32 seed = 0x12345678;

    using Object::Object;
    float random() {
        seed = seed * 1103515245u + 12345u; // ❓ the program's generator (0x5792F0) is not read
        return float((seed >> 8) & 0xFFFF) / 65536.0f;
    }
    void start(int how) {
        active = true, mode = how, at_end = false;
        if (kind == kColor) {
            state = how == kIn ? 1 : 0, cover = how == kIn ? 1.0f : 0.0f;
        } else if (kind == kCross) {
            state = how == kIn ? 1 : 0, t = how == kOut ? 0.0f : 1.0f;
        } else {
            state = how == kIn ? 1 : 0;
            if (how != kIn) {
                timers.assign(32 * 24, 0);
                for (int r = 0; r < 24; ++r)
                    for (int c = 0; c < 32; ++c) {
                        const float delay = slope >= 0 ? float(c) / 32.0f * slope : float(32 - c) / 32.0f * -slope;
                        timers[size_t(r * 32 + c)] = 1 + random() * range + delay;
                    }
                capture_wanted = true;
            } else timers.clear();
        }
    }
    void step() {
        if (!active) return;
        if (kind == kColor) {
            if (state == 0) {
                cover += stride;
                if (cover > 1) cover = 1, state = 1;
            } else if (state == 1) {
                if (mode == kOut) active = false, state = 3; // stays covered: the scripts leave the project
                else state = 2;
            } else if (state == 2) {
                cover -= stride;
                if (cover <= 0) cover = 0, state = 3, active = false;
            }
            return;
        }
        if (state == 0) {
            state = 1;
        } else if (state == 1) {
            if (mode == kOut) active = false, state = 3;
            else state = 2;
        } else if (state == 2 && kind == kCross) {
            t -= stride;
            if (t < 0) {
                t = 0;
                if (at_end) state = 3, active = false;
                at_end = true;
            }
        } else if (state == 2) {
            bool any = false;
            for (float& timer : timers)
                if (timer > 0) timer -= stride, any = any || timer > 0;
            if (!any) state = 3, active = false, timers.clear();
        }
    }
};

// MScriptWatcher(context, function) (Prologue's: (context, module, function's name)): the function is called every `interval` frames while the
// watcher is appended; `count` is how often it has been.
struct Watcher : Object {
    Value function, context;
    int interval = 1, frames = 0, count = 0;
    bool queued = false;
    using Object::Object;
};

} // namespace

void attach_actor(LiveWidget& w, const Value& value) {
    const auto actor = value.as<Actor>();
    w.actor = actor;
    if (!actor) return;
    actor->widget = w.self();
    actor->attached(w);
    if (w.runtime) w.runtime->track_actor(actor);
}

void step_actor(script::Heap& actor) {
    if (auto* a = dynamic_cast<Actor*>(&actor)) {
        const auto w = a->widget.lock();
        if (w && w->actor.get() == a) a->step(); // only while it is the widget's actor
    }
}

bool step_watcher(Object& o) {
    auto* w = dynamic_cast<Watcher*>(&o);
    if (!w || w->queued) return false;
    if (++w->frames < std::max(w->interval, 1)) return false;
    w->frames = 0;
    w->queued = true; // one call at a time: the next is counted from when this one was made
    return true;
}

float transition_cover(const Value& transition) {
    const auto t = transition.as<Transition>();
    return t && t->kind == Transition::kColor ? std::clamp(t->cover, 0.0f, 1.0f) : 0.0f;
}

TransitionPicture transition_picture(const Value& transition) {
    TransitionPicture p;
    const auto t = transition.as<Transition>();
    if (!t || !t->active) return p;
    if (t->kind == Transition::kColor) p.cover = std::clamp(t->cover, 0.0f, 1.0f);
    else if (t->kind == Transition::kCross && t->state == 2) {
        p.page_opacity = std::clamp(1 - t->t, 0.0f, 1.0f);
        p.page_scale = std::clamp(1 + t->t * t->t, 1.0f, 2.0f);
    } else if (t->kind == Transition::kBlock) {
        p.blocks = &t->timers;
        p.capture = t->capture_wanted;
        t->capture_wanted = false;
    }
    return p;
}

int transition_phase(const Value& transition) {
    const auto t = transition.as<Transition>();
    return t && t->active ? t->state : -1;
}

void step_transition(const Value& transition) {
    if (const auto t = transition.as<Transition>()) t->step();
}

void install_actor_natives(Runtime& rt, script::Module& menu) {
    Runtime* r = &rt;
    auto widget_of = [](Args a) { return a.empty() ? nullptr : a[0].as<LiveWidget>(); };

    menu.define("MFadeActor", Vm::native("MFadeActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<FadeActor>("MFadeActor");
        act->widget = widget_of(a);
        act->source = arg_number(a, 1, 1), act->destination = arg_number(a, 2, 0);
        // As the program's constructor leaves it: at the destination, coming in.
        act->out = true, act->warp(), act->out = false;
        FadeActor* p = act.get();
        common_methods(act);
        act->method("warp", [p](Vm&, const Value&, Args) {
            p->warp();
            return Value{};
        });
        number_field(*act, "slope", &p->slope);
        act->property("velocity", [p] { return Value::number(p->velocity); }, [p](const Value& v) { p->velocity = std::fabs(v.as_float()); });
        act->property("period", [p] { return Value::number(std::fabs(p->destination - p->source) / std::max(p->velocity * 60.0f, 1e-6f)); },
                      [p](const Value& v) {
                          const float seconds = v.as_float() <= 0 ? 1.0f : v.as_float();
                          p->velocity = std::fabs(p->destination - p->source) / (seconds * 60.0f);
                      });
        act->property("over", [p] { return Value::number(p->src_over); }, [p](const Value& v) { p->src_over = p->dest_over = v.as_float(); });
        number_field(*act, "src_over", &p->src_over);
        number_field(*act, "dest_over", &p->dest_over);
        act->property("out", [p] { return Value::integer(p->out); }, [p](const Value& v) { p->out = v.truthy(), p->done = false; });
        flag_field(*act, "repeat", &p->repeat);
        return object_value(act);
    }));

    menu.define("MMoveActor", Vm::native("MMoveActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<MoveActor>("MMoveActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        const Rect at = w && w->data.rect("geometry") ? *w->data.rect("geometry") : Rect{};
        act->source[0] = act->now[0] = at.x, act->source[1] = act->now[1] = at.y;
        act->destination[0] = arg_number(a, 1, at.x), act->destination[1] = arg_number(a, 2, at.y);
        act->destination[2] = act->destination[3] = arg_number(a, 3, 1);
        MoveActor* p = act.get();
        common_methods(act);
        act->method("warp", [p](Vm&, const Value&, Args) {
            p->warp();
            return Value{};
        });
        act->method("doFlip", [p](Vm&, const Value&, Args) {
            p->out = !p->out;
            return Value{};
        });
        number_field(*act, "destinationX", &p->destination[0]);
        number_field(*act, "destinationY", &p->destination[1]);
        number_field(*act, "ratio", &p->ratio);
        flag_field(*act, "out", &p->out);
        flag_field(*act, "repeat", &p->repeat);
        flag_field(*act, "linear", &p->linear);
        flag_field(*act, "flip", &p->flip);
        return object_value(act);
    }));

    menu.define("MBlinkActor", Vm::native("MBlinkActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<BlinkActor>("MBlinkActor");
        act->widget = widget_of(a);
        if (const auto w = act->widget.lock()) act->shown_before = w->data.number("alpha", 1);
        BlinkActor* p = act.get();
        common_methods(act);
        flag_field(*act, "repeat", &p->repeat);
        flag_field(*act, "reverse_phase", &p->reverse_phase);
        flag_field(*act, "wave_form", &p->wave_form);
        act->property("period", [p] { return Value::number((p->destination - p->source) / std::max(p->stride * 60.0f, 1e-6f)); },
                      [p](const Value& v) { p->set_period(v.as_float()); });
        number_field(*act, "destination", &p->destination);
        number_field(*act, "source", &p->source);
        number_field(*act, "flick", &p->flick);
        return object_value(act);
    }));

    menu.define("MRotateActor", Vm::native("MRotateActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<RotateActor>("MRotateActor");
        act->widget = widget_of(a);
        act->destination = arg_number(a, 1, 0), act->cw = arg_number(a, 2, 1) != 0;
        act->angle = act->source;
        RotateActor* p = act.get();
        common_methods(act);
        act->method("warp", [p](Vm&, const Value&, Args) {
            p->warp();
            return Value{};
        });
        act->method("doFlip", [p](Vm&, const Value&, Args) {
            p->out = !p->out, p->cw = !p->cw;
            return Value{};
        });
        number_field(*act, "destination", &p->destination);
        act->property("velocity", [p] { return Value::number(p->velocity); }, [p](const Value& v) { p->velocity = v.as_float(), p->by_velocity = true; });
        act->property("ratio", [p] { return Value::number(p->ratio); }, [p](const Value& v) { p->ratio = v.as_float(), p->by_velocity = false; });
        flag_field(*act, "out", &p->out);
        flag_field(*act, "cw", &p->cw);
        flag_field(*act, "repeat", &p->repeat);
        flag_field(*act, "flip", &p->flip);
        return object_value(act);
    }));

    menu.define("MChaseActor", Vm::native("MChaseActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<ChaseActor>("MChaseActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        if (w) act->attached(*w);
        ChaseActor* p = act.get();
        common_methods(act);
        act->method("doStart", [p](Vm&, const Value&, Args) {
            p->stopped = false, p->phase = 0;
            return Value{};
        });
        act->method("doStop", [p](Vm&, const Value&, Args) {
            p->stopped = true, p->phase = 0;
            return Value{};
        });
        act->property("period", [p] { return Value::number(p->period); }, [p](const Value& v) {
            p->period = v.as_float();
            p->stride = 360.0f / std::max(p->period * 60.0f, 1e-3f);
        });
        number_field(*act, "floating_range_x", &p->range_x);
        number_field(*act, "floating_range_y", &p->range_y);
        act->set("floating_wait", Value::integer(0));
        return object_value(act);
    }));

    menu.define("MAnchorActor", Vm::native("MAnchorActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<AnchorActor>("MAnchorActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        const std::shared_ptr<LiveWidget> anchor = a.size() > 1 ? a[1].as<LiveWidget>() : nullptr;
        act->widget = w, act->anchor = anchor;
        act->follow_x = arg_number(a, 2, 0) != 0, act->follow_y = arg_number(a, 3, 0) != 0;
        if (w && anchor) {
            const Rect at = rect_of(*w), to = rect_of(*anchor);
            act->dx = at.x - to.x, act->dy = at.y - to.y;
        }
        common_methods(act);
        return object_value(act);
    }));

    menu.define("MMagnifyActor", Vm::native("MMagnifyActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<MagnifyActor>("MMagnifyActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        if (w) act->attached(*w);
        MagnifyActor* p = act.get();
        common_methods(act);
        flag_field(*act, "repeat", &p->repeat);
        flag_field(*act, "reverse_phase", &p->reverse_phase);
        // The script's setter passes 1 for any period but 0 (0x2B9DB0): one second.
        act->property("period", [p] { return Value::number((1.0f / 60.0f) / p->stride); }, [p](const Value& v) {
            p->stride = (1.0f / 60.0f) / (v.as_float() != 0 ? 1.0f : 1e-3f);
        });
        return object_value(act);
    }));

    menu.define("MButtonActor", Vm::native("MButtonActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<ButtonActor>("MButtonActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        if (w) act->attached(*w);
        ButtonActor* p = act.get();
        common_methods(act);
        act->property("period", [p] { return Value::number((1.0f / 60.0f) / p->stride); },
                      [p](const Value& v) { p->stride = (1.0f / 60.0f) / std::max(v.as_float(), 1e-3f); });
        return object_value(act);
    }));

    menu.define("MTextActor", Vm::native("MTextActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<TextActor>("MTextActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        if (w) act->attached(*w);
        TextActor* p = act.get();
        common_methods(act);
        number_field(*act, "start_point", &p->start_point);
        number_field(*act, "end_point", &p->end_point);
        return object_value(act);
    }));

    menu.define("MSwitchActor", Vm::native("MSwitchActor", [widget_of](Vm&, const Value&, Args a) {
        auto act = std::make_shared<SwitchActor>("MSwitchActor");
        const std::shared_ptr<LiveWidget> w = widget_of(a);
        act->widget = w;
        if (w) act->attached(*w);
        SwitchActor* p = act.get();
        common_methods(act);
        act->method("start", [p](Vm&, const Value&, Args) {
            p->stride = 0.1f;
            return Value{};
        });
        act->method("end", [p](Vm&, const Value&, Args) {
            p->stride = -0.1f;
            return Value{};
        });
        act->method("warp", [p](Vm&, const Value&, Args) {
            p->t = 1;
            return Value{};
        });
        number_field(*act, "period", &p->period);
        return object_value(act);
    }));

    // The transitions: period gives the step a picture, 1 / (period x 60) (colour 1/15, cross
    // 1/30, block 2/15 when not set).
    for (const char* name : {"MColorTransition", "MCrossTransition", "MBlockTransition"})
        menu.define(name, Vm::native(name, [r, name](Vm&, const Value&, Args) {
            auto t = std::make_shared<Transition>(name);
            Transition* p = t.get();
            const std::string kind = name;
            p->kind = kind == "MCrossTransition" ? Transition::kCross : kind == "MBlockTransition" ? Transition::kBlock : Transition::kColor;
            p->stride = p->kind == Transition::kCross ? 1.0f / 30.0f : p->kind == Transition::kBlock ? 2.0f / 15.0f : 1.0f / 15.0f;
            auto start = [p](int mode) {
                return [p, mode](Vm&, const Value&, Args) {
                    p->start(mode);
                    return Value{};
                };
            };
            t->method("panOut", start(Transition::kOut));
            t->method("panIn", start(Transition::kIn));
            t->method("panOutIn", start(Transition::kOutIn));
            auto wait_while = [p](std::function<bool(int)> in_state) {
                return [p, in_state](Vm& vm, const Value&, Args) {
                    if (p->active && in_state(p->state)) vm.wait([p, in_state] { return !p->active || !in_state(p->state); });
                    return Value{};
                };
            };
            t->method("syncOut", wait_while([](int s) { return s == 0; }));
            t->method("syncWait", wait_while([](int s) { return s == 1; }));
            t->method("syncIn", wait_while([](int s) { return s == 1 || s == 2; }));
            t->property("period", [p] { return Value::number(1.0f / std::max(p->stride * 60.0f, 1e-6f)); },
                        [p](const Value& v) { p->stride = 1.0f / std::max(v.as_float() * 60.0f, 1e-3f); });
            number_field(*t, "range", &p->range);
            number_field(*t, "slope", &p->slope);
            // ❓ No script sets these: the cross transition's capture type and trail.
            for (const char* f : {"type", "magnify_ratio", "magnify_diff", "with_black"}) t->set(f, Value::integer(0));
            (void)r;
            return object_value(t);
        }));

    menu.define("MScriptWatcher", Vm::native("MScriptWatcher", [r](Vm& vm, const Value&, Args a) {
        auto w = std::make_shared<Watcher>("MScriptWatcher");
        w->context = a.empty() ? Value{} : a[0];
        w->function = a.size() > 1 ? a[1] : Value{};
        // Prologue's scripts name the function by its module and name: (context, module, "onCheck").
        if (a.size() > 2 && a[2].kind == script::Kind::String) w->function = vm.attribute(a[1], a[2].as_string());
        Watcher* p = w.get();
        std::weak_ptr<Watcher> weak = w;
        number_field(*w, "interval", &p->interval);
        number_field(*w, "count", &p->count);
        w->method("append", [r, weak](Vm&, const Value&, Args) {
            if (auto s = weak.lock()) s->frames = 0, r->add_watcher(s);
            return Value{};
        });
        w->method("remove", [r, p](Vm&, const Value&, Args) {
            r->remove_watcher(p);
            return Value{};
        });
        return object_value(w);
    }));
}

// The runtime's side of a watcher that is due: its function and argument, and the count.
bool watcher_call(Object& o, Value& function, Value& context) {
    auto* w = dynamic_cast<Watcher*>(&o);
    if (!w) return false;
    function = w->function, context = w->context;
    return true;
}

void watcher_called(Object& o) {
    if (auto* w = dynamic_cast<Watcher*>(&o)) w->queued = false, ++w->count;
}

} // namespace gt4r::menu
