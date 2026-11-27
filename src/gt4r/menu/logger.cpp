// The Analyzer: MLoggerControl (0x11F1E0) and its face's pad. docs/formats/logger.md.

#include "gt4r/menu/logger.h"

#include "base/log.h"

#include "gt4r/menu/game.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/runtime.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/sim/car_spec.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace gt4r::menu {

using script::Args;
using script::Kind;
using script::Value;
using script::Vm;

namespace {

float f32(u32 bits) { return std::bit_cast<float>(bits); }

// The program's float to int (cvt.w.s: toward zero), for a value that is not negative.
s32 whole(float v) { return v >= 2147483648.0f ? s32(0x7FFFFFFF) : s32(v); }

// The value of the line through (a, va) and (b, vb) at x (0x11E310).
float line_at(float a, float b, float va, float vb, float x) { return ((vb - va) * x + (va * b - vb * a)) * (1.0f / (b - a)); }

// The area under that line between lo and hi, its part within a .. b (0x11E348).
float area(float a, float b, float va, float vb, float lo, float hi) {
    if (b < lo || hi < a) return 0;
    const float slope = vb - va, cut = va * b - vb * a;
    if (a < lo || hi < b) {
        const float inv = 1.0f / (b - a);
        if (a < lo) va = (slope * lo + cut) * inv, a = lo;
        if (hi < b) vb = (slope * hi + cut) * inv, b = hi;
    }
    return (va + vb) * (b - a) * 0.5f;
}

} // namespace

std::string logger_time_text(s64 ms) {
    if (ms < 0 || ms >= 359999999) return "\x7F'\x7F\x7F.\x7F\x7F\x7F";
    char b[40];
    if (ms >= 3600000) std::snprintf(b, sizeof b, "%d:%02d'%02d.%03d", int(ms / 3600000), int(ms / 60000 % 60), int(ms / 1000 % 60), int(ms % 1000));
    else std::snprintf(b, sizeof b, "%d'%02d.%03d", int(ms / 60000), int(ms / 1000 % 60), int(ms % 1000));
    return b;
}

std::string logger_tenths(int v) {
    char b[32];
    std::snprintf(b, sizeof b, "%s%d.%d", v < 0 ? "-" : "", std::abs(v) / 10, std::abs(v) % 10);
    return b;
}

// A picture of the car (0x11E450): its speed, throttle and brake added up over each sample's metres; a sample made each
// time the car passes one's end.
void Logger::Sampler::sample(const LoggerPicture& p) {
    if (out->size() >= kSamples) return;
    float d = p.distance;
    const float v = p.speed, t = p.throttle, b = p.brake;
    top = std::max(top, v);
    auto keep = [&] { prev_d = d, prev_v = v, prev_t = t, prev_b = b; };
    if (first) {
        start = far = d, first = false;
        return keep();
    }
    if (d <= far) {
        d = far;
        return keep();
    }
    far = d;
    const float cur = d - start, was = prev_d - start;
    float lo = float(out->size()) * spacing, hi = lo + spacing;
    sum_v += area(was, cur, prev_v, v, lo, hi), sum_t += area(was, cur, prev_t, t, lo, hi), sum_b += area(was, cur, prev_b, b, lo, hi);
    while (hi <= cur) {
        LoggerSample s;
        const float sv = std::min(sum_v * 40.0f * inv + 0.5f, f32(0x45FFF800)); // (8191)
        const float st = std::min(sum_t * 255.0f * inv + 0.5f, 255.0f), sb = std::min(sum_b * 255.0f * inv + 0.5f, 255.0f);
        s.speed_gear = u16((u32(whole(sv)) << 3) | (u32(p.gear) & 7));
        s.throttle = u8(whole(st)), s.brake = u8(whole(sb));
        out->push_back(s);
        if (out->size() >= kSamples) return; // (the program keeps no picture then)
        lo += spacing, hi += spacing;
        sum_v = area(was, cur, prev_v, v, lo, hi), sum_t = area(was, cur, prev_t, t, lo, hi), sum_b = area(was, cur, prev_b, b, lo, hi);
    }
    keep();
}

