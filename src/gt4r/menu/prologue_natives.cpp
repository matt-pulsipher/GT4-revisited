// What Gran Turismo 4 Prologue's scripts ask of the program that Gran Turismo 4's do not (the license
// record: prologue_license.cpp). docs/formats/prologue.md "The menus".

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/runtime_internal.h"

#include "disc/core_image.h"
#include "gt4r/sim/program_prologue.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>

namespace gt4r::menu {

using script::Args;
using script::Value;
using script::Vm;

// ---- The switch actor's colours (MSwitchActor's update, 0x2EB848): its button's `base`,
// `text` and `ring` take a colour by the button's state each picture. The colours are the
// program's own, made by its startup (0x2EBC48: each a colour object at 0x70C218 + 0x10 n built from
// four floats); they are read here by following that routine's constant loads, not kept.
// docs/formats/prologue.md "The menus".

namespace {

constexpr u32 kColorsMaker = 0x2EBC48, kColorsMakerEnd = 0x2EC600; // the routine that makes them
constexpr u32 kColorConstructor = 0x1D74B0;                       // (r, g, b, a) into the object at $a0

// The floats each colour object gets, by its address: the routine's lui / ori / addiu / mtc1 /
// mov.s followed in order, a constructor's call recorded with its delay slot done.
std::map<u32, std::array<float, 4>> colors_made(ByteSpan image, u32 load) {
    std::map<u32, std::array<float, 4>> out;
    if (kColorsMaker < load || kColorsMakerEnd - load > image.size()) return out;
    std::array<std::optional<u32>, 32> gpr{};
    std::array<std::optional<float>, 32> fpr{};
    gpr[0] = 0;
    auto word = [&](u32 at) {
        u32 w;
        std::memcpy(&w, image.data() + (at - load), 4);
        return w;
    };
    auto as_float = [](u32 bits) {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    };
    auto step = [&](u32 w) {
        const u32 op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        const u16 imm = u16(w);
        if (op == 0x0F) gpr[rt] = u32(imm) << 16;                                     // lui
        else if (op == 0x0D) gpr[rt] = gpr[rs] ? std::optional<u32>(*gpr[rs] | imm) : std::nullopt; // ori
        else if (op == 0x09) gpr[rt] = gpr[rs] ? std::optional<u32>(*gpr[rs] + u32(s32(s16(imm)))) : std::nullopt; // addiu
        else if (op == 0x11 && rs == 4) fpr[(w >> 11) & 31] = gpr[rt] ? std::optional<float>(as_float(*gpr[rt])) : std::nullopt; // mtc1
        else if (op == 0x11 && rs == 16 && (w & 63) == 6) fpr[(w >> 6) & 31] = fpr[(w >> 11) & 31];                 // mov.s
        else if (op == 0) gpr[(w >> 11) & 31].reset();                   // (a register operation: its result unknown)
        else if ((op >= 0x18 && op <= 0x27) || op == 0x37) gpr[rt].reset(); // (loads)
        gpr[0] = 0;
    };
    for (u32 at = kColorsMaker; at + 8 <= kColorsMakerEnd; at += 4) {
        const u32 w = word(at);
        if ((w >> 26) == 0x03 && ((w & 0x3FFFFFF) << 2) == kColorConstructor) {
            step(word(at + 4)); // the delay slot
            at += 4;
            if (gpr[4] && fpr[12] && fpr[13] && fpr[14] && fpr[15]) out[*gpr[4]] = {*fpr[12], *fpr[13], *fpr[14], *fpr[15]};
            continue;
        }
        step(w);
    }
    return out;
}

} // namespace

std::optional<SwitchColors> prologue_switch_colors(const RuntimeConfig& config) {
    if (!config.program && !config.program_image) return std::nullopt;
    try {
        const std::shared_ptr<const gt3r::CoreImage> image = config.image();
        const gt3r::CoreImage& core = *image;
        if (!sim::PrologueProgram::locate(core.program, core.load_address).recognized()) return std::nullopt;
        const std::map<u32, std::array<float, 4>> made = colors_made(core.program, core.load_address);
        // The update's choices (0x2EB848): by the button's active (1) and insensitive (8) bits;
        // the ring's blink while it has the focus, and its colours without it.
        SwitchColors c;
        auto at = [&made](u32 address, std::array<float, 4>& into) {
            const auto it = made.find(address);
            if (it == made.end()) return false;
            into = it->second;
            return true;
        };
        const bool all = at(0x70C238, c.base_active) && at(0x70C248, c.base_idle) && at(0x70C278, c.base_insensitive_active) &&
                         at(0x70C288, c.base_insensitive) && at(0x70C258, c.text_active) && at(0x70C268, c.text_idle) &&
                         at(0x70C298, c.text_insensitive_active) && at(0x70C2A8, c.text_insensitive) && at(0x70C238, c.ring_blink_on) &&
                         at(0x70C228, c.ring_blink_off) && at(0x70C288, c.ring_insensitive) && at(0x70C248, c.ring_idle);
        if (!all) return std::nullopt;
        return c;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace gt4r::menu
