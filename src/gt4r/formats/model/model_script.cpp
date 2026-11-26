#include "gt4r/formats/model/model_script.h"

#include "base/ee_math.h"

#include <cmath>
#include <cstring>
#include <random>

namespace gt4r::mdls {

namespace {

float as_f(u32 v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
u32 bits(float f) {
    u32 v;
    std::memcpy(&v, &f, 4);
    return v;
}
std::string name_at(ByteSpan d, u32 at) {
    std::string s;
    for (size_t o = at; o < d.size() && d[o] && s.size() < 64; ++o) s += char(d[o]);
    return s;
}
// A table of {u32 name, u16 cells, u16 cell}.
std::vector<Script::Variable> variables(ByteSpan d, u32 at, u32 count) {
    std::vector<Script::Variable> out;
    if (!count) return out;
    if (!at || at + 8 * u64(count) > d.size()) throw FormatError("MDLS script: a variable table is outside the block");
    for (u32 i = 0; i < count; ++i) {
        Script::Variable v;
        v.name = name_at(d, rd32(d, at + 8 * size_t(i)));
        v.cells = u16(rd16(d, at + 8 * size_t(i) + 4)), v.cell = u16(rd16(d, at + 8 * size_t(i) + 6));
        out.push_back(std::move(v));
    }
    return out;
}

} // namespace

int Script::host_cell(const std::string& name) const {
    for (const Variable& v : host)
        if (v.name == name) return v.cell;
    return -1;
}

Script Script::parse(ByteSpan d) {
    Script s;
    if (d.size() < 0x80 || !gt3r::has_magic(d, "MDLS")) throw FormatError("not an MDLS model set");
    const u32 size = rd32(d, 0x10);
    if (size < 0x80 || size > d.size()) throw FormatError("MDLS: bad size");
    d = d.first(size);
    const u32 code = rd32(d, 0x5C);
    if (!code || code >= size) return s;
    // The code runs to the next table after it (or the block's end); commands are checked as they run.
    u32 end = size;
    for (size_t o : {0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x60, 0x68, 0x7C}) {
        const u32 at = rd32(d, o);
        if (at > code && at < end) end = at;
    }
    s.code.assign(d.begin() + code, d.begin() + end);
    s.register_cells = rd16(d, 0x20), s.second_cells = rd16(d, 0x22);
    s.host = variables(d, rd32(d, 0x4C), rd16(d, 0x24));
    s.registers = variables(d, rd32(d, 0x54), rd16(d, 0x28));
    const u32 models = rd16(d, 0x16), table = rd32(d, 0x38);
    if (table + 0x28 * u64(models) > d.size()) throw FormatError("MDLS: model records outside the block");
    s.models.resize(models);
    for (u32 i = 0; i < models; ++i) {
        const size_t rec = table + 0x28 * size_t(i);
        s.models[i] = {s32(rd32(d, rec + 0x1C)), s32(rd32(d, rec + 0x20)), s32(rd32(d, rec + 0x24))};
    }
    return s;
}

float ScriptState::register_f(u32 cell) const { return cell < registers.size() ? as_f(registers[cell]) : 0.0f; }
void ScriptState::set_host_f(int cell, float value) { set_host(cell, bits(value)); }

bool run_script(const Script& s, s32 entry, ScriptState& st, const ScriptCallback& callback) {
    if (entry < 0 || size_t(entry) >= s.code.size()) return false;
    const ByteSpan code(s.code);
    u32 stack[256] = {};
    // A frame's cells 0 and 1 are the caller's frame and the place to return to; a routine's own
    // start at 2.
    int sp = 2, fp = 0, depth = 0;
    size_t pc = size_t(entry);
    static std::minstd_rand rng(12345);
    auto segment = [&](u32 operand) -> std::vector<u32>* {
        switch (operand >> 14) {
        case 0: return &st.registers;
        case 1: return &st.second;
        case 2: return &st.host;
        default: return nullptr;
        }
    };
    auto need = [&](size_t n) { return pc + n <= code.size(); };
    for (int steps = 0; steps < 10000; ++steps) {
        if (!need(1)) return false;
        const u8 op = code[pc++];
        auto push = [&](u32 v) {
            if (sp < 256) stack[sp++] = v;
        };
        auto pop = [&]() -> u32 { return sp > 0 ? stack[--sp] : 0; };
        if (op >= 0x30 && op <= 0x3A) { // i32 arithmetic and compares: second from top by top
            const s32 b = s32(pop()), a = s32(pop());
            s32 r = 0;
            switch (op) {
            case 0x30: r = s32(u32(a) + u32(b)); break;
            case 0x31: r = s32(u32(a) - u32(b)); break;
            case 0x32: r = s32(u32(a) * u32(b)); break;
            case 0x33: r = b ? (a == INT32_MIN && b == -1 ? a : a / b) : 0; break;
            case 0x34: r = b ? (b == -1 ? 0 : a % b) : 0; break;
            case 0x35: r = a < b; break;
            case 0x36: r = a <= b; break;
            case 0x37: r = a > b; break;
            case 0x38: r = a >= b; break;
            case 0x39: r = a == b; break;
            default: r = a != b; break;
            }
            push(u32(r));
            continue;
        }
        if (op >= 0x80 && op <= 0x8A) { // f32
            const gt3r::ee::F eb = gt3r::ee::F::raw(pop()), ea = gt3r::ee::F::raw(pop());
            const float b = as_f(eb.bits), a = as_f(ea.bits);
            switch (op) {
            case 0x80: push((ea + eb).bits); break; // (the EE's arithmetic)
            case 0x81: push((ea - eb).bits); break;
            case 0x82: push((ea * eb).bits); break;
            case 0x83: push((ea / eb).bits); break;
            case 0x84: push(bits(std::fmod(a, b))); break;
            case 0x85: push(a < b); break;
            case 0x86: push(a <= b); break;
            case 0x87: push(a > b); break;
            case 0x88: push(a >= b); break;
            case 0x89: push(a == b); break;
            default: push(a != b); break;
            }
            continue;
        }
        switch (op) {
        case 0x10: case 0x13: { // push / pop a local
            if (!need(2)) return false;
            const int at = fp + s16(rd16(code, pc));
            pc += 2;
            if (at < 0 || at >= 256) return false;
            if (op == 0x10) push(stack[at]);
            else stack[at] = pop();
            break;
        }
        case 0x11:
            if (!need(4)) return false;
            push(rd32(code, pc)), pc += 4;
            break;
        case 0x12: case 0x14: { // push / pop a variable (a segment the set lacks reads 0)
            if (!need(2)) return false;
            const u32 operand = rd16(code, pc);
            pc += 2;
            std::vector<u32>* seg = segment(operand);
            const size_t cell = operand & 0x3FFF;
            if (op == 0x12) push(seg && cell < seg->size() ? (*seg)[cell] : 0);
            else {
                const u32 v = pop();
                if (seg && cell < seg->size()) (*seg)[cell] = v;
            }
            break;
        }
        case 0x20:
            if (!need(1)) return false;
            sp = std::min(256, sp + code[pc++]);
            break;
        case 0x21: // a call begins: the frame's two cells
            push(u32(fp)), push(0);
            break;
        case 0x22: { // u8 arguments, u16 place
            if (!need(3)) return false;
            const int args = code[pc];
            const size_t place = rd16(code, pc + 1);
            pc += 3;
            const int frame = sp - args - 2;
            if (frame < 0 || place >= code.size()) return false;
            stack[frame + 1] = u32(pc);
            fp = frame, pc = place, ++depth;
            break;
        }
        case 0x23: { // return: the top cell is the result; the outermost frame ends the run
            if (!need(1)) return false;
            ++pc;
            if (depth == 0) return true;
            const u32 result = pop();
            const int frame = fp;
            fp = int(stack[frame]), pc = stack[frame + 1], sp = frame, --depth;
            push(result);
            break;
        }
        case 0x24:
            if (!need(2)) return false;
            pc = rd16(code, pc);
            break;
        case 0x25: case 0x26: case 0x27: { // jump by an offset from the next command; when 0; when not 0
            if (!need(2)) return false;
            const s32 rel = s16(rd16(code, pc));
            pc += 2;
            const bool take = op == 0x25 || (pop() == 0) == (op == 0x26);
            if (take) {
                const s64 to = s64(pc) + rel;
                if (to < 0 || size_t(to) >= code.size()) return false;
                pc = size_t(to);
            }
            break;
        }
        case 0x28: pop(); break;
        case 0x3B: push(u32(-s32(pop()))); break;
        case 0x3C: push(gt3r::ee::int_to_f(s32(pop())).bits); break;
        case 0x40: { const u32 b = pop(); push(pop() & b); break; }
        case 0x41: { const u32 b = pop(); push(pop() | b); break; }
        case 0x42: { const u32 b = pop(); push(pop() ^ b); break; }
        case 0x44: push(pop() << 1); break;
        case 0x45: push(u32(s32(pop()) >> 1)); break;
        case 0x46: push(~pop()); break;
        case 0x8B: push(bits(-as_f(pop()))); break;
        case 0x8C: { // toward zero
            const float f = as_f(pop());
            push(std::isfinite(f) && std::fabs(f) < 2.1e9f ? u32(s32(f)) : 0u);
            break;
        }
        case 0xA0: push(bits(as_f(pop()) * (float(rng() % 65536) / 65536.0f))); break;
        case 0xA1: push(bits(std::sin(as_f(pop())))); break;
        case 0xA2: push(bits(std::cos(as_f(pop())))); break;
        case 0xA3: push(bits(std::tan(as_f(pop())))); break;
        case 0xA4: push(bits(std::sqrt(as_f(pop())))); break;
        case 0xA5: push(bits(0.5f - 0.5f * std::cos(as_f(pop())))); break;
        case 0xB0: { const float b = as_f(pop()), a = as_f(pop()); push(bits(std::pow(a, b))); break; }
        case 0xB1: { const float b = as_f(pop()), a = as_f(pop()); push(bits(std::atan2(a, b))); break; }
        case 0xB2: { // the fraction of a / b
            const gt3r::ee::F b = gt3r::ee::F::raw(pop()), a = gt3r::ee::F::raw(pop()), q = a / b;
            push((q - gt3r::ee::int_to_f(gt3r::ee::to_int(q))).bits);
            break;
        }
        case 0xC0: case 0xC1: { // ask the owner: an i32 or an f32 answer
            if (!need(4)) return false;
            const u32 id = rd16(code, pc), operand = rd16(code, pc + 2);
            pc += 4;
            float answer = 0;
            if (callback) {
                const std::vector<u32>* seg = segment(operand);
                const size_t cell = operand & 0x3FFF;
                answer = callback(id, seg && cell < seg->size() ? std::span<const u32>(*seg).subspan(cell) : std::span<const u32>{});
            }
            push(op == 0xC0 ? u32(s32(answer)) : bits(answer));
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

std::vector<u32> still_registers(const Script& s, std::span<const std::pair<std::string, float>> hosts) {
    if (s.empty()) return {};
    ScriptState st(s);
    for (const auto& [name, value] : hosts) {
        const int cell = s.host_cell(name);
        if (name == "time" || name == "timezone") st.set_host(cell, u32(s32(value)));
        else st.set_host_f(cell, value);
    }
    for (const Script::Entries& e : s.models)
        if (e.setup >= 0) run_script(s, e.setup, st);
    for (const Script::Entries& e : s.models)
        if (e.tick >= 0) run_script(s, e.tick, st);
    for (const Script::Entries& e : s.models)
        if (e.draw >= 0) run_script(s, e.draw, st);
    return st.registers;
}

} // namespace gt4r::mdls
