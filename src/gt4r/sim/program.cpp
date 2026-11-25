#include "gt4r/sim/program.h"

#include "gt4r/sim/program_prologue.h"

#include "disc/core_image.h"
#include "gt4r/disc/disc.h"

#include <exception>

namespace gt4r::sim {

namespace {

// FNV-1a over a stretch of the image.
u64 hash(ByteSpan image, u32 base, u32 from, u32 to) {
    if (from < base || to > base + image.size() || from >= to) return 0;
    u64 h = 0xcbf29ce484222325ull;
    for (u32 a = from; a < to; ++a) h = (h ^ image[a - base]) * 0x100000001b3ull;
    return h;
}

// The routines that address the tables in SCUS-97328 v1.01: the spec builder, its wrapper with the
// fix-ups and the converter.
struct Routine {
    u32 from, to;
    u64 hash;
};
constexpr Routine kRoutines[] = {
    {0x4413C8, 0x44194C, 0x74b88b8726c105c9ull},
    {0x3F3B68, 0x3F4078, 0xca526b3daf3e7935ull},
    {0x361908, 0x362DF8, 0xddee4b2a031785e5ull},
};

} // namespace

Program Program::from_image(Bytes image, u32 load_address) {
    auto core = std::make_shared<gt3r::CoreImage>();
    core->program = std::move(image), core->load_address = load_address;
    return from_core(std::move(core));
}

Program Program::from_core(std::shared_ptr<const gt3r::CoreImage> core) {
    Program p;
    p.core_ = std::move(core);
    p.base_ = p.core_->load_address;
    p.known_ = true;
    for (const Routine& r : kRoutines)
        if (hash(p.image(), p.base_, r.from, r.to) != r.hash) p.known_ = false;
    // The static block's shape: a rolling-resistance curve of 1 to 8 rising speeds.
    if (p.known_) {
        const u32 n = p.b(kStatic + 0x0B);
        if (n < 1 || n > 8) p.known_ = false;
        for (u32 i = 1; i < n && p.known_; ++i)
            if (p.b(kStatic + 0x0C + i) < p.b(kStatic + 0x0C + i - 1)) p.known_ = false;
    }
    p.issue_ = p.known_ ? "" : "the disc's program is not a build whose tables are mapped";
    if (!p.known_ && PrologueProgram::locate(p).recognized())
        p.issue_ = "Gran Turismo 4 Prologue's program, whose own physics block, driver sets and spec defaults replace Gran Turismo 3's; for the rest";
    return p;
}

Program Program::load(Disc& disc) {
    try {
        return from_core(disc.program_image());
    } catch (const std::exception& e) {
        Program p;
        p.issue_ = std::string("the disc's program cannot be read: ") + e.what();
        return p;
    }
}

u8 Program::b(u32 addr) const {
    if (!core_ || addr < base_ || addr - base_ >= core_->program.size()) return 0;
    return core_->program[addr - base_];
}

} // namespace gt4r::sim