void Logger::analyze_start() {
    // (0x123368: running, not done, no progress; the thread 0x122FA8)
    running = true, analyzed = false, progress_at = progress_of = 0;
    track[0].clear(), track[1].clear();
    for (int t = 0; t < 2; ++t) {
        laps_[size_t(t)].reset(), done_[size_t(t)] = true, at_[size_t(t)] = 0;
        has[size_t(t)] = source && source->info[size_t(t)].has;
        ms[size_t(t)] = source ? source->info[size_t(t)].ms : -1;
    }
    // The spacing: the course's length / 16383, at least 0.5 m (0x395400).
    const float len = source ? source->course_length : 0;
    spacing = std::max(len / f32(0x467FFC00), 0.5f);
    for (int t = 0; t < 2; ++t) {
        // An empty Track B samples entry 0 again: its samples are A's.
        if (!has[size_t(t)] || !source || !source->lap) continue;
        laps_[size_t(t)] = source->lap(t);
        if (!laps_[size_t(t)] || !laps_[size_t(t)]->next) {
            laps_[size_t(t)].reset();
            continue;
        }
        Sampler& s = samplers_[size_t(t)];
        s = Sampler{};
        s.out = &track[size_t(t)], s.spacing = spacing, s.inv = 1.0f / spacing, s.length = len;
        done_[size_t(t)] = false;
        progress_of = std::max(progress_of, laps_[size_t(t)]->frames);
    }
    if (done_[0] && done_[1]) running = false; // nothing to run
    LOG_INFO("Analyzer: analysis of Track A %s, Track B %s (%zu pictures, a sample every %.3f m)", done_[0] ? "empty" : "run", done_[1] ? "empty" : "run",
             progress_of, double(spacing));
}

void Logger::analyze_stop() {
    if (!running) return;
    running = false;
    laps_[0].reset(), laps_[1].reset();
}

bool Logger::analyze_step(double budget) {
    if (!running) return false;
    const auto t0 = std::chrono::steady_clock::now();
    for (int n = 0; !(done_[0] && done_[1]); ++n) {
        if ((n & 63) == 63 && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > budget) return false;
        for (size_t t = 0; t < 2; ++t) {
            if (done_[t]) continue;
            LoggerPicture p;
            if (!laps_[t]->next(p)) {
                done_[t] = true;
                continue;
            }
            ++at_[t];
            samplers_[t].sample(p);
        }
        progress_at = std::max(at_[0], at_[1]);
    }
    finish();
    return true;
}

// The analysis's end (0x122FA8 after its loop, 0x122D38).
void Logger::finish() {
    laps_[0].reset(), laps_[1].reset();
    running = false;
    if (!has[1]) track[1] = track[0], samplers_[1] = samplers_[0];
    if (!has[0] && has[1]) track[0] = track[1], samplers_[0] = samplers_[1];
    const size_t n = std::min(track[0].size(), track[1].size());
    track[0].resize(n), track[1].resize(n);
    start = std::min(samplers_[0].start, samplers_[1].start);
    const float len = n > 0 ? float(s32(n) - 1) * spacing : 0.0f; // (0x11DBB0)
    end = start + len;
    const float fastest = std::max(samplers_[0].top, samplers_[1].top);
    speed_top = float(whole(std::min(800.0f, fastest * f32(0x40666666) + 100.0f)));
    scale_v = std::max(1.0f, float(whole(fastest * 9.0f / 250.0f)));
    length = end - start;
    middle = length * 0.5f;
    scale_m = length > 0 ? std::clamp(width / length, f32(0x3ECCCCCD), 10.0f) : 10.0f;
    scroll = 0;
    analyzed = true;
    LOG_INFO("Analyzer: %zu samples, %.1f m, top %.1f km/h, the speed graph's top %.0f, scale %.0f", n, double(length), double(fastest * 3.6f),
             double(speed_top), double(scale_v));
}

