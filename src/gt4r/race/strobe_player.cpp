// The movie player: timelines, the scripts (AVM1 bytecode: the operations the race's movies use),
// the buttons' focus. docs/formats/strobe.md "Playing".

#include "gt4r/race/strobe.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <cstdio>
#include <cstdlib>

namespace gt4r::race {

using Value = StrobePlayer::Value;
using Movie = StrobeMovie;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const std::string kKeyObject = "\x01Key";

} // namespace

double Value::to_number() const {
    switch (kind) {
    case Number: return n;
    case Bool: return n;
    case String: {
        char* end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        return end && end != s.c_str() ? v : std::nan("");
    }
    default: return kind == Null ? 0 : std::nan("");
    }
}

bool Value::truthy() const {
    switch (kind) {
    case Number: return n != 0 && !std::isnan(n);
    case Bool: return n != 0;
    case String: return !s.empty() && to_number() != 0; // Flash 5: a string is true when it is a non-zero number
    case ClipRef: case FunctionRef: return true;
    default: return false;
    }
}

std::string Value::to_string() const {
    switch (kind) {
    case Number: {
        if (std::isnan(n)) return "NaN";
        if (n == std::floor(n) && std::fabs(n) < 1e15) return std::to_string(s64(n));
        char b[32];
        std::snprintf(b, sizeof b, "%.15g", n);
        return b;
    }
    case Bool: return n ? "true" : "false";
    case String: return s;
    case Null: return "null";
    default: return "";
    }
}

struct StrobePlayer::Function {
    const Bytes* code = nullptr;
    size_t start = 0, end = 0;
    std::vector<std::string> params;
    std::vector<std::string> pool;
    Clip* scope = nullptr;
};

struct StrobePlayer::Clip {
    enum Kind { SpriteClip, ButtonClip, TextClip } kind = SpriteClip;
    int index = 0; // the sprite's, button's or text field's
    std::string name;
    Clip* parent = nullptr;
    size_t track = 0; // its track in the parent's sprite
    int frame = 0;
    bool playing = true, visible = true;
    std::optional<float> set_x, set_y;  // its place in its parent as a script set it (_x, _y), past its track's
    std::map<std::string, Value> vars; // by lower-case name
    std::map<size_t, std::unique_ptr<Clip>> children; // by track
    std::array<std::unique_ptr<Clip>, 3> states;      // a button's: up, over, down
    std::string text;                                  // a text field's
};

struct StrobePlayer::Impl {
    std::shared_ptr<const Movie> movie;
    Host host;
    std::unique_ptr<Clip> root;
    std::vector<std::unique_ptr<Clip>> gone; // clips taken off the stage (functions may still name them)
    std::deque<std::pair<Clip*, int>> queue; // frame actions to run: clip, action block
    Clip* focus = nullptr;
    int pressed = 0; // frames the focused button shows its down state
    int key_code = 0;
    float clock = 0;
    StrobePlayer* self = nullptr;

    // ---- the stage

    std::unique_ptr<Clip> make(const Movie::Track& t, size_t track, Clip* parent) {
        auto c = std::make_unique<Clip>();
        c->parent = parent, c->track = track, c->name = t.name, c->index = t.index;
        if (t.kind == Movie::kSprite) {
            c->kind = Clip::SpriteClip;
            if (size_t(t.index) >= movie->sprites.size()) return nullptr;
            enter(*c);
            // Its load event (clip event 0x01).
            for (const auto& [mask, block] : t.clip_actions)
                if ((mask & 0x01) && block >= 0) queue.push_back({c.get(), block});
        } else if (t.kind == Movie::kButton) {
            c->kind = Clip::ButtonClip;
            if (size_t(t.index) >= movie->buttons.size()) return nullptr;
            const Movie::Button& b = movie->buttons[t.index];
            for (size_t k = 0; k < 3; ++k)
                if (size_t(b.states[k]) < movie->sprites.size()) {
                    auto s = std::make_unique<Clip>();
                    s->parent = c.get(), s->index = b.states[k];
                    enter(*s);
                    c->states[k] = std::move(s);
                }
        } else {
            c->kind = Clip::TextClip;
            if (size_t(t.index) < movie->texts.size()) c->text = movie->texts[t.index].text;
        }
        return c;
    }

    // The clip's children for its frame, and its frame's actions queued.
    void enter(Clip& c) {
        const Movie::Sprite& sp = movie->sprites[size_t(c.index)];
        for (size_t i = 0; i < sp.tracks.size(); ++i) {
            const Movie::Track& t = sp.tracks[i];
            const bool on = t.first <= c.frame && c.frame < t.end;
            const bool instance = t.kind == Movie::kSprite || t.kind == Movie::kButton || t.kind == Movie::kText;
            auto it = c.children.find(i);
            if (on && instance && it == c.children.end()) {
                if (auto made = make(t, i, &c)) c.children.emplace(i, std::move(made));
            } else if (!on && it != c.children.end()) {
                forget(*it->second);
                gone.push_back(std::move(it->second));
                c.children.erase(it);
            }
        }
        for (const auto& [frame, block] : sp.frame_actions)
            if (frame == c.frame) queue.push_back({&c, block});
    }
    void forget(Clip& c) {
        if (focus == &c) focus = nullptr;
        for (auto& [i, ch] : c.children) forget(*ch);
    }

