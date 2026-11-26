#pragma once

// The game program's own tables that the simulation reads (docs/specs/physics.md "Program tables"). On a Gran Turismo 4
// disc, whatever its build, they are Gran Turismo 4's own as program_tables.h carries them (SCUS-97328 v1.01's), read by
// their address in that build; the disc's program is checked against them once (check(): what differs or is not found is
// logged). On a Prologue disc the image's own bytes serve (program_prologue.h places Prologue's tables). A program image
// given as it is (from_image: the interpreter's memory) serves its own bytes.

#include "disc/core_image.h"
#include "gt4r/base/types.h"
#include "gt4r/sim/program_tables.h"

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
    // A program image loaded at `load_address`, its own bytes served (the interpreter's memory: known() when it holds
    // SCUS-97328 v1.01's code).
    static Program from_image(Bytes image, u32 load_address);
    // A disc's program: Gran Turismo 4's tables served (ours), or Prologue's image.
    static Program from_core(std::shared_ptr<const gt3r::CoreImage> core);

    // Gran Turismo 4's tables are served (any Gran Turismo 4 program; an image of v1.01's code).
    bool known() const { return known_; }
    // Why they are not ("Gran Turismo 4 Prologue's program, ...", "no program").
    const std::string& issue() const { return issue_; }
    // Our tables served (a disc's Gran Turismo 4 program), not the image's bytes.
    bool ours() const { return ours_; }
    // The disc's program against our tables (empty rows for an image or Prologue's).
    const TableCheck& check() const;
    // The release whose tables are served (program_tables.h): the program's own, SCUS-97328 v1.01's for another build.
    Release release() const { return release_; }
    // Whether the program is one of the releases told apart (else v1.01's tables serve it).
    bool release_known() const { return release_known_; }

    // The image as loaded at base() (kept for a build that is not known too: program_prologue.h).
    ByteSpan image() const { return core_ ? ByteSpan(core_->program) : ByteSpan{}; }
    u32 base() const { return base_; }
    // The byte at a program address: ours of the table holding it (0 outside every table; a read inside the image is
    // logged once), or the image's (0 outside it).
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
    static Program from_core(std::shared_ptr<const gt3r::CoreImage> core, bool prologue_disc);
    std::shared_ptr<const gt3r::CoreImage> core_; // (shared: every Program of a disc holds the one image)
    std::shared_ptr<const TableCheck> check_;     // (shared likewise: made once per image)
    u32 base_ = 0;
    bool known_ = false, ours_ = false, release_known_ = false;
    Release release_ = Release::Us;
    std::string issue_ = "no program";
};

} // namespace gt4r::sim
