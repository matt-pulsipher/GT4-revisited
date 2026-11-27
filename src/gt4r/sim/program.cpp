#include "gt4r/sim/program.h"

#include "gt4r/sim/program_prologue.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "gt4r/disc/disc.h"

#include <atomic>
#include <exception>
#include <map>
#include <mutex>

namespace gt4r::sim {

namespace {

const TableCheck kNoCheck;
constexpr const char* kPrologue = "Gran Turismo 4 Prologue's program, whose own tables serve";

// Each disc program's check, made once (by the image it was made of).
std::shared_ptr<const TableCheck> checked(const std::shared_ptr<const gt3r::CoreImage>& core) {
    static std::mutex m;
    static std::map<const gt3r::CoreImage*, std::pair<std::weak_ptr<const gt3r::CoreImage>, std::shared_ptr<const TableCheck>>> done;
    std::lock_guard<std::mutex> lock(m);
    for (auto it = done.begin(); it != done.end();) it = it->second.first.expired() ? done.erase(it) : std::next(it);
    auto& slot = done[core.get()];
    if (slot.second && !slot.first.expired()) return slot.second;
    auto c = std::make_shared<const TableCheck>(check_tables(core->program, core->load_address));
    for (const std::string& line : c->lines()) {
        if (c->differ() || c->not_found()) LOG_WARN("Program tables: %s", line.c_str());
        else LOG_INFO("Program tables: %s", line.c_str());
    }
    slot = {core, c};
    return c;
}

} // namespace

Program Program::from_image(Bytes image, u32 load_address) {
    auto core = std::make_shared<gt3r::CoreImage>();
    core->program = std::move(image), core->load_address = load_address;
    Program p;
    p.core_ = std::move(core);
    p.base_ = p.core_->load_address;
    if (PrologueProgram::locate(p).recognized()) {
        p.issue_ = kPrologue;
        return p;
    }
    p.known_ = reference_build(p.image(), p.base_);
    p.issue_ = p.known_ ? "" : "the image is not SCUS-97328 v1.01's program";
    return p;
}

Program Program::from_core(std::shared_ptr<const gt3r::CoreImage> core) { return from_core(std::move(core), false); }

Program Program::from_core(std::shared_ptr<const gt3r::CoreImage> core, bool prologue_disc) {
    Program p;
    p.core_ = std::move(core);
    p.base_ = p.core_->load_address;
    if (PrologueProgram::locate(p).recognized()) {
        p.issue_ = kPrologue;
        return p;
    }
    if (prologue_disc) { // (never Gran Turismo 4's tables, nor the shared engine's, for a Prologue build not placed)
        p.issue_ = "a Gran Turismo 4 Prologue build whose tables are not placed";
        return p;
    }
    p.known_ = p.ours_ = true;
    p.issue_ = "";
    p.check_ = checked(p.core_);
    p.release_known_ = p.check_->release.has_value();
    p.release_ = p.check_->release.value_or(Release::Us);
    return p;
}

Program Program::load(Disc& disc) {
    try {
        return from_core(disc.program_image(), disc.info().game == Game::Prologue);
    } catch (const std::exception& e) {
        Program p;
        p.issue_ = std::string("the disc's program cannot be read: ") + e.what();
        return p;
    }
}

const TableCheck& Program::check() const { return check_ ? *check_ : kNoCheck; }

u8 Program::b(u32 addr) const {
    if (ours_) {
        if (const ProgramTable* t = gt4_table_at(addr, release_)) return t->bytes[addr - t->address];
        if (core_ && addr >= base_ && addr - base_ < core_->program.size()) {
            static std::atomic<int> told{0};
            if (told.fetch_add(1) < 8) LOG_WARN("Program tables: a read at 0x%06X, outside Gran Turismo 4's tables (0)", addr);
        }
        return 0;
    }
    if (!core_ || addr < base_ || addr - base_ >= core_->program.size()) return 0;
    return core_->program[addr - base_];
}

} // namespace gt4r::sim
