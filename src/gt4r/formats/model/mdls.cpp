#include "gt4r/formats/model/mdls.h"
#include "gt4r/formats/model/mdls_skin.h"

#include "formats/model/gtm1.h"
#include "formats/ps2/vif.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>

namespace gt4r::mdls {

namespace {

namespace ps2 = gt3r::ps2;

constexpr float kUvScale = 1.0f / 4096.0f; // 16-bit texture coordinates: 4.12 fixed point
constexpr float kNormalScale = 1.0f / 127.0f;
constexpr u8 kNop = 0x01, kJump8 = 0x08, kJump16 = 0x09, kSelectSet = 0x2B;
// The reflection passes' weight C0 + C1 min(V.N, 0): 0x56 the draw's factors (the program's globals [0x6238A0] and
// [0x62389C]), 0x57 the constant C0 = 1, C1 = 0 (the handlers 0x4566D0, 0x4566F8). GTM1 sets them with 0x36 / 0x32.
constexpr u8 kDrawFactors = 0x56, kPlainFactors = 0x57, kSetC1 = 0x32, kSetC0 = 0x36;

// Operand bytes of a draw-program command; -1: control flow read by the walker, -2: none such.
// (The game's dispatcher has 0x5B commands; the sizes are what each handler steps over.)
int operand_bytes(u8 op) {
    switch (op) {
    case 0x01: case 0x0B: case 0x0C: case 0x0D: case 0x17: case 0x18: case 0x1A: case 0x1B: case 0x1D: case 0x1E:
    case 0x22: case 0x23: case 0x25: case 0x26: case 0x2D: case 0x2E: case 0x30: case 0x38: case 0x39: case 0x3A:
    case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x43: case 0x47: case 0x4E: case 0x4F:
    case 0x56: case 0x57: case 0x58:
        return 0;
    case 0x02: case 0x04: case 0x0E: case 0x19: case 0x1F: case 0x29: case 0x2A: case 0x2B: case 0x2F: case 0x50:
    case 0x59:
        return 1;
    case 0x03: case 0x05: case 0x1C: case 0x20: case 0x2C: case 0x44: case 0x45: case 0x46: case 0x48: case 0x49:
    case 0x4A: case 0x4B: case 0x4C: case 0x51: case 0x5A:
        return 2;
    case 0x41: case 0x42:
        return 3;
    case 0x14: case 0x15: case 0x16: case 0x21: case 0x24: case 0x27: case 0x32: case 0x34: case 0x35: case 0x36:
        return 4;
    case 0x54: case 0x55:
        return 8;
    case 0x11: case 0x12: case 0x31:
        return 12;
    case 0x13: case 0x28: case 0x33: case 0x37: case 0x52: case 0x53:
        return 16;
    case 0x0F: case 0x10:
        return 64;
    case 0x00: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x4D:
        return -1;
    default:
        return -2;
    }
}

// Commands GTM1's reader does not have: no-ops there, a callback, the script and what reads its registers,
// the outside texture sets, the colour scale and offset, the face and texture-coordinate modes.
bool only_here(u8 op) { return op >= 0x38 && op != 0x4D && op != kDrawFactors && op != kPlainFactors; }

void put16(Bytes& b, size_t o, u32 v) {
    b[o] = u8(v), b[o + 1] = u8(v >> 8);
}
void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }
s32 s16_at(ByteSpan d, size_t o) { return s32(s16(rd16(d, o))); }

// Walks a draw program from `start` over every path and replaces the commands GTM1 lacks.
void patch_program(Bytes& b, size_t start, ConvertStats& st, const Scene& scene, std::set<u32>* shapes = nullptr, std::map<u32, u16>* weights = nullptr) {
    std::set<size_t> seen;
    std::vector<size_t> work{start};
    std::map<size_t, int> weight_at; // a case's: the weight's register before its switch
    std::vector<std::pair<size_t, size_t>> inside; // Scene::only_inside: the constructs kept
    std::vector<std::pair<size_t, size_t>> draws;  // the commands that draw: place, size
    const ByteSpan d(b);
    auto need = [&](size_t o, size_t n) {
        if (o > b.size() || b.size() - o < n) throw FormatError("MDLS draw program runs past the block");
    };
    while (!work.empty()) {
        size_t pc = work.back();
        work.pop_back();
        int weight = weight_at.count(pc) ? weight_at[pc] : -1; // the register of the last 0x4C on this path
        while (seen.insert(pc).second) {
            need(pc, 1);
            const u8 op = b[pc];
            const size_t a = pc + 1;
            ++st.commands;
            if (op == 0x00) break;
            if (op == 0x06 || op == 0x4D) { // u16 id / register, u8 n, s16 rel[n]: each from its own place
                need(a, 3);
                const u32 n = b[a + 2];
                const u32 id = rd16(d, a);
                const size_t tab = a + 3;
                need(tab, 2 * size_t(n));
                const size_t after = tab + 2 * size_t(n);
                for (u32 k = 0; k < n; ++k) {
                    work.push_back(size_t(s64(tab + 2 * k) + s16_at(d, tab + 2 * k)));
                    weight_at.emplace(work.back(), weight);
                }
                if (op == 0x06 && scene.only_inside >= 0 && id == u32(scene.only_inside) && n) {
                    // The construct: from its first case to where the jump after the table leads.
                    size_t lo = SIZE_MAX, hi = 0;
                    for (u32 k = 0; k < n; ++k) lo = std::min(lo, size_t(s64(tab + 2 * k) + s16_at(d, tab + 2 * k)));
                    need(after, 1);
                    if (b[after] == 0x08) need(after + 1, 1), hi = size_t(s64(after + 1) + std::int8_t(b[after + 1]));
                    else if (b[after] == 0x09) need(after + 1, 2), hi = size_t(s64(after + 1) + s16_at(d, after + 1));
                    if (hi > lo) inside.emplace_back(lo, hi);
                }
                // A register reads its value (0 without the script's), a callback the scene's.
                std::optional<s64> value;
                if (op == 0x4D) value = id < scene.registers.size() ? s64(s32(scene.registers[id])) : 0;
                else if (scene.resolve_callbacks) {
                    const auto it = scene.callback_values.find(u16(id));
                    value = it != scene.callback_values.end() ? it->second : scene.callback_default;
                }
                if (value) {
                    ++st.replaced_commands;
                    if (*value >= 0 && *value < s64(n)) {
                        const size_t entry = tab + 2 * size_t(*value);
                        const s64 rel = s64(entry) + s16_at(d, entry) - s64(pc + 2);
                        b[pc] = kNop, b[pc + 1] = kJump16;
                        put16(b, pc + 2, u32(s32(rel)) & 0xFFFF);
                    } else {
                        std::fill(b.begin() + ptrdiff_t(pc), b.begin() + ptrdiff_t(after), kNop);
                    }
                }
                pc = after;
                continue;
            }
            if (op == 0x07) { // 16-byte sphere, u8 n, s16 rel[n]
                need(a, 17);
                const u32 n = b[a + 16];
                need(a + 17, 2 * size_t(n));
                for (u32 k = 0; k < n; ++k) work.push_back(size_t(s64(a + 17 + 2 * k) + s16_at(d, a + 17 + 2 * k)));
                if (n) break;
                pc = a + 17;
                continue;
            }
            if (op == 0x08) {
                need(a, 1);
                pc = size_t(s64(a) + std::int8_t(b[a]));
                continue;
            }
            if (op == 0x09) {
                need(a, 2);
                pc = size_t(s64(a) + s16_at(d, a));
                continue;
            }
            if (op == 0x0A) { // u8 n, n x 12 bytes, s16 skip
                need(a, 1);
                const size_t skip = a + 1 + 12 * size_t(b[a]);
                need(skip, 2);
                work.push_back(size_t(s64(skip) + s16_at(d, skip)));
                pc = skip + 2;
                continue;
            }
            const int n = operand_bytes(op);
            if (n < 0) throw FormatError("MDLS draw program: unknown command");
            need(a, size_t(n));
            if (op == 0x02 || op == 0x03 || op == 0x04 || op == 0x05 || op == 0x35) draws.emplace_back(pc, size_t(n) + 1);
            if (op == 0x4C) weight = int(rd16(d, a));
            if (weights && weight >= 0) { // (the register each shape drawn has its weight from)
                if (op == 0x04) (*weights)[b[a]] = u16(weight);
                else if (op == 0x05) (*weights)[rd16(d, a)] = u16(weight);
                else if (op == 0x35)
                    for (u32 k = 0; k < rd16(d, a + 2); ++k) (*weights)[rd16(d, a) + k] = u16(weight);
            }
            if (shapes) { // the shapes this program can draw
                if (op == 0x04) shapes->insert(b[a]);
                else if (op == 0x05) shapes->insert(rd16(d, a));
                else if (op == 0x35)
                    for (u32 k = 0; k < rd16(d, a + 2); ++k) shapes->insert(rd16(d, a) + k);
            }
            if (op == 0x50 && b[a] < scene.outside_sets.size() && scene.outside_sets[b[a]] >= 0) {
                ++st.replaced_commands;
                b[pc] = kSelectSet, b[a] = u8(scene.outside_sets[b[a]]); // a set of the model's own list
            } else if (only_here(op)) {
                ++st.replaced_commands;
                std::fill(b.begin() + ptrdiff_t(pc), b.begin() + ptrdiff_t(a) + n, kNop);
            }
            pc = a + size_t(n);
        }
    }
    if (scene.only_inside >= 0)
        for (const auto& [at, size] : draws) {
            bool keep = false;
            for (const auto& [lo, hi] : inside) keep |= at >= lo && at < hi;
            if (!keep) std::fill(b.begin() + ptrdiff_t(at), b.begin() + ptrdiff_t(at + size), kNop);
        }
}

// The program from `start` copied to the end of `b` with GT4's factor commands as GTM1's: 0x56 (the draw's factors,
// as a program starts) as C0 = 1, C1 = 0, 0x57 as C0 = 1, C1 = -0 (mdls.h kPlainFactorsMark). Every command a path
// reaches is copied in its order, the jumps and tables aimed again (0x08 as 0x09). Returns the copy's offset.
u32 relink_program(Bytes& b, size_t start) {
    std::map<size_t, size_t> size_at; // reached command -> its size
    std::vector<size_t> work{start};
    const ByteSpan d(b);
    auto need = [&](size_t o, size_t n) {
        if (o > b.size() || b.size() - o < n) throw FormatError("MDLS draw program runs past the block");
    };
    while (!work.empty()) {
        size_t pc = work.back();
        work.pop_back();
        while (!size_at.count(pc)) {
            need(pc, 1);
            const u8 op = b[pc];
            const size_t a = pc + 1;
            size_t n = 0;
            bool ends = false;
            if (op == 0x00) n = 1, ends = true;
            else if (op == 0x06 || op == 0x4D) {
                need(a, 3);
                n = 4 + 2 * size_t(b[a + 2]);
                need(pc, n);
                for (u32 k = 0; k < b[a + 2]; ++k) work.push_back(size_t(s64(a + 3 + 2 * k) + s16_at(d, a + 3 + 2 * k)));
            } else if (op == 0x07) {
                need(a, 17);
                n = 18 + 2 * size_t(b[a + 16]);
                need(pc, n);
                for (u32 k = 0; k < b[a + 16]; ++k) work.push_back(size_t(s64(a + 17 + 2 * k) + s16_at(d, a + 17 + 2 * k)));
                ends = b[a + 16] != 0;
            } else if (op == kJump8) {
                need(a, 1);
                n = 2, ends = true;
                work.push_back(size_t(s64(a) + std::int8_t(b[a])));
            } else if (op == kJump16) {
                need(a, 2);
                n = 3, ends = true;
                work.push_back(size_t(s64(a) + s16_at(d, a)));
            } else if (op == 0x0A) {
                need(a, 1);
                const size_t skip = a + 1 + 12 * size_t(b[a]);
                need(skip, 2);
                n = skip + 2 - pc;
                work.push_back(size_t(s64(skip) + s16_at(d, skip)));
            } else {
                const int k = operand_bytes(op);
                if (k < 0) throw FormatError("MDLS draw program: unknown command");
                n = 1 + size_t(k);
                need(pc, n);
            }
            size_at[pc] = n;
            if (ends) break;
            pc += n;
        }
    }
    // The copy: each command's new place, then the relative operands.
    std::map<size_t, size_t> moved;
    size_t at = (b.size() + 15) & ~size_t(15);
    for (const auto& [pc, n] : size_at) {
        moved[pc] = at - ((b.size() + 15) & ~size_t(15));
        const u8 op = b[pc];
        at += op == kDrawFactors || op == kPlainFactors ? 10 : op == kJump8 ? 3 : n;
    }
    Bytes c;
    const size_t base = (b.size() + 15) & ~size_t(15);
    auto place = [&](size_t target) -> size_t {
        const auto it = moved.find(target);
        if (it == moved.end()) throw FormatError("MDLS draw program jumps inside a command");
        return base + it->second;
    };
    auto rel16 = [&](size_t from, size_t target) {
        const s64 r = s64(place(target)) - s64(base + from);
        if (r < -32768 || r > 32767) throw FormatError("MDLS draw program too long to copy");
        c.push_back(u8(r)), c.push_back(u8(u64(r) >> 8));
    };
    auto f32 = [&](float v) {
        u32 w;
        std::memcpy(&w, &v, 4);
        for (int i = 0; i < 4; ++i) c.push_back(u8(w >> (8 * i)));
    };
    for (const auto& [pc, n] : size_at) {
        const u8 op = b[pc];
        const size_t a = pc + 1;
        if (op == kDrawFactors || op == kPlainFactors) {
            c.push_back(kSetC0), f32(1.0f);
            c.push_back(kSetC1), f32(op == kPlainFactors ? -0.0f : 0.0f);
        } else if (op == kJump8 || op == kJump16) {
            c.push_back(kJump16);
            rel16(c.size(), op == kJump8 ? size_t(s64(a) + std::int8_t(b[a])) : size_t(s64(a) + s16_at(d, a)));
        } else if (op == 0x06 || op == 0x4D || op == 0x07) {
            const size_t head = op == 0x07 ? 17 : 3, count = op == 0x07 ? b[a + 16] : b[a + 2];
            c.insert(c.end(), b.begin() + ptrdiff_t(pc), b.begin() + ptrdiff_t(a + head));
            for (size_t k = 0; k < count; ++k) rel16(c.size(), size_t(s64(a + head + 2 * k) + s16_at(d, a + head + 2 * k)));
        } else if (op == 0x0A) {
            const size_t skip = a + 1 + 12 * size_t(b[a]);
            c.insert(c.end(), b.begin() + ptrdiff_t(pc), b.begin() + ptrdiff_t(skip));
            rel16(c.size(), size_t(s64(skip) + s16_at(d, skip)));
        } else {
            c.insert(c.end(), b.begin() + ptrdiff_t(pc), b.begin() + ptrdiff_t(pc + n));
        }
    }
    b.resize(base, 0);
    b.insert(b.end(), c.begin(), c.end());
    return u32(base);
}

// Whether a path of the program reaches a factor command (0x56, 0x57).
bool sets_factors(ByteSpan d, size_t start) {
    std::set<size_t> seen;
    std::vector<size_t> work{start};
    while (!work.empty()) {
        size_t pc = work.back();
        work.pop_back();
        while (pc < d.size() && seen.insert(pc).second) {
            const u8 op = d[pc];
            const size_t a = pc + 1;
            if (op == kDrawFactors || op == kPlainFactors) return true;
            if (op == 0x00) break;
            if (op == 0x06 || op == 0x4D) {
                if (a + 3 > d.size()) break;
                for (u32 k = 0; k < d[a + 2]; ++k) work.push_back(size_t(s64(a + 3 + 2 * k) + s16_at(d, a + 3 + 2 * k)));
                pc = a + 3 + 2 * size_t(d[a + 2]);
            } else if (op == 0x07) {
                if (a + 17 > d.size()) break;
                for (u32 k = 0; k < d[a + 16]; ++k) work.push_back(size_t(s64(a + 17 + 2 * k) + s16_at(d, a + 17 + 2 * k)));
                if (d[a + 16]) break;
                pc = a + 17;
            } else if (op == kJump8) {
                if (a >= d.size()) break;
                pc = size_t(s64(a) + std::int8_t(d[a]));
            } else if (op == kJump16) {
                if (a + 2 > d.size()) break;
                pc = size_t(s64(a) + s16_at(d, a));
            } else if (op == 0x0A) {
                if (a >= d.size()) break;
                const size_t skip = a + 1 + 12 * size_t(d[a]);
                if (skip + 2 > d.size()) break;
                work.push_back(size_t(s64(skip) + s16_at(d, skip)));
                pc = skip + 2;
            } else {
                const int n = operand_bytes(op);
                if (n < 0) break;
                pc = a + size_t(n);
            }
        }
    }
    return false;
}

// A VIF stream being written.
struct Stream {
    Bytes b;
    void word(u32 v) {
        b.resize(b.size() + 4);
        std::memcpy(b.data() + b.size() - 4, &v, 4);
    }
    void unpack(u32 vn, u32 vl, u32 addr, bool masked, u32 count, ByteSpan data) {
        word((0x60u | vn << 2 | vl | (masked ? 0x10u : 0u)) << 24 | (count & 0xFF) << 16 | 0xC000u | addr);
        b.insert(b.end(), data.begin(), data.end());
        while (b.size() & 3) b.push_back(0);
    }
    void floats(u32 comps, u32 addr, bool masked, const std::vector<std::array<float, 4>>& v) {
        Bytes raw(v.size() * comps * 4);
        for (size_t i = 0; i < v.size(); ++i) std::memcpy(raw.data() + i * comps * 4, v[i].data(), comps * 4);
        unpack(comps - 1, 0, addr, masked, u32(v.size()), raw);
    }
};

// One packet as GTM1 has it: the GIF tag, float positions, the strip table, float normals or
// byte colours, float texture coordinates (or the reflection pass's normals in their place), and
// the same again for a second key frame. Returns whether the packet had packed data.
//
// The packet's own layout: VU memory holds 0x40 vectors each of positions (from 0x00), texture
// coordinates (0x40) and normals or colours (0x80), the GIF tag at 0xC0; a packet with two key
// frames splits each range in half. An array may come as several UNPACKs of different widths, each
// continuing at the address the last one stopped, and a packed run may be offsets from the VIF
// row registers (STROW, with STMOD 1; STMOD 2 also moves the row).
bool rewrite_packet(ByteSpan packet, float scale, bool packed_shape, float color_scale, float morph_weight, Stream& out, ConvertStats& st,
                    const SkinBlock* skin = nullptr) {
    using Vec = std::array<float, 4>;
    struct Array {
        std::vector<Vec> v;
        std::vector<bool> set;
        bool any = false;
    };
    Array pos[2], uv[2], env[2], nrm[2];
    std::vector<u32> col[2];
    Bytes tag, strips;
    bool packed = false;
    u32 mode = 0;
    std::array<u32, 4> row{0, 0, 0, 0};
    const std::vector<ps2::VifCommand> cmds = ps2::parse_vif(packet);
    u32 n = 0; // vertices: the GIF tag's loop count
    for (const ps2::VifCommand& c : cmds)
        if (c.is_unpack() && c.unpack().addr == 0xC0 && c.unpack().vn == 2 && c.unpack().vl == 0 && c.payload.size() >= 12)
            n = rd32(c.payload, 0) & 0x7FFF;
    // `whole32`: 32-bit values are integers too (a packed shape's positions too large for 16 bits:
    // a backdrop's), not floats.
    auto place = [&](Array* slot, u32 base, const ps2::VifUnpack& u, float k, std::array<u32, 4>& r, bool whole32 = false) {
        // Key frame 0 holds the vectors below the count; the second half of the range is key frame 1.
        u32 at = u.addr - base;
        Array* a = &slot[0];
        if (at >= n && at >= 0x20) a = &slot[1], at -= 0x20;
        if (at + u.count > 0x40) return false;
        if (a->v.size() < at + u.count) a->v.resize(at + u.count, Vec{0, 0, 0, 0}), a->set.resize(at + u.count, false);
        std::vector<std::array<u32, 4>> w = ps2::unpack_words(u);
        for (u32 i = 0; i < u.count; ++i) {
            Vec f{0, 0, 0, 0};
            for (u32 j = 0; j < u.components(); ++j) {
                if (u.vl == 0 && !whole32) {
                    std::memcpy(&f[j], &w[i][j], 4);
                    continue;
                }
                u32 v = w[i][j];
                if (mode == 1 || mode == 2) v += r[j];
                if (mode == 2) r[j] = v;
                f[j] = float(s32(v)) * k;
            }
            a->v[at + i] = f, a->set[at + i] = true;
        }
        a->any = true;
        return true;
    };
    for (const ps2::VifCommand& c : cmds) {
        if (c.cmd() == ps2::kVifStmod) mode = c.imm() & 3;
        if (c.cmd() == ps2::kVifStrow && c.payload.size() >= 16)
            for (int k = 0; k < 4; ++k) row[size_t(k)] = rd32(c.payload, size_t(k) * 4);
        if (!c.is_unpack()) continue;
        const ps2::VifUnpack u = c.unpack();
        const u32 a = u.addr;
        bool known = true;
        if (a == 0xC0 && u.count == 1 && u.vn == 2 && u.vl == 0) {
            tag.assign(u.data.begin(), u.data.begin() + 12);
        } else if (a >= 0x40 && a < 0x80 && u.vn == 0 && u.vl == 2 && strips.empty()) {
            strips.assign(u.data.begin(), u.data.begin() + u.count);
        } else if (a < 0x40 && u.vn == 2) {
            packed |= u.vl != 0 || packed_shape;
            known = place(pos, 0x00, u, scale, row, packed_shape);
        } else if (a >= 0x40 && a < 0x80 && u.vn == 1) {
            packed |= u.vl != 0 || packed_shape;
            known = place(uv, 0x40, u, kUvScale, row, packed_shape);
        } else if (a >= 0x40 && a < 0x80 && u.vn == 2) {
            packed |= u.vl != 0;
            known = place(env, 0x40, u, kNormalScale, row);
        } else if (a >= 0x80 && a < 0xC0 && u.vn == 2) {
            packed |= u.vl != 0;
            known = place(nrm, 0x80, u, kNormalScale, row);
        } else if (a >= 0x80 && a < 0xC0 && u.vn == 3 && u.vl == 2) {
            u32 at = a - 0x80, key = 0;
            if (at >= n && at >= 0x20) key = 1, at -= 0x20;
            if (col[key].size() < at + u.count) col[key].resize(at + u.count, 0);
            for (u32 i = 0; i < u.count; ++i) {
                u32 c = rd32(u.data, size_t(i) * 4);
                if (color_scale != 1.0f) { // (the alpha byte stays)
                    u32 scaled_c = c & 0xFF000000u;
                    for (int k = 0; k < 3; ++k)
                        scaled_c |= std::min<u32>(255, u32(float((c >> (8 * k)) & 0xFF) * color_scale + 0.5f)) << (8 * k);
                    c = scaled_c;
                }
                col[key][at + i] = c;
            }
        } else {
            known = false;
        }
        if (!known) {
            ++st.unknown_unpacks;
            static const char* const kNames[] = {"S", "V2", "V3", "V4"};
            char name[32];
            std::snprintf(name, sizeof name, "%s-%d @%02x", kNames[u.vn], 32 >> u.vl, a);
            ++st.unknown_kinds[name];
        }
    }
    // A skinned packet: its vertices in the stored pose from the block after the VIF data, the
    // normals where the block says (lit: 0x80; the reflection pass: 0x40, in the UV's place).
    if (skin) {
        pos[0] = {}, nrm[0] = {}, env[0] = {};
        for (const SkinVertex& v : skin->vertices) {
            pos[0].v.push_back({v.pos[0], v.pos[1], v.pos[2], 1}), pos[0].set.push_back(true);
            Array& normals = skin->dest == 0x40 ? env[0] : nrm[0];
            if (skin->dest) normals.v.push_back({v.normal[0], v.normal[1], v.normal[2], 0}), normals.set.push_back(true), normals.any = true;
        }
        pos[0].any = !skin->vertices.empty();
        packed = true;
    }
    // The shape between the two key shapes, when the draw's weight is known.
    if (morph_weight >= 0 && pos[1].any) {
        const float w = std::min(morph_weight, 1.0f);
        auto blend = [&](Array* a) {
            if (!a[0].any || !a[1].any) return;
            for (size_t i = 0; i < a[0].v.size() && i < a[1].v.size(); ++i)
                for (size_t j = 0; j < 4; ++j) a[0].v[i][j] += (a[1].v[i][j] - a[0].v[i][j]) * w;
        };
        blend(pos), blend(uv), blend(env), blend(nrm);
        for (size_t i = 0; i < col[0].size() && i < col[1].size(); ++i) {
            u32 c = 0;
            for (int k = 0; k < 4; ++k) {
                const float x = float((col[0][i] >> (8 * k)) & 0xFF), y = float((col[1][i] >> (8 * k)) & 0xFF);
                c |= u32(x + (y - x) * w + 0.5f) << (8 * k);
            }
            col[0][i] = c;
        }
        pos[1] = {}, uv[1] = {}, env[1] = {}, nrm[1] = {}, col[1].clear();
        packed = true; // (written again in any case)
    }
    if (!tag.empty()) out.unpack(2, 0, 0xC0, false, 1, tag);
    for (int key = 0; key < 2; ++key) {
        const u32 shift = key ? 0x20 : 0;
        if (pos[key].any) out.floats(3, 0x00 + shift, false, pos[key].v);
        if (!key && !strips.empty()) out.unpack(0, 2, 0x40, true, u32(strips.size()), strips);
        if (nrm[key].any) out.floats(3, 0x80 + shift, false, nrm[key].v);
        if (!col[key].empty()) {
            Bytes raw(col[key].size() * 4);
            std::memcpy(raw.data(), col[key].data(), raw.size());
            out.unpack(3, 2, 0x80 + shift, false, u32(col[key].size()), raw);
        }
        if (uv[key].any) out.floats(2, 0x40 + shift, true, uv[key].v);
        if (env[key].any) out.floats(3, 0x40 + shift, true, env[key].v);
    }
    while (out.b.size() & 15) out.b.push_back(0); // VIF no-ops up to the quadword
    return packed;
}

// A rewritten packet has a second key frame (vectors at 0x20, 0x60 or 0xA0).
bool has_second_key(ByteSpan packet) {
    for (const ps2::VifCommand& c : ps2::parse_vif(packet))
        if (c.is_unpack() && (c.unpack().addr == 0x20 || c.unpack().addr == 0x60 || c.unpack().addr == 0xA0)) return true;
    return false;
}

} // namespace

