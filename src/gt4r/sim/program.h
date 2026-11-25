#pragma once

// The game program's own tables that the simulation reads (docs/specs/physics.md "Program tables"):
// read from the user's CORE.GT4 when a race is set up, never kept in the repository. The tables are
// at known places in Gran Turismo 4 SCUS-97328 v1.01, the build the specification was written from;
// that build is recognized by its code (a hash of the routines that address the tables). For any
// other program (Prologue, another release) known() is false and the simulation runs on the shared
// engine's own values (Prologue's own tables where program_prologue.h places them).

#include "disc/core_image.h"
#include "gt4r/base/types.h"

#include <memory>
#include <string>

namespace gt4r {
class Disc;
}

namespace gt4r::sim {

class Program {
public:
    Program() = default;
    // From the disc's packed program; never throws (a program that cannot be read is unknown).
    static Program load(Disc& disc);
    // From a program image loaded at `load_address`.
    static Program from_image(Bytes image, u32 load_address);
    static Program from_core(std::shared_ptr<const gt3r::CoreImage> core);

    bool known() const { return known_; }
    // Why the tables are not the disc's ("the disc's program is not a build whose tables are mapped").
    const std::string& issue() const { return issue_; }

    // The image as loaded at base() (kept for a build that is not known too: program_prologue.h).
    ByteSpan image() const { return core_ ? ByteSpan(core_->program) : ByteSpan{}; }
    u32 base() const { return base_; }
    // The image's bytes at a program address (0 outside it).
    u8 b(u32 addr) const;
    u16 h(u32 addr) const { return u16(b(addr) | (b(addr + 1) << 8)); }
    u32 w(u32 addr) const { return u32(h(addr)) | (u32(h(addr + 2)) << 16); }

    // ---- the tables (addresses in SCUS-97328 v1.01) ----
    // The static physics block: Gran Turismo 3's block with 24 surfaces (the per-surface arrays grown
    // from 16 entries, what follows them 0x30 bytes further on).
    static constexpr u32 kStatic = 0x620CD8, kStaticSize = 0x160;
    static constexpr u32 kSurfaces = 24;
    // The spec builder's defaults: the inertia shares (percent of the yaw byte: pitch, roll) and the
    // defaults of two spec bytes.
    static constexpr u32 kPitchInertiaPct = 0x620E15, kRollInertiaPct = 0x620E16;
    static constexpr u32 kSpec11CDefault = 0x620E22, kSpec112Default = 0x620E23;
    // A rally car's changes on a loose course (6 bytes) and the ASCC switch byte after them.
    static constexpr u32 kLooseSetup = 0x6224A0, kAsccSwitch = 0x6224A8;
    // The default steering lock curve (x 6 bytes, y 6 bytes at +8) and tire curve B (the same shape).
    static constexpr u32 kSteerCurve = 0x6A4028, kTireCurveB = 0x622420;
    // A material's soft flag (24 words).
    static constexpr u32 kSoft = 0x622430;

private:
    std::shared_ptr<const gt3r::CoreImage> core_; // (shared: every Program of a disc holds the one image)
    u32 base_ = 0;
    bool known_ = false;
    std::string issue_ = "no program";
};

} // namespace gt4r::sim