float Logger::speed_at(int t, float metres) const {
    const std::vector<LoggerSample>& s = track[size_t(t)];
    if (s.empty()) return 0;
    const float last = float(s32(s.size()) - 1) * spacing;
    metres = std::min(std::max(metres, 0.0f), last);
    const size_t i = std::min(size_t(whole(metres / spacing)), s.size() - 1);
    return s[i].speed();
}

float Logger::graph_height() const {
    // (0x1233E0: the pedal bands, the gap, the speed graph and 75 above it)
    float bands = 0;
    if (pedals()) bands = float(int(shown(0)) + int(shown(1))) * 50.0f + 4.0f;
    return bands + 10.0f + speed_top / scale_v + 75.0f;
}

void Logger::input(const float sticks[4]) {
    // (0x1206A0) The pad's analog values: 2, 3 the left stick, 0, 1 the right.
    const float zoom_m = sticks[2], zoom_v = sticks[3], move = sticks[0], lift = sticks[1];
    auto curve = [](float v) {
        const float c = std::pow(std::fabs(v), 1.5f);
        return v >= 0 ? c : -c;
    };
    const float mx = curve(move), my = curve(lift);
    const float ln10 = f32(0x40135DA2), k = f32(0x3F99999A);
    scale_m += zoom_m * (1.0f / 32.0f) * (std::log(scale_m) / ln10 + k);
    if (scale_m < f32(0x3ECCCCCD)) scale_m = f32(0x3ECCCCCD);
    else if (10.0f < scale_m) scale_m = 10.0f;
    scale_v += zoom_v * (1.0f / 32.0f) * (std::log(scale_v) / ln10 + k);
    if (scale_v < 0.5f) scale_v = 0.5f;
    else if (4.0f < scale_v) scale_v = 4.0f;
    // The middle: stopped at an end, round to the other end when pushed past it again after the stick was let go.
    float x = middle + (mx + mx) / scale_m;
    if (x < 0) {
        if (edge[0] >= 0 && edge[0] < 2) x = 0, edge[0] = 1;
        else if (edge[0] == 2) x = length, edge[0] = 0;
    } else if (length < x) {
        if (edge[1] >= 0 && edge[1] < 2) x = length, edge[1] = 1;
        else if (edge[1] == 2) x = 0, edge[0] = 0; // (the program clears the other end's here)
    } else if (mx == 0) {
        if (edge[0] == 1) edge[0] = 2;
        if (edge[1] == 1) edge[1] = 2;
    } else edge[0] = edge[1] = 0;
    middle = x;
    const float most = std::max(graph_height() - height, 0.0f);
    scroll = std::min(std::max(scroll - (my + my), 0.0f), most);
}

std::array<u8, 5>& logger_display(Runtime& rt) { return rt.game().logger_display; }

// ---- The script class.

namespace {

struct LoggerControl : Object {
    Logger logger;
    LoggerControl() : Object("MLoggerControl") {}
};

std::shared_ptr<LiveWidget> widget_in(const Value& v) { return v.p ? std::dynamic_pointer_cast<LiveWidget>(v.p) : nullptr; }

} // namespace

// A value the face keeps (its control, progress, grab: widget_class_member).
Value face_value(const LiveWidget& face, const std::string& name) {
    const auto it = face.extras.find(name);
    if (it == face.extras.end() || !it->second) return {};
    if (auto v = std::dynamic_pointer_cast<script::Variable>(it->second)) return v->value;
    return {};
}

namespace {

std::string speed_text(Runtime& rt, float ms) {
    // (0x472940: km/h v x 3.6, mph x 0.6214; 0x472680 the unit's word)
    const GameState::Unit u = rt.game().velocity_unit();
    char b[32];
    std::snprintf(b, sizeof b, "%3d%s", whole(std::max(ms * f32(0x40666666) * u.factor, 0.0f)), u.name);
    return b;
}

} // namespace