    void go(Clip& c, int frame, bool play) {
        if (c.kind != Clip::SpriteClip) return;
        const int frames = movie->sprites[size_t(c.index)].frames;
        c.frame = std::clamp(frame, 0, frames - 1);
        c.playing = play;
        enter(c);
    }

    void tick(Clip& c) {
        // (Clips placed by this frame start on their first frame: only those there before move on.)
        std::vector<Clip*> kids;
        for (auto& [i, ch] : c.children) kids.push_back(ch.get());
        if (c.kind == Clip::SpriteClip && c.playing) {
            const int frames = movie->sprites[size_t(c.index)].frames;
            if (frames > 1) {
                c.frame = (c.frame + 1) % frames;
                enter(c);
            }
        }
        for (Clip* k : kids)
            if (std::any_of(c.children.begin(), c.children.end(), [k](const auto& e) { return e.second.get() == k; })) tick(*k);
        // The children's frame event (clip event 0x02), every frame.
        if (c.kind == Clip::SpriteClip)
            for (auto& [i, ch] : c.children)
                for (const auto& [mask, block] : movie->sprites[size_t(c.index)].tracks[i].clip_actions)
                    if ((mask & 0x02) && block >= 0) queue.push_back({ch.get(), block});
        for (auto& s : c.states)
            if (s) tick(*s);
    }

    void run_queue() {
        for (int guard = 0; !queue.empty() && guard < 10000; ++guard) {
            const auto [clip, block] = queue.front();
            queue.pop_front();
            if (block >= 0 && size_t(block) < movie->actions.size()) run(movie->actions[size_t(block)], 0, movie->actions[size_t(block)].size(), clip, nullptr, {});
        }
    }

    // ---- names

    Clip* child_named(Clip& c, const std::string& name) {
        const std::string want = lower(name);
        for (auto& [i, ch] : c.children)
            if (lower(ch->name) == want) return ch.get();
        return nullptr;
    }
    static Value clip_value(Clip* c) {
        Value v;
        if (c) v.kind = Value::ClipRef, v.clip = c;
        return v;
    }

    struct Frame {
        Clip* target = nullptr; // `this`, and the timeline the names are on
        std::map<std::string, Value> locals;
        Value regs[4];
        std::vector<std::string> pool;
    };