bool is_mdls(ByteSpan d) { return d.size() >= 0x80 && gt3r::has_magic(d, "MDLS"); }

Header read_header(ByteSpan d) {
    if (!is_mdls(d)) throw FormatError("not an MDLS model set");
    Header h;
    h.size = rd32(d, 0x10);
    if (h.size < 0x80 || h.size > d.size()) throw FormatError("MDLS: bad size");
    h.models = rd16(d, 0x16), h.shapes = rd16(d, 0x18), h.materials = rd16(d, 0x1A);
    h.texture_sets = rd16(d, 0x1C), h.texture_lists = rd16(d, 0x1E);
    h.variations = d[0x2C], h.bones = rd16(d, 0x2E);
    h.model_table = rd32(d, 0x38), h.shape_table = rd32(d, 0x3C), h.material_table = rd32(d, 0x40);
    h.texture_table = rd32(d, 0x44), h.variation_table = rd32(d, 0x60);
    h.script = rd32(d, 0x5C) != 0;
    auto in = [&](u32 off, u64 bytes) { return off <= h.size && h.size - off >= bytes; };
    if (!in(h.model_table, u64(h.models) * 0x28) || !in(h.shape_table, u64(h.shapes) * 4) ||
        !in(h.material_table, u64(h.materials) * 0x50) || !in(h.texture_table, u64(h.texture_lists) * 4))
        throw FormatError("MDLS: table out of range");
    return h;
}