Logger* logger_of_face(const LiveWidget& face) {
    const Value c = face_value(face, "control");
    if (c.kind != Kind::Object || !c.p) return nullptr;
    auto* l = dynamic_cast<LoggerControl*>(c.p.get());
    return l ? &l->logger : nullptr;
}

void install_logger_natives(Runtime& rt, script::Module& menu) {
    auto cls = std::make_shared<script::Module>();
    cls->name = "MLoggerControl", cls->is_class = true;
    Runtime* r = &rt;
    cls->construct = [r](Vm&, Args) {
        auto o = std::make_shared<LoggerControl>();
        Logger* L = &o->logger;
        L->source = r->game().logger;
        // (0x11EA90: the units by the speed option, 0x6244D8)
        L->metric = std::string(r->game().velocity_unit().name) != "mph";
        L->grid = L->metric ? 25.0f : f32(0x41C92AE1);
        L->line_factor = L->metric ? 1.0f : f32(0x3FCDFEDA);
        L->display = &logger_display(*r);
        for (int t = 0; t < 2; ++t) L->has[size_t(t)] = L->source && L->source->info[size_t(t)].has;
        o->method("analyze_start", [L](Vm&, const Value&, Args) { return L->analyze_start(), Value{}; });
        o->method("analyze_stop", [L](Vm&, const Value&, Args) { return L->analyze_stop(), Value{}; });
        auto info = [L](Args a) -> const LoggerTrackInfo* {
            const s32 t = arg_int(a, 0, -1);
            if (t < 0 || t > 1 || !L->source || !L->source->info[size_t(t)].has) return nullptr;
            return &L->source->info[size_t(t)];
        };
        o->method("getCarName", [r, info](Vm& vm, const Value&, Args a) {
            const LoggerTrackInfo* i = info(a);
            if (!i || i->car.empty()) return text("");
            return text(vm.attribute(make_car_object(*r, i->car, 0), "short_name").to_string()); // (0x445450)
        });
        o->method("getPower", [r, info](Vm&, const Value&, Args a) {
            const LoggerTrackInfo* i = info(a);
            if (!i) return text("");
            // (0x364E78: the race's car's power; the catalog's when the race gave none; "----" for a car that hides it,
            // GENERIC_CAR +0x12 bit 3, 0x4489A0)
            float ps = float(i->power);
            if (i->power < 0) {
                ps = 0;
                try {
                    if (std::shared_ptr<GameNatives> n = natives_of(*r)) {
                        const std::optional<sim::ShowroomFigures> f = n->db().stock_showroom(i->car);
                        const ByteSpan row = n->db().car_row(i->car);
                        if (f && !(row.size() > 0x12 && (row[0x12] & 8))) ps = float(gt3r::ee::to_int(f->power));
                    }
                } catch (const std::exception&) {
                }
            }
            if (ps <= 0) return text("----");
            const GameState::Unit u = r->game().power_unit();
            return text(std::to_string(whole(ps * u.factor + 0.5f)) + u.name); // ("%d%s", 0x4729A0, 0x4726B8)
        });
        // The faces it writes each picture (+0x8C, +0x94, +0x9C).
        auto faces = [L](std::shared_ptr<LiveWidget>* slot) {
            return [L, slot](Vm&, const Value&, Args a) {
                const s32 t = arg_int(a, 0, -1);
                if (t >= 0 && t < 2 && a.size() > 1) slot[t] = widget_in(a[1]);
                (void)L;
                return Value{};
            };
        };
        o->method("setTotalTime", faces(L->time_face));
        o->method("setSpeedCurrent", faces(L->speed_face));
        o->method("setSpeedAverage", [](Vm&, const Value&, Args) { return Value{}; }); // (no page gives it a face)
        o->method("setCourseMap", [L](Vm&, const Value&, Args a) {
            if (!a.empty()) L->map_face = widget_in(a[0]);
            return Value{};
        });
        o->set("mode", Value::integer(1)); // (0x618718: 1 from the sequence)
        o->property("load_track", [L] { return Value::integer(L->source ? L->source->load_track : -1); }, [L](const Value& v) {
            if (L->source) L->source->load_track = v.kind == Kind::Int ? v.i : v.is_number() ? s32(v.as_float()) : -1;
        });
        o->property("course_label", [L] { return text(L->source ? L->source->course_label : std::string()); });
        o->property("race_mode_label", [L] { return text(L->source ? L->source->race_mode_label : std::string()); });
        o->property("race_mode_subnumber", [L] { return number(L->source ? L->source->race_mode_subnumber : -1); });
        static const char* const kSwitches[] = {"display_TrackA", "display_TrackB", "display_speed", "display_accel", "display_brake"};
        for (size_t k = 0; k < 5; ++k)
            o->property(kSwitches[k], [r, k] { return number(logger_display(*r)[k] != 0); },
                        [r, k](const Value& v) { logger_display(*r)[k] = u8(v.truthy()); });
        return object_value(o);
    };
    menu.define("MLoggerControl", Value::of(Kind::Module, cls));
}