    Value get_variable(Frame& f, const std::string& raw) {
        // A path: "a.b.c" or "/a/b".
        std::string name = raw;
        for (char& ch : name)
            if (ch == '/') ch = '.';
        if (const size_t dot = name.find('.'); dot != std::string::npos) {
            Value v = name[0] == '.' ? clip_value(root.get()) : get_variable(f, name.substr(0, dot));
            size_t at = name[0] == '.' ? 1 : dot + 1;
            while (at <= name.size()) {
                const size_t next = std::min(name.find('.', at), name.size());
                if (next > at) v = get_member(v, name.substr(at, next - at));
                at = next + 1;
            }
            return v;
        }
        const std::string n = lower(name);
        if (n == "_root") return clip_value(root.get());
        if (n == "this") return clip_value(f.target);
        if (n == "_parent") return clip_value(f.target ? f.target->parent : nullptr);
        if (n == "key") return Value::string(kKeyObject);
        if (const auto it = f.locals.find(n); it != f.locals.end()) return it->second;
        for (Clip* c = f.target; c; c = nullptr) {
            if (const auto it = c->vars.find(n); it != c->vars.end()) return it->second;
            if (Clip* ch = child_named(*c, name)) return clip_value(ch);
        }
        return {};
    }
    void set_variable(Frame& f, const std::string& name, const Value& v) {
        const std::string n = lower(name);
        if (const auto it = f.locals.find(n); it != f.locals.end()) {
            it->second = v;
            return;
        }
        if (const size_t dot = name.rfind('.'); dot != std::string::npos) {
            set_member(get_variable(f, name.substr(0, dot)), name.substr(dot + 1), v);
            return;
        }
        if (f.target) f.target->vars[n] = v;
    }
    // A clip's place in its parent: as a script set it, else its track's now.
    float place_in(const Clip& c, bool x) const {
        if (x ? c.set_x.has_value() : c.set_y.has_value()) return x ? *c.set_x : *c.set_y;
        if (!c.parent || c.parent->kind != Clip::SpriteClip) return 0;
        const Movie::Key& key = movie->sprites[size_t(c.parent->index)].tracks[c.track].key_at(c.parent->frame);
        return x ? key.x : key.y;
    }
    // The properties of GetProperty / SetProperty by number (Flash's): those the race's movies use.
    static const char* property(int index) { return index == 0 ? "_x" : index == 1 ? "_y" : index == 7 ? "_visible" : index == 13 ? "_name" : ""; }
    Value get_member(const Value& o, const std::string& name) {
        if (o.kind == Value::String && o.s == kKeyObject) { // (the Key object's codes)
            static const std::map<std::string, int> kCodes{{"left", 0x25}, {"up", 0x26}, {"right", 0x27}, {"down", 0x28}, {"space", 0x20}, {"enter", 0x0D}, {"escape", 0x1B}, {"shift", 0x10}};
            const auto it = kCodes.find(lower(name));
            return it != kCodes.end() ? Value::number(it->second) : Value{};
        }
        if (o.kind == Value::ArrayRef && o.array) {
            if (lower(name) == "length") return Value::number(double(o.array->size()));
            const double at = Value::string(name).to_number();
            return at >= 0 && at < double(o.array->size()) ? (*o.array)[size_t(at)] : Value{};
        }
        if (o.kind != Value::ClipRef || !o.clip) return {};
        Clip& c = *o.clip;
        const std::string n = lower(name);
        if (n == "_x" || n == "_y") return Value::number(place_in(c, n == "_x"));
        if (n == "_name") return Value::string(c.name);
        if (n == "_parent") return clip_value(c.parent);
        if (n == "_root") return clip_value(root.get());
        if (n == "text" && c.kind == Clip::TextClip) return Value::string(c.text);
        if (n == "_currentframe") return Value::number(c.frame + 1);
        if (n == "_visible") return Value::boolean(c.visible);
        if (const auto it = c.vars.find(n); it != c.vars.end()) return it->second;
        if (Clip* ch = child_named(c, name)) return clip_value(ch);
        return {};
    }
    void set_member(const Value& o, const std::string& name, const Value& v) {
        if (o.kind == Value::ArrayRef && o.array) {
            if (const double at = Value::string(name).to_number(); at >= 0 && at < 1000) {
                if (at >= double(o.array->size())) o.array->resize(size_t(at) + 1);
                (*o.array)[size_t(at)] = v;
            }
            return;
        }
        if (o.kind != Value::ClipRef || !o.clip) return;
        Clip& c = *o.clip;
        if (c.kind == Clip::TextClip && lower(name) == "text") c.text = v.to_string();
        else if (lower(name) == "_visible") c.visible = v.truthy();
        else if (lower(name) == "_x" || lower(name) == "_y") { // (not a number: left as it was)
            if (const double at = v.to_number(); !std::isnan(at)) (lower(name) == "_x" ? c.set_x : c.set_y) = float(at);
        }
        else c.vars[lower(name)] = v;
    }

    int frame_of(Clip& c, const Value& v) {
        if (c.kind != Clip::SpriteClip) return 0;
        if (v.kind == Value::String) {
            const int at = movie->sprites[size_t(c.index)].label(v.s);
            if (at >= 0) return at;
            const double n = v.to_number();
            return std::isnan(n) ? c.frame : int(n) - 1;
        }
        return int(v.to_number()) - 1; // the scripts count frames from 1
    }

    Value call(const Value& fn, Clip* self_clip, const std::vector<Value>& args) {
        if (fn.kind != Value::FunctionRef || !fn.fn) return {};
        const Function& f = *fn.fn;
        std::map<std::string, Value> locals;
        for (size_t i = 0; i < f.params.size(); ++i) locals[lower(f.params[i])] = i < args.size() ? args[i] : Value{};
        return run(*f.code, f.start, f.end, self_clip ? self_clip : f.scope, &locals, f.pool);
    }

    Value call_method(Frame& fr, const Value& obj, const std::string& name, const std::vector<Value>& args) {
        const std::string n = lower(name);
        if (obj.kind == Value::String && obj.s == kKeyObject) return n == "getcode" ? Value::number(key_code) : Value{};
        if (obj.kind == Value::ArrayRef && obj.array) {
            if (n == "push") obj.array->insert(obj.array->end(), args.begin(), args.end());
            else if (n == "pop" && !obj.array->empty()) {
                const Value v = obj.array->back();
                obj.array->pop_back();
                return v;
            }
            return Value::number(double(obj.array->size()));
        }
        if (obj.kind == Value::String && n == "substr") {
            const double at = args.empty() ? 0 : args[0].to_number(), len = args.size() > 1 ? args[1].to_number() : double(obj.s.size());
            const size_t from = size_t(std::clamp(at < 0 ? at + double(obj.s.size()) : at, 0.0, double(obj.s.size())));
            return Value::string(obj.s.substr(from, size_t(std::max(0.0, len))));
        }
        if (obj.kind == Value::ClipRef && obj.clip) {
            Clip& c = *obj.clip;
            if (n == "gotoandplay" || n == "gotoandstop") {
                go(c, frame_of(c, args.empty() ? Value{} : args[0]), n == "gotoandplay");
                return {};
            }
            if (n == "play") return c.playing = true, Value{};
            if (n == "stop") return c.playing = false, Value{};
            if (const auto it = c.vars.find(n); it != c.vars.end() && it->second.kind == Value::FunctionRef) return call(it->second, &c, args);
        }
        (void)fr;
        return host ? host(name, args) : Value{};
    }

