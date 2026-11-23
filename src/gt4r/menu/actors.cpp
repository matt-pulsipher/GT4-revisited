// The program's actors (what moves, fades and blinks a widget each frame), transitions (the
// colour that covers a page while it changes) and watchers (a script function every so many
// frames). The fade, move and blink actors and the colour transition follow the program's own
// routines (docs/formats/menu.md has their addresses); the others are marked there.

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
    void attached(LiveWidget& w) override { shown_before = w.opacity(); }
    void set_period(float seconds) {
        stride = (destination - source) / std::max(seconds * 60.0f, 1e-3f);
        rewind();
    }
    void rewind() override {
        phase = source;
        if (const auto w = widget.lock()) w->set_property("opacity", Value::number(shown_before));
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
        if (const auto w = widget.lock()) w->set_property("opacity", Value::number(std::clamp(v, 0.0f, 1.0f)));
    }
};

// The actors not read from the program yet: they keep what the scripts set and leave the
// widget as it is. ❓
struct IdleActor : Actor {
    using Actor::Actor;
    void step() override {}
};

Value number_or(const Value& v, float fallback) { return v.is_number() ? v : Value::number(fallback); }

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

// MColorTransition: a colour covers the page (out), waits, then uncovers it (in). `state`
// 0 going out, 1 out, 2 coming in, 3 done; what follows the out depends on which of panOut,
// panIn, panOutIn started it.
struct Transition : Object {
    enum { kOut = 0, kIn = 1, kOutIn = 2, kNone = 3 };
    int mode = kNone, state = 3;
    bool active = false;
    float cover = 0, stride = 1.0f / 15.0f;

    using Object::Object;
    void step() {
        if (!active) return;
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
    }
};

// MScriptWatcher(context, function): the function is called every `interval` frames while the
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
    return t ? std::clamp(t->cover, 0.0f, 1.0f) : 0.0f;
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
        if (const auto w = act->widget.lock()) act->shown_before = w->opacity();
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

    // ❓ Not read from the program yet: the attributes their classes register hold what the
    // scripts set; the widget stays as it is.
    struct Idle {
        const char* name;
        std::vector<const char*> fields, methods;
    };
    static const Idle idle[] = {
        {"MRotateActor", {"destination", "velocity", "ratio", "out", "cw", "repeat", "flip"}, {"warp", "doFlip"}},
        {"MChaseActor", {"period", "floating_range_x", "floating_range_y"}, {"doStart", "doStop"}},
        {"MMagnifyActor", {"repeat", "reverse_phase", "period"}, {}},
        {"MSwitchActor", {"period"}, {"start", "end", "warp"}},
        {"MTextActor", {"start_point", "end_point"}, {}},
        {"MButtonActor", {"period"}, {}},
        {"MAnchorActor", {}, {}},
    };
    for (const Idle& kind : idle)
        menu.define(kind.name, Vm::native(kind.name, [widget_of, kind](Vm&, const Value&, Args a) {
            auto act = std::make_shared<IdleActor>(kind.name);
            act->widget = widget_of(a);
            common_methods(act);
            for (const char* f : kind.fields) act->set(f, Value::integer(0));
            for (const char* m : kind.methods) act->method(m, [](Vm&, const Value&, Args) { return Value{}; });
            return object_value(act);
        }));

    // The transitions. MCrossTransition and MBlockTransition cover the page as the colour one
    // does here (❓ their own pictures are not written).
    for (const char* name : {"MColorTransition", "MCrossTransition", "MBlockTransition"})
        menu.define(name, Vm::native(name, [r, name](Vm&, const Value&, Args) {
            auto t = std::make_shared<Transition>(name);
            Transition* p = t.get();
            auto start = [p](int mode, int state, float cover) {
                return [p, mode, state, cover](Vm&, const Value&, Args) {
                    p->active = true, p->mode = mode, p->state = state, p->cover = cover;
                    return Value{};
                };
            };
            t->method("panOut", start(Transition::kOut, 0, 0));
            t->method("panIn", start(Transition::kIn, 1, 1));
            t->method("panOutIn", start(Transition::kOutIn, 0, 0));
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
                        [p](const Value& v) { p->stride = 1.0f / std::max(v.as_float() * 60.0f, 1.0f); });
            for (const char* f : {"type", "magnify_ratio", "magnify_diff", "with_black", "range", "slope"}) t->set(f, Value::integer(0));
            (void)r;
            return object_value(t);
        }));

    menu.define("MScriptWatcher", Vm::native("MScriptWatcher", [r](Vm&, const Value&, Args a) {
        auto w = std::make_shared<Watcher>("MScriptWatcher");
        w->context = a.empty() ? Value{} : a[0];
        w->function = a.size() > 1 ? a[1] : Value{};
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