// The face each picture (mLoggerFace::vf70 0x1BC138, 0x1206A0): the analysis's share and its end, the progress, the pad.
void step_logger_face(Runtime& rt, LiveWidget& face) {
    Logger* L = logger_of_face(face);
    if (!L) return;
    if (const std::optional<Rect> g = face.data.rect("geometry")) L->width = float(whole(g->w + 0.5f)), L->height = float(whole(g->h + 0.5f));
    if (L->running) {
        if (L->analyze_step(0.006)) {
            rt.queue_call(face, "onLoggerAnalyzeFinish", {rt.context()});
        } else if (const Value p = face_value(face, "progress"); p.kind == Kind::Object && p.p) {
            // (0x122CC0: the frame of the longer lap over its length)
            if (auto* o = dynamic_cast<Object*>(p.p.get())) o->set("value", Value::number(L->progress_of ? float(L->progress_at) / float(L->progress_of) : 0.0f));
        }
    } else if (face_value(face, "grab").truthy() && rt.current_page() == face.page()) {
        float sticks[4];
        for (int i = 0; i < 4; ++i) sticks[i] = rt.pad().sticks[i];
        L->input(sticks);
    }
    // The tracks' times and speeds at the middle.
    for (int t = 0; t < 2; ++t) {
        if (!L->has[size_t(t)]) continue;
        if (L->time_face[t]) L->time_face[t]->set_property("text", text(logger_time_text(L->analyzed ? L->ms[size_t(t)] : 0)));
        if (L->speed_face[t]) L->speed_face[t]->set_property("text", text(speed_text(rt, L->analyzed ? L->speed_at(t, L->middle) : 0.0f)));
    }
    // The course map's point at the middle and its span the metres shown (its vcalls +0x308 / +0x310).
    if (L->map_face) {
        L->map_face->set_property("logger_point", Value::number(L->middle + L->start));
        L->map_face->set_property("logger_from", Value::number(std::max(L->view_left(), 0.0f) + L->start));
        L->map_face->set_property("logger_to", Value::number(std::min(L->view_right(), L->end - L->start) + L->start));
    }
}

// The face's keys while it has the pad (`grab`; vf72 0x1BC2C0): Triangle or Select (0xFF1B, 0xFF63) the menu, Cross or Start
// (0xFF0D, 0xFF8D) the display's options.
bool logger_face_key(Runtime& rt, LiveWidget& face, Key k) {
    if (!face_value(face, "grab").truthy() || !logger_of_face(face)) return false;
    if (k == Key::Cancel || k == Key::Select) return rt.queue_call(face, "onLoggerMenu", {rt.context()}), true;
    if (k == Key::Accept || k == Key::Start) return rt.queue_call(face, "onLoggerPopup", {rt.context()}), true;
    return false;
}

} // namespace gt4r::menu