    static bool loose_equal(const Value& a, const Value& b) {
        const bool an = a.kind == Value::Undefined || a.kind == Value::Null, bn = b.kind == Value::Undefined || b.kind == Value::Null;
        if (an || bn) return an && bn;
        if (a.kind == Value::ClipRef || b.kind == Value::ClipRef) return a.kind == b.kind && a.clip == b.clip;
        if (a.kind == Value::String && b.kind == Value::String) return a.s == b.s;
        return a.to_number() == b.to_number();
    }
    static bool strict_equal(const Value& a, const Value& b) {
        const auto ka = a.kind == Value::Bool ? Value::Number : a.kind, kb = b.kind == Value::Bool ? Value::Number : b.kind;
        if (a.kind != b.kind && !(ka == kb && a.kind != Value::Bool && b.kind != Value::Bool)) return false;
        return loose_equal(a, b);
    }

    // Runs code[start, end) on `target`; returns what it returns.
    Value run(const Bytes& code, size_t start, size_t end, Clip* target, std::map<std::string, Value>* locals, std::vector<std::string> pool) {
        Frame f;
        f.target = target;
        if (locals) f.locals = *locals;
        f.pool = std::move(pool);
        std::vector<Value> st;
        auto pop = [&st]() {
            if (st.empty()) return Value{};
            Value v = std::move(st.back());
            st.pop_back();
            return v;
        };
        auto pop_args = [&]() {
            const int n = std::clamp(int(pop().to_number()), 0, 64);
            std::vector<Value> args;
            for (int i = 0; i < n; ++i) args.push_back(pop());
            return args;
        };
        size_t pc = start;
        for (int guard = 0; pc < end && guard < 200000; ++guard) {
            const u8 op = code[pc++];
            u16 len = 0;
            if (op >= 0x80) {
                if (pc + 2 > end) break;
                len = u16(code[pc] | code[pc + 1] << 8);
                pc += 2;
            }
            const size_t body = pc, next = std::min(end, pc + len);
            auto str_at = [&](size_t& p) {
                std::string s;
                while (p < next && code[p]) s += char(code[p++]);
                ++p;
                return s;
            };
            switch (op) {
            case 0x00: return {};
            case 0x06: if (target) target->playing = true; break;
            case 0x07: if (target) target->playing = false; break;
            case 0x12: st.push_back(Value::boolean(!pop().truthy())); break;
            case 0x17: pop(); break;
            case 0x1C: { const Value n = pop(); st.push_back(get_variable(f, n.to_string())); break; }
            case 0x1D: { const Value v = pop(), n = pop(); set_variable(f, n.to_string(), v); break; }
            case 0x22: { // GetProperty(target, index): 13 the name
                const int index = int(pop().to_number());
                const Value t = pop();
                Clip* c = t.kind == Value::ClipRef ? t.clip : t.to_string().empty() ? target : get_variable(f, t.to_string()).clip;
                st.push_back(c && *property(index) ? get_member(clip_value(c), property(index)) : Value{});
                break;
            }
            case 0x23: { // SetProperty(target, index, value)
                const Value v = pop();
                const int index = int(pop().to_number());
                const Value t = pop();
                Clip* c = t.kind == Value::ClipRef ? t.clip : t.to_string().empty() ? target : get_variable(f, t.to_string()).clip;
                if (c && *property(index)) set_member(clip_value(c), property(index), v);
                break;
            }
            case 0x0A: { const Value b = pop(), a = pop(); st.push_back(Value::number(a.to_number() + b.to_number())); break; }
            case 0x0B: { const Value b = pop(), a = pop(); st.push_back(Value::number(a.to_number() - b.to_number())); break; }
            case 0x0C: { const Value b = pop(), a = pop(); st.push_back(Value::number(a.to_number() * b.to_number())); break; }
            case 0x0D: { const Value b = pop(), a = pop(); st.push_back(Value::number(a.to_number() / b.to_number())); break; }
            case 0x40:   // NewObject (an Array, empty)
            case 0x42: { // InitArray: the first element on the top
                if (op == 0x40) pop();
                Value v;
                v.kind = Value::ArrayRef, v.array = std::make_shared<std::vector<Value>>(pop_args());
                if (op == 0x40) v.array->clear();
                st.push_back(std::move(v));
                break;
            }
            case 0x3D: { // CallFunction
                const std::string name = pop().to_string();
                const std::vector<Value> args = pop_args();
                const Value fn = get_variable(f, name);
                st.push_back(fn.kind == Value::FunctionRef ? call(fn, target, args) : (host ? host(name, args) : Value{}));
                break;
            }
            case 0x3E: return pop();
            case 0x47: { const Value b = pop(), a = pop();
                if (a.kind == Value::String || b.kind == Value::String) st.push_back(Value::string(a.to_string() + b.to_string()));
                else st.push_back(Value::number(a.to_number() + b.to_number()));
                break; }
            case 0x48: { const Value b = pop(), a = pop(); st.push_back(Value::boolean(a.to_number() < b.to_number())); break; }
            case 0x67: { const Value b = pop(), a = pop(); st.push_back(Value::boolean(a.to_number() > b.to_number())); break; }
            case 0x49: { const Value b = pop(), a = pop(); st.push_back(Value::boolean(loose_equal(a, b))); break; }
            case 0x66: { const Value b = pop(), a = pop(); st.push_back(Value::boolean(strict_equal(a, b))); break; }
            case 0x4C: st.push_back(st.empty() ? Value{} : st.back()); break;
            case 0x4E: { const Value n = pop(), o = pop(); st.push_back(get_member(o, n.to_string())); break; }
            case 0x4F: { const Value v = pop(), n = pop(), o = pop(); set_member(o, n.to_string(), v); break; }
            case 0x50: st.push_back(Value::number(pop().to_number() + 1)); break;
            case 0x51: st.push_back(Value::number(pop().to_number() - 1)); break;
            case 0x52: { // CallMethod
                const Value name = pop(), obj = pop();
                const std::vector<Value> args = pop_args();
                if (name.kind == Value::Undefined || name.to_string().empty()) st.push_back(call(obj, target, args));
                else st.push_back(call_method(f, obj, name.to_string(), args));
                break;
            }
            case 0x81: if (target && len >= 2) go(*target, int(code[body] | code[body + 1] << 8), false); break;
            case 0x87: if (len >= 1 && code[body] < 4) f.regs[code[body]] = st.empty() ? Value{} : st.back(); break;
            case 0x88: {
                size_t p = body + 2;
                f.pool.clear();
                const u16 n = u16(code[body] | code[body + 1] << 8);
                for (u16 i = 0; i < n && p < next; ++i) f.pool.push_back(str_at(p));
                break;
            }
            case 0x96: {
                size_t p = body;
                while (p < next) {
                    const u8 type = code[p++];
                    Value v;
                    switch (type) {
                    case 0: v = Value::string(str_at(p)); break;
                    case 1: { float x; std::memcpy(&x, &code[p], 4); p += 4; v = Value::number(x); break; }
                    case 2: v.kind = Value::Null; break;
                    case 3: break;
                    case 4: v = code[p] < 4 ? f.regs[code[p]] : Value{}; ++p; break;
                    case 5: v = Value::boolean(code[p++] != 0); break;
                    case 6: { // a double: its two halves swapped
                        u8 b[8];
                        for (int k = 0; k < 4; ++k) b[k] = code[p + 4 + size_t(k)], b[4 + k] = code[p + size_t(k)];
                        double x;
                        std::memcpy(&x, b, 8);
                        p += 8;
                        v = Value::number(x);
                        break;
                    }
                    case 7: { s32 x; std::memcpy(&x, &code[p], 4); p += 4; v = Value::number(x); break; }
                    case 8: v = code[p] < f.pool.size() ? Value::string(f.pool[code[p]]) : Value{}; ++p; break;
                    case 9: { const u16 i = u16(code[p] | code[p + 1] << 8); p += 2; v = i < f.pool.size() ? Value::string(f.pool[i]) : Value{}; break; }
                    default: p = next; break;
                    }
                    st.push_back(std::move(v));
                }
                break;
            }
            case 0x99: { const s16 off = s16(code[body] | code[body + 1] << 8); pc = size_t(ptrdiff_t(next) + off); continue; }
            case 0x9D: {
                const s16 off = s16(code[body] | code[body + 1] << 8);
                if (pop().truthy()) {
                    pc = size_t(ptrdiff_t(next) + off);
                    continue;
                }
                break;
            }
            case 0x9B: { // DefineFunction: name, parameters, the body after this record
                size_t p = body;
                auto fn = std::make_shared<Function>();
                const std::string name = str_at(p);
                const u16 n = u16(code[p] | code[p + 1] << 8);
                p += 2;
                for (u16 i = 0; i < n; ++i) fn->params.push_back(str_at(p));
                const u16 size = u16(code[p] | code[p + 1] << 8);
                fn->code = &code, fn->start = next, fn->end = std::min(end, next + size), fn->pool = f.pool, fn->scope = target;
                Value v;
                v.kind = Value::FunctionRef, v.fn = fn;
                if (name.empty()) st.push_back(v);
                else if (target) target->vars[lower(name)] = v;
                pc = fn->end;
                continue;
            }
            default: break; // an operation the race's movies do not use: skipped
            }
            pc = next;
        }
        return {};
    }