std::vector<size_t> find_model_sets(ByteSpan file) {
    std::vector<size_t> out;
    for (size_t o = 0; o + 0x80 <= file.size(); o += 16) {
        if (std::memcmp(file.data() + o, "MDLS", 4) != 0) continue;
        const u32 size = rd32(file, o + 0x10);
        if (size < 0x80 || size > file.size() - o) continue;
        out.push_back(o);
        o += ((size_t(size) + 15) & ~size_t(15)) - 16; // (the loop steps over the last 16)
    }
    return out;
}

Bytes to_gtm1(ByteSpan mdls, ConvertStats* stats, const Scene& scene) {
    const Header h = read_header(mdls);
    ConvertStats local;
    ConvertStats& st = stats ? *stats : local;
    const ByteSpan src = mdls.first(h.size);
    Bytes out(src.begin(), src.end());

    // Draw programs: one per model (model record +0x18).
    std::vector<u32> programs(h.models);
    std::set<u32> wanted_shapes;
    std::map<u32, u16> weight_of;
    const bool some = !scene.only_models.empty();
    std::vector<bool> wanted(h.models, !some);
    for (u32 m : scene.only_models)
        if (m < h.models) wanted[m] = true;
    for (u32 i = 0; i < h.models; ++i) {
        programs[i] = rd32(src, h.model_table + 0x28 * size_t(i) + 0x18);
        if (programs[i] && wanted[i]) patch_program(out, programs[i], st, scene, some ? &wanted_shapes : nullptr, scene.morph_weights.empty() ? nullptr : &weight_of);
    }

    // Shapes: GTM1's chunk layout, +0x04 the scale of packed positions. A shape with packed data
    // is written again at the end with float data.
    std::vector<u32> chunks(h.shapes);
    for (u32 s = 0; s < h.shapes; ++s) {
        const u32 so = rd32(src, h.shape_table + 4 * size_t(s));
        if (so > h.size || h.size - so < 0x10) throw FormatError("MDLS: shape out of range");
        if (some && !wanted_shapes.count(s)) { // (not drawn by the models asked for)
            chunks[s] = so;
            continue;
        }
        float scale;
        std::memcpy(&scale, src.data() + so + 4, 4);
        const u32 count = rd16(src, so + 10);
        if (h.size - so - 0x10 < u64(count) * 8) throw FormatError("MDLS: shape packets out of range");
        ++st.shapes;
        std::vector<Stream> packets(count);
        bool packed = false, morph = false;
        float weight = scene.morph_weight;
        if (const auto w = weight_of.find(s); w != weight_of.end())
            if (const auto given = scene.morph_weights.find(w->second); given != scene.morph_weights.end()) weight = given->second;
        for (u32 k = 0; k < count; ++k) {
            const size_t e = so + 0x10 + 8 * size_t(k);
            const u32 po = so + rd32(src, e), qwc = rd16(src, e + 4);
            if (po > h.size || h.size - po < u64(qwc) * 16) throw FormatError("MDLS: packet out of range");
            ++st.packets;
            const u32 shape_flags = rd16(src, so + 8);
            std::optional<SkinBlock> skin;
            if (shape_flags & kShapeSkinned) skin = read_skin_block(src, po + size_t(qwc) * 16, (shape_flags & kShapeSkinPacked) != 0);
            const bool p = rewrite_packet(src.subspan(po, size_t(qwc) * 16), scale, (shape_flags & 0x80) != 0 && scale > 0, scene.color_scale,
                                          weight, packets[k], st, skin ? &*skin : nullptr);
            st.packed_packets += p;
            packed |= p || scene.color_scale != 1.0f; // (scaled colours are written again too)
            morph |= has_second_key(packets[k].b);
        }
        // The kind (bits 0-4) as GTM1 has it, with its mark of a shape with two key frames.
        const u32 flags = rd16(src, so + 8);
        // (A skinned shape's marks 0x8000 / 0x4000 / 0x2000 say how its blocks are stored: they
        // are written out as plain vertices.)
        const u32 gtm1_flags = (flags & ((flags & kShapeSkinned) ? 0x1F1F : 0xFF1F)) | (morph ? 0x40u : 0u);
        if (!packed) {
            chunks[s] = so;
            put16(out, so + 8, gtm1_flags);
            put32(out, so + 4, 0);
            continue;
        }
        while (out.size() & 15) out.push_back(0);
        const size_t at = out.size();
        chunks[s] = u32(at);
        out.resize(at + 0x10 + 8 * size_t(count), 0);
        put16(out, at + 8, gtm1_flags);
        put16(out, at + 10, count);
        put16(out, at + 12, rd16(src, so + 12));
        put16(out, at + 14, rd16(src, so + 14));
        while (out.size() & 15) out.push_back(0);
        for (u32 k = 0; k < count; ++k) {
            const size_t e = at + 0x10 + 8 * size_t(k);
            if (packets[k].b.size() / 16 > 0xFFFF) throw FormatError("MDLS: packet too large");
            put32(out, e, u32(out.size() - at));
            put16(out, e + 4, u32(packets[k].b.size() / 16));
            put16(out, e + 6, rd16(src, so + 0x10 + 8 * size_t(k) + 6));
            out.insert(out.end(), packets[k].b.begin(), packets[k].b.end());
        }
    }

    // The programs that switch the reflection factors, copied with GTM1's commands for them.
    for (u32 i = 0; i < h.models; ++i)
        if (programs[i] && wanted[i] && sets_factors(out, programs[i])) programs[i] = relink_program(out, programs[i]);

    // The tables GTM1 keeps apart: the programs' offsets, a sphere per model (the model record's
    // centre and radius) and the shapes' offsets.
    while (out.size() & 15) out.push_back(0);
    if (some && scene.others_empty) { // (a program of its end alone)
        for (u32 i = 0; i < h.models; ++i)
            if (!wanted[i]) programs[i] = u32(out.size());
        out.resize(out.size() + 16, 0);
    }
    const size_t node_tab = out.size();
    out.resize(node_tab + 4 * size_t(h.models), 0);
    for (u32 i = 0; i < h.models; ++i) put32(out, node_tab + 4 * size_t(i), programs[i]);
    while (out.size() & 15) out.push_back(0);
    const size_t sphere_tab = out.size();
    out.resize(sphere_tab + 16 * size_t(h.models), 0);
    for (u32 i = 0; i < h.models; ++i) {
        const size_t rec = h.model_table + 0x28 * size_t(i);
        std::memcpy(out.data() + sphere_tab + 16 * size_t(i), src.data() + rec + 0x0C, 12);
        std::memcpy(out.data() + sphere_tab + 16 * size_t(i) + 12, src.data() + rec + 0x08, 4);
    }
    const size_t chunk_tab = out.size();
    out.resize(chunk_tab + 4 * size_t(h.shapes), 0);
    for (u32 s = 0; s < h.shapes; ++s) put32(out, chunk_tab + 4 * size_t(s), chunks[s]);

    // The header. The lists of texture sets (+0x44 -> u32 lists[] -> u32 sets[]) are GTM1's
    // texture-LOD tables as they are, and the material variations (+0x60) its paint sets.
    std::fill(out.begin(), out.begin() + 0x44, u8(0));
    std::memcpy(out.data(), "GTM1", 4);
    put16(out, 0x10, h.models), put16(out, 0x12, h.shapes), put16(out, 0x14, h.materials);
    put16(out, 0x16, h.texture_sets), put16(out, 0x18, h.texture_lists), put16(out, 0x1A, h.variations);
    put32(out, 0x20, u32(node_tab)), put32(out, 0x24, u32(chunk_tab)), put32(out, 0x28, h.material_table);
    put32(out, 0x2C, h.texture_lists ? rd32(src, h.texture_table) : 0);
    put32(out, 0x30, u32(sphere_tab)), put32(out, 0x34, h.texture_lists ? h.texture_table : 0);
    put32(out, 0x38, h.variations ? h.variation_table : 0);
    return out;
}

gt3r::scene::Model build_model(ByteSpan mdls, const gt3r::gtm1::Options& opt, const Scene& scene, const std::string& name) {
    const Bytes block = to_gtm1(mdls, nullptr, scene);
    return gt3r::gtm1::decode_gtm1(block, opt, name);
}

gt3r::scene::Model build_model(ByteSpan mdls, const Options& opt, const std::string& name) {
    const Bytes block = to_gtm1(mdls, nullptr, opt.scene);
    gt3r::gtm1::Options o;
    o.node = opt.model;
    o.run.lod = opt.lod;
    o.run.texture_lod = opt.texture_list;
    o.textures = opt.textures;
    return gt3r::gtm1::decode_gtm1(block, o, name);
}

} // namespace gt4r::mdls
