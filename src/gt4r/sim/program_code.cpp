#include "gt4r/sim/program_code.h"

#include "base/bytes.h"

#include <algorithm>
#include <array>

namespace gt4r::sim {

namespace {

constexpr u64 kBase = 1099511628211ull; // the rolling hash's base (odd)

// An upper half that forms an address in the EE's memory above the program's 1 MB load address.
bool address_half(u32 imm) { return imm >= 0x10 && imm < 0x200; }
bool load_op(u32 op) {
    switch (op) {
    case 0x1A: case 0x1B: case 0x1E: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x31: case 0x36: case 0x37:
        return true;
    default: return false;
    }
}
bool store_op(u32 op) {
    switch (op) {
    case 0x1F: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x39: case 0x3E: case 0x3F:
        return true;
    default: return false;
    }
}
// The I-type ops that write a general register rt.
bool writes_rt(u32 op) {
    return (op >= 0x08 && op <= 0x0E) || op == 0x18 || op == 0x19 || op == 0x1A || op == 0x1B || op == 0x1E || (op >= 0x20 && op <= 0x27) || op == 0x37;
}
bool caller_saved(u32 r) { return (r >= 1 && r <= 15) || r == 24 || r == 25 || r == 31; }

} // namespace

ProgramCode::ProgramCode(ByteSpan program, u32 load_address) : load_(load_address) {
    const u32 n_words = u32(program.size() / 4);
    u32 last = 0;
    for (u32 k = 0; k < n_words; ++k) {
        if (gt3r::rd32(program, 4 * k) != 0x03E00008) continue;
        if (last && 4 * (k - last) > 0x20000) break; // (Gran Turismo 4's code has runs of 16 .. 54 KB without one)
        last = k;
    }
    text_end_ = std::min<u32>(load_ + u32(program.size()), (load_ + 4 * last + 8 + 15) & ~15u);
    const u32 code = (text_end_ - load_) / 4;
    masked_.resize(code), address_.assign(code, 0);
    // What each register holds of an address: the upper half it was given (an index added or not).
    std::array<u32, 32> hi{};
    int reset_at = -1, reset_kind = 0; // after a jump's delay slot: every register (1) or the caller-saved ones (2)
    auto clear = [&](u32 r) { hi[r] = 0; };
    for (u32 i = 0; i < code; ++i) {
        if (int(i) == reset_at) {
            for (u32 r = 0; r < 32; ++r)
                if (reset_kind == 1 || caller_saved(r)) hi[r] = 0;
            reset_at = -1;
        }
        const u32 w = gt3r::rd32(program, 4 * i), op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31;
        const u32 imm = w & 0xFFFF;
        u32 m = w;
        if (op == 0x02 || op == 0x03) { // a jump or a call
            m = op << 26;
            reset_at = int(i) + 2, reset_kind = op == 0x02 ? 1 : 2;
        } else if (op == 0x00) {
            const u32 funct = w & 0x3F;
            if (funct == 0x08) reset_at = int(i) + 2, reset_kind = 1;                // a jump to a register
            else if (funct == 0x09) reset_at = int(i) + 2, reset_kind = 2, clear(rd); // a call through a register
            else if ((funct == 0x21 || funct == 0x2D) && rd && (hi[rs] != 0) != (hi[rt] != 0)) hi[rd] = hi[rs] | hi[rt]; // an index added
            else if (funct != 0x0C && funct != 0x0D && funct != 0x0F) clear(rd);
        } else if (op == 0x01) { // the branch group: its linking branches call
            if (rt == 0x10 || rt == 0x11 || rt == 0x12 || rt == 0x13) reset_at = int(i) + 2, reset_kind = 2;
        } else if (op == 0x0F) { // an upper half
            if (address_half(imm)) {
                m = w & 0xFFFF0000;
                if (rt) hi[rt] = imm << 16;
            } else {
                clear(rt);
            }
        } else if (op == 0x1C) { // the multimedia group
            clear(rd);
        } else if (op == 0x11 || op == 0x12) { // FPU / VU0 moves into a general register
            if (rs == 0 || rs == 1 || rs == 2) clear(rt);
        } else {
            const bool arith = op == 0x09 || op == 0x19 || op == 0x0D;
            if ((load_op(op) || store_op(op) || arith) && hi[rs]) {
                m = w & 0xFFFF0000;
                address_[i] = op == 0x0D ? (hi[rs] | imm) : hi[rs] + u32(s32(s16(imm)));
            }
            if (writes_rt(op)) clear(rt);
        }
        masked_[i] = m;
    }
    prefix_.assign(code + 1, 0);
    power_.assign(code + 1, 1);
    for (u32 i = 0; i < code; ++i) {
        prefix_[i + 1] = prefix_[i] * kBase + u64(masked_[i]) + 1;
        power_[i + 1] = power_[i] * kBase;
    }
}

u64 ProgramCode::hash(u32 at, u32 words) const {
    if (!in_code(at, words)) return 0;
    const size_t i = (at - load_) / 4;
    return prefix_[i + words] - prefix_[i] * power_[words];
}

std::vector<u32> ProgramCode::find(u64 h, u32 words) const {
    std::vector<u32> out;
    if (words == 0 || words > masked_.size()) return out;
    const u64 pn = power_[words];
    for (size_t i = 0; i + words <= masked_.size(); ++i)
        if (prefix_[i + words] - prefix_[i] * pn == h) out.push_back(load_ + u32(4 * i));
    return out;
}

u32 ProgramCode::address_at(u32 pc) const { return in_code(pc) ? address_[(pc - load_) / 4] : 0; }

u32 ProgramCode::address_in(u32 at, u32 index, u32 offset) const {
    const u32 a = address_at(at + 4 * index);
    return a ? a - offset : 0;
}

} // namespace gt4r::sim