    // ---- buttons

    void buttons(Clip& c, std::vector<Clip*>& out) {
        for (auto& [i, ch] : c.children) {
            if (!ch->visible) continue;
            if (ch->kind == Clip::ButtonClip) out.push_back(ch.get());
            else if (ch->kind == Clip::SpriteClip) buttons(*ch, out);
        }
    }
    // A clip's place on the canvas.
    void place(const Clip& c, float& x, float& y) {
        x = y = 0;
        std::vector<const Clip*> chain;
        for (const Clip* p = &c; p && p->parent; p = p->parent) chain.push_back(p);
        float m[6] = {1, 0, 0, 1, 0, 0};
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Clip& k = **it;
            if (k.parent->kind != Clip::SpriteClip) continue;
            const Movie::Track& t = movie->sprites[size_t(k.parent->index)].tracks[k.track];
            const Movie::Key& key = t.key_at(k.parent->frame);
            float l[6];
            local(key, l);
            if (k.set_x) l[4] = *k.set_x;
            if (k.set_y) l[5] = *k.set_y;
            float r[6];
            multiply(m, l, r);
            std::memcpy(m, r, sizeof m);
        }
        x = m[4], y = m[5];
    }
    static void local(const Movie::Key& k, float* m) {
        const float c = std::cos(k.rotation), s = std::sin(k.rotation);
        m[0] = k.sx * c, m[1] = k.sx * s, m[2] = -k.sy * s, m[3] = k.sy * c, m[4] = k.x, m[5] = k.y;
    }
    static void multiply(const float* a, const float* b, float* out) { // a then b inside it: out = a x b
        out[0] = a[0] * b[0] + a[2] * b[1], out[1] = a[1] * b[0] + a[3] * b[1];
        out[2] = a[0] * b[2] + a[2] * b[3], out[3] = a[1] * b[2] + a[3] * b[3];
        out[4] = a[0] * b[4] + a[2] * b[5] + a[4], out[5] = a[1] * b[4] + a[3] * b[5] + a[5];
    }
    bool under_default(const Clip* c) {
        for (; c; c = c->parent)
            if (lower(c->name) == "default") return true;
        return false;
    }
    void conditions(Clip& button, u16 mask) {
        const Movie::Button& b = movie->buttons[size_t(button.index)];
        for (const auto& [m, block] : b.conditions)
            if ((m & mask) && block >= 0 && size_t(block) < movie->actions.size())
                queue.push_back({button.parent, block});
        run_queue();
    }
    void check_focus() {
        std::vector<Clip*> list;
        buttons(*root, list);

        if (focus && std::find(list.begin(), list.end(), focus) == list.end()) focus = nullptr;
        if (!focus && !list.empty()) {
            // The button in the clip named DEFAULT, else the highest; the pointer arriving on it is
            // its focus (its scripts run).
            for (Clip* b : list)
                if (under_default(b)) focus = b;
            if (!focus) {
                float top = 1e30f;
                for (Clip* b : list) {
                    float x, y;
                    place(*b, x, y);
                    if (y < top) top = y, focus = b;
                }
            }
            conditions(*focus, 0x01);
        }
    }
    void move_focus(int dx, int dy) {
        std::vector<Clip*> list;
        buttons(*root, list);
        if (!focus || list.size() < 2) return;
        float fx, fy;
        place(*focus, fx, fy);
        Clip* best = nullptr;
        float best_d = 1e30f;
        Clip* wrap = nullptr;
        float wrap_d = -1e30f;
        for (Clip* b : list) {
            if (b == focus) continue;
            float x, y;
            place(*b, x, y);
            const float along = dx ? (x - fx) * float(dx) : (y - fy) * float(dy), across = dx ? std::fabs(y - fy) : std::fabs(x - fx);
            if (along > 0.5f) {
                const float d = along + across * 2;
                if (d < best_d) best_d = d, best = b;
            } else if (-along - across * 2 > wrap_d) {
                wrap_d = -along - across * 2, wrap = b; // the farthest the other way: round the list
            }
        }
        Clip* to = best ? best : wrap;
        if (to && to != focus) {
            focus = to;
            conditions(*to, 0x01);
        }
    }

    // ---- drawing

    // A clip box (canvas units, x0 .. y1), intersected with the one already there.
    struct ClipBox {
        bool on = false;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        void add(float bx0, float by0, float bx1, float by1) {
            if (!on) x0 = bx0, y0 = by0, x1 = bx1, y1 = by1, on = true;
            else x0 = std::max(x0, bx0), y0 = std::max(y0, by0), x1 = std::min(x1, bx1), y1 = std::min(y1, by1);
        }
    };
    static void set_clip(Draw& d, const ClipBox& clip) {
        if (!clip.on) return;
        d.clipped = true;
        d.clip[0] = clip.x0, d.clip[1] = clip.y0, d.clip[2] = clip.x1, d.clip[3] = clip.y1;
    }

    void draw(const Clip& c, const float* m, const float* mul, const float* add, std::vector<Draw>& out) const { draw(c, m, mul, add, out, ClipBox{}); }
    void draw(const Clip& c, const float* m, const float* mul, const float* add, std::vector<Draw>& out, const ClipBox& outer) const {
        if (!c.visible) return;
        if (c.kind == Clip::ButtonClip) {
            const int state = &c == focus ? (pressed > 0 ? 2 : 1) : 0;
            const Clip* s = c.states[size_t(state)] ? c.states[size_t(state)].get() : c.states[0].get();
            if (s) draw(*s, m, mul, add, out, outer);
            return;
        }
        const Movie::Sprite& sp = movie->sprites[size_t(c.index)];
        ClipBox clip = outer; // (what the tracks so far are drawn inside)
        u32 clip_until = 0;   // a mask track of this sprite is on for the tracks up to this depth
        ClipBox within = outer;
        for (size_t i = 0; i < sp.tracks.size(); ++i) {
            const Movie::Track& t = sp.tracks[i];
            if (!(t.first <= c.frame && c.frame < t.end)) continue;
            if (t.depth > clip_until) clip = outer, clip_until = 0; // (the mask ended)
            const Movie::Key& k = t.key_at(c.frame);
            float l[6], w[6];
            local(k, l);
            if (const auto it = c.children.find(i); it != c.children.end()) { // (placed by a script)
                if (it->second->set_x) l[4] = *it->second->set_x;
                if (it->second->set_y) l[5] = *it->second->set_y;
            }
            multiply(m, l, w);
            float km[4], ka[4], wm[4], wa[4];
            for (int j = 0; j < 4; ++j) {
                km[j] = float((k.mul >> (8 * j)) & 0xFF) / 128.0f;
                ka[j] = float((k.add >> (8 * j)) & 0xFF) / 255.0f;
                wm[j] = km[j] * mul[j];
                wa[j] = ka[j] * mul[j] + add[j];
            }
            if (t.clip != 0) { // a mask: its shape's box, in this track's place, for the tracks above up to its depth
                within = outer;
                float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                if (t.kind != Movie::kSprite && t.kind != Movie::kButton && t.kind != Movie::kText && t.kind != Movie::kExternal &&
                    t.kind == 0 && size_t(t.index) < movie->shapes.size()) {
                    const Movie::Shape& sh = movie->shapes[t.index];
                    const float px[2] = {sh.x0, sh.x1}, py[2] = {sh.y0, sh.y1};
                    x0 = y0 = 1e9f, x1 = y1 = -1e9f;
                    for (int a = 0; a < 2; ++a)
                        for (int b = 0; b < 2; ++b) {
                            const float qx = w[0] * px[a] + w[2] * py[b] + w[4], qy = w[1] * px[a] + w[3] * py[b] + w[5];
                            x0 = std::min(x0, qx), y0 = std::min(y0, qy), x1 = std::max(x1, qx), y1 = std::max(y1, qy);
                        }
                }
                within.add(x0, y0, x1, y1);
                clip = within, clip_until = t.clip;
                continue;
            }
            if (t.kind == Movie::kSprite || t.kind == Movie::kButton) {
                const auto it = c.children.find(i);
                if (it != c.children.end()) draw(*it->second, w, wm, wa, out, clip);
            } else if (t.kind == Movie::kText) {
                if (size_t(t.index) >= movie->texts.size()) continue;
                Draw d;
                d.text = &movie->texts[t.index];
                const auto it = c.children.find(i);
                d.words = it != c.children.end() ? it->second->text : d.text->text;
                std::memcpy(d.m, w, sizeof w), std::memcpy(d.mul, wm, sizeof wm), std::memcpy(d.add, wa, sizeof wa);
                set_clip(d, clip);
                out.push_back(std::move(d));
            } else if (t.kind == Movie::kExternal) {
                Draw d;
                // The picture's own name, else its clip's, else its name in the movie's table.
                const std::string listed = size_t(t.index) < movie->externals.size() ? movie->externals[t.index].name : std::string();
                d.external = !t.name.empty() ? t.name : !c.name.empty() ? c.name : !listed.empty() ? listed : std::string("_EXT");
                std::memcpy(d.m, w, sizeof w), std::memcpy(d.mul, wm, sizeof wm), std::memcpy(d.add, wa, sizeof wa);
                set_clip(d, clip);
                out.push_back(std::move(d));
            } else if (t.kind == 0 && size_t(t.index) < movie->shapes.size()) { // (kind 1: a morph shape, none in these movies)
                for (const Movie::Shape::Group& g : movie->shapes[t.index].groups) {
                    Draw d;
                    d.group = &g;
                    std::memcpy(d.m, w, sizeof w), std::memcpy(d.mul, wm, sizeof wm), std::memcpy(d.add, wa, sizeof wa);
                    set_clip(d, clip);
                    out.push_back(std::move(d));
                }
            }
        }
    }
};

StrobePlayer::StrobePlayer(std::shared_ptr<const StrobeMovie> movie, Host host) : impl_(std::make_unique<Impl>()) {
    impl_->movie = std::move(movie);
    impl_->host = std::move(host);
    impl_->self = this;
    restart();
}

StrobePlayer::~StrobePlayer() = default;

void StrobePlayer::restart() {
    Impl& m = *impl_;
    m.queue.clear();
    m.focus = nullptr;
    m.gone.clear();
    m.root = std::make_unique<Clip>();
    m.root->index = 0;
    m.clock = 0, m.pressed = 0;
    m.enter(*m.root);
    m.run_queue();
    m.check_focus();
}

void StrobePlayer::advance(float seconds) {
    Impl& m = *impl_;
    m.clock += seconds * m.movie->fps;
    for (int n = 0; m.clock >= 1 && n < 600; ++n) {
        m.clock -= 1;
        m.tick(*m.root);
        m.run_queue();
        if (m.pressed > 0) --m.pressed;
        m.check_focus();
    }
    if (m.clock > 1) m.clock = 0;
}

void StrobePlayer::key(int code) {
    Impl& m = *impl_;
    m.key_code = code;
    // The clips' own key handlers (a slider's left and right).
    std::vector<Clip*> all;
    std::function<void(Clip&)> walk = [&](Clip& c) {
        for (auto& [i, ch] : c.children) {
            all.push_back(ch.get());
            if (ch->kind == Clip::SpriteClip) walk(*ch);
        }
    };
    walk(*m.root);
    for (Clip* c : all) {
        if (!c->parent || c->parent->kind != Clip::SpriteClip) continue;
        const Movie::Track& t = m.movie->sprites[size_t(c->parent->index)].tracks[c->track];
        for (const auto& [mask, block] : t.clip_actions)
            if ((mask & 0x40) && block >= 0) m.queue.push_back({c, block});
    }
    m.run_queue();
    if (code == 0x26 && nav_v_) m.move_focus(0, -1);
    else if (code == 0x28 && nav_v_) m.move_focus(0, 1);
    else if (code == 0x25 && nav_h_) m.move_focus(-1, 0);
    else if (code == 0x27 && nav_h_) m.move_focus(1, 0);
    else if (code == 0x20 && m.focus) {
        m.pressed = 4;
        m.conditions(*m.focus, 0x04);
    }
    m.check_focus();
}

std::vector<StrobePlayer::Draw> StrobePlayer::draw_list() const {
    std::vector<Draw> out;
    const float m[6] = {1, 0, 0, 1, 0, 0}, mul[4] = {1, 1, 1, 1}, add[4] = {0, 0, 0, 0};
    impl_->draw(*impl_->root, m, mul, add, out);
    return out;
}

int StrobePlayer::frame() const { return impl_->root->frame; }

void StrobePlayer::go_to(int frame) {
    Impl& m = *impl_;
    m.go(*m.root, frame, false);
    m.run_queue();
    m.check_focus();
}

void StrobePlayer::play() {
    impl_->root->playing = true;
}

std::string StrobePlayer::state() const {
    std::string best;
    int at = -1;
    for (const auto& [frame, name] : impl_->movie->sprites[0].labels)
        if (frame <= impl_->root->frame && frame > at) at = frame, best = name;
    return best;
}

std::string StrobePlayer::focus() const {
    std::string path;
    for (const Clip* c = impl_->focus; c && c->parent; c = c->parent)
        if (!c->name.empty()) path = c->name + (path.empty() ? "" : "." + path);
    return impl_->focus && path.empty() ? std::string("?") : path;
}

Value StrobePlayer::variable(const std::string& path) const {
    Impl::Frame f;
    f.target = impl_->root.get();
    return impl_->get_variable(f, path);
}

} // namespace gt4r::race

namespace gt4r::race {

bool StrobePlayer::set(const std::string& path, const std::string& member, const Value& v) {
    Impl::Frame f;
    f.target = impl_->root.get();
    const Value o = path.empty() ? Impl::clip_value(impl_->root.get()) : impl_->get_variable(f, path);
    if (o.kind != Value::ClipRef || !o.clip) return false;
    impl_->set_member(o, member, v);
    return true;
}

StrobePlayer::Value StrobePlayer::call(const std::string& path, const std::string& method, const std::vector<Value>& args) {
    Impl::Frame f;
    f.target = impl_->root.get();
    const Value o = path.empty() ? Impl::clip_value(impl_->root.get()) : impl_->get_variable(f, path);
    if (o.kind != Value::ClipRef || !o.clip) return {};
    return impl_->call_method(f, o, method, args);
}

} // namespace gt4r::race

namespace gt4r::race {

std::vector<std::string> StrobePlayer::tree() const {
    std::vector<std::string> out;
    std::function<void(const Clip&, const std::string&)> walk = [&](const Clip& c, const std::string& path) {
        for (const auto& [t, ch] : c.children) {
            const std::string p = path.empty() ? (ch->name.empty() ? "#" + std::to_string(t) : ch->name) : path + "." + (ch->name.empty() ? "#" + std::to_string(t) : ch->name);
            const char* kind = ch->kind == Clip::TextClip ? "text" : ch->kind == Clip::ButtonClip ? "button" : "sprite";
            out.push_back(p + "  " + kind + " " + std::to_string(ch->index) + " frame " + std::to_string(ch->frame) + (ch->visible ? "" : " hidden") +
                          (ch->kind == Clip::TextClip ? " \"" + ch->text + "\"" : ""));
            walk(*ch, p);
        }
    };
    walk(*impl_->root, "");
    return out;
}

} // namespace gt4r::race
