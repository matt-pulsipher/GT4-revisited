#include "gt4r/race/animated_models.h"

#include "base/log.h"
#include "gt4r/formats/model/mdls.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace gt4r::race {

namespace {

namespace scene = gt3r::scene;

// What a model's draw program reads of the registers: the first key-shape switch and morph weight;
// `other`: it also moves, turns or colors by registers (not handled here).
struct ProgramUse {
    int switch_cell = -1, weight_cell = -1, cases = 0;
    bool other = false;
    std::set<u32> texture_sets;
};

// Walks a draw program from its start over every path. (The commands' sizes are the converter's:
// docs/formats/model.md "Draw programs".)
ProgramUse scan_program(ByteSpan b, size_t start) {
    ProgramUse use;
    std::set<size_t> seen;
    std::vector<size_t> work{start};
    auto s16_at = [&](size_t o) { return s32(s16(rd16(b, o))); };
    static const auto size = [](u8 op) -> int {
        switch (op) {
        case 0x01: case 0x0B: case 0x0C: case 0x0D: case 0x17: case 0x18: case 0x1A: case 0x1B: case 0x1D: case 0x1E: case 0x22: case 0x23: case 0x25: case 0x26:
        case 0x2D: case 0x2E: case 0x30: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x43: case 0x47:
        case 0x4E: case 0x4F: case 0x56: case 0x57: case 0x58:
            return 0;
        case 0x02: case 0x04: case 0x0E: case 0x19: case 0x1F: case 0x29: case 0x2A: case 0x2B: case 0x2F: case 0x50: case 0x59:
            return 1;
        case 0x03: case 0x05: case 0x1C: case 0x20: case 0x2C: case 0x44: case 0x45: case 0x46: case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x51:
        case 0x5A:
            return 2;
        case 0x41: case 0x42: return 3;
        case 0x14: case 0x15: case 0x16: case 0x21: case 0x24: case 0x27: case 0x32: case 0x34: case 0x35: case 0x36: return 4;
        case 0x54: case 0x55: return 8;
        case 0x11: case 0x12: case 0x31: return 12;
        case 0x13: case 0x28: case 0x33: case 0x37: case 0x52: case 0x53: return 16;
        case 0x0F: case 0x10: return 64;
        default: return -1;
        }
    };
    while (!work.empty()) {
        size_t pc = work.back();
        work.pop_back();
        while (pc < b.size() && seen.insert(pc).second) {
            const u8 op = b[pc];
            const size_t a = pc + 1;
            if (op == 0x00) break;
            if (op == 0x06 || op == 0x4D) {
                if (a + 3 > b.size()) return use;
                const u32 n = b[a + 2];
                const size_t tab = a + 3;
                if (tab + 2 * size_t(n) > b.size()) return use;
                for (u32 k = 0; k < n; ++k) work.push_back(size_t(s64(tab + 2 * k) + s16_at(tab + 2 * k)));
                if (op == 0x4D) {
                    if (use.switch_cell < 0) use.switch_cell = int(rd16(b, a)), use.cases = int(n);
                    else if (use.switch_cell != int(rd16(b, a))) use.other = true; // (a second switch: a person's clip)
                }
                pc = tab + 2 * size_t(n);
                continue;
            }
            if (op == 0x07) {
                if (a + 17 > b.size()) return use;
                const u32 n = b[a + 16];
                for (u32 k = 0; k < n && a + 19 + 2 * k <= b.size(); ++k) work.push_back(size_t(s64(a + 17 + 2 * k) + s16_at(a + 17 + 2 * k)));
                if (n) break;
                pc = a + 17;
                continue;
            }
            if (op == 0x08) {
                if (a + 1 > b.size()) return use;
                pc = size_t(s64(a) + std::int8_t(b[a]));
                continue;
            }
            if (op == 0x09) {
                if (a + 2 > b.size()) return use;
                pc = size_t(s64(a) + s16_at(a));
                continue;
            }
            if (op == 0x0A) {
                if (a + 1 > b.size()) return use;
                const size_t skip = a + 1 + 12 * size_t(b[a]);
                if (skip + 2 > b.size()) return use;
                work.push_back(size_t(s64(skip) + s16_at(skip)));
                pc = skip + 2;
                continue;
            }
            const int n = size(op);
            if (n < 0 || a + size_t(n) > b.size()) {
                use.other = true;
                return use;
            }
            if (op == 0x4C) {
                if (use.weight_cell < 0) use.weight_cell = int(rd16(b, a));
                else if (use.weight_cell != int(rd16(b, a))) use.other = true;
            }
            if ((op >= 0x44 && op <= 0x4B && op != 0x47) || op == 0x54 || op == 0x55) use.other = true;
            if (op == 0x2B) use.texture_sets.insert(b[a]);
            if (op == 0x2C) use.texture_sets.insert(rd16(b, a));
            pc = a + size_t(n);
        }
    }
    return use;
}

float as_f(u32 v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

} // namespace

AnimatedModels::AnimatedModels(ByteSpan set, float color_scale, const gt3r::gtm1::Options& decode) {
    if (!mdls::is_mdls(set)) return;
    const mdls::Header h = mdls::read_header(set);
    script_ = mdls::Script::parse(set);
    if (script_.empty()) return;
    state_ = mdls::ScriptState(script_);
    time_cell_ = script_.host_cell("time"), step_cell_ = script_.host_cell("deltaT");

    // The models to animate: a draw script, and a program that reads a key shape or a weight
    // from the registers and nothing else of them.
    std::set<u32> used_sets;
    int most_cases = 1;
    bool any_weight = false;
    for (u32 i = 0; i < h.models && i < script_.models.size(); ++i) {
        if (script_.models[i].draw < 0 && script_.models[i].tick < 0) continue;
        const u32 program = rd32(set, h.model_table + 0x28 * size_t(i) + 0x18);
        if (!program || program >= h.size) continue;
        const ProgramUse use = scan_program(set.first(h.size), program);
        if (use.other || (use.switch_cell < 0 && use.weight_cell < 0)) continue;
        Animated a;
        a.model = i, a.switch_cell = use.switch_cell, a.weight_cell = use.weight_cell;
        a.keys.resize(size_t(use.switch_cell >= 0 ? std::max(use.cases, 1) : 1));
        most_cases = std::max(most_cases, int(a.keys.size()));
        any_weight |= use.weight_cell >= 0;
        used_sets.insert(use.texture_sets.begin(), use.texture_sets.end());
        models_.push_back(std::move(a));
    }
    if (models_.empty()) return;

    // A copy of the set that lists only the texture sets those models use: decoding it reads
    // those pictures alone.
    Bytes trimmed(set.begin(), set.begin() + h.size);
    for (u32 l = 0; l < h.texture_lists; ++l) {
        const u32 list = rd32(set, h.texture_table + 4 * size_t(l));
        for (u32 t = 0; t < h.texture_sets && list && list + 4 * size_t(t) + 4 <= h.size; ++t)
            if (!used_sets.count(t)) std::memset(trimmed.data() + list + 4 * size_t(t), 0, 4);
    }
    mdls::Scene sc;
    sc.color_scale = color_scale;
    for (const Animated& a : models_) sc.only_models.push_back(a.model);
    gt3r::gtm1::Options o = decode;
    o.textures = true;
    size_t meshes = 0;
    try {
        for (int k = 0; k < most_cases; ++k) {
            // Every model's switch at case k.
            sc.registers.assign(script_.register_cells, 0);
            for (const Animated& a : models_)
                if (a.switch_cell >= 0 && size_t(a.switch_cell) < sc.registers.size()) sc.registers[size_t(a.switch_cell)] = u32(k);
            for (int end = 0; end < (any_weight ? 2 : 1); ++end) {
                sc.morph_weight = float(end);
                const Bytes block = mdls::to_gtm1(trimmed, nullptr, sc);
                const gt3r::gtm1::Gtm1 g = gt3r::gtm1::Gtm1::parse(block);
                for (Animated& a : models_) {
                    if (size_t(k) >= a.keys.size()) continue;
                    o.node = int(a.model);
                    scene::Model m = gt3r::gtm1::build_model(g, o, "animated");
                    if (textures_.textures.empty()) textures_.textures = m.textures;
                    scene::Mesh mesh;
                    if (!m.meshes.empty()) mesh = std::move(m.meshes[0]);
                    mesh.node = int(a.model);
                    mesh.name = "node" + std::to_string(a.model);
                    ++meshes;
                    (end == 0 ? a.keys[size_t(k)].first : a.keys[size_t(k)].second) = std::move(mesh);
                }
            }
            if (!any_weight)
                for (Animated& a : models_)
                    if (size_t(k) < a.keys.size()) a.keys[size_t(k)].second = a.keys[size_t(k)].first;
        }
    } catch (const std::exception& e) {
        LOG_WARN("Animated models: %s; they stay still", e.what());
        models_.clear();
        return;
    }
    // A key whose two ends do not line up (another count of vertices) is shown at its first end.
    for (Animated& a : models_)
        for (Key& key : a.keys)
            if (key.first.vertices.size() != key.second.vertices.size() || key.first.indices.size() != key.second.indices.size()) key.second = key.first;
    restart();
    LOG_INFO("Animated models: %zu of the set's, %d key shapes at most, %zu poses decoded", models_.size(), most_cases, meshes);
}

std::vector<u32> AnimatedModels::models() const {
    std::vector<u32> out;
    for (const Animated& a : models_) out.push_back(a.model);
    return out;
}

// The questions of a course's people (docs/formats/course.md): 0 the distance to the nearest
// car, 1 the time until one is level with the place, 2 the nearest car's bearing from the
// person's facing.
float AnimatedModels::ask(u32 id, std::span<const u32> cells, std::span<const CarState> cars) const {
    if (cells.size() < 3) return 0;
    const float x = as_f(cells[0]), y = as_f(cells[1]), z = as_f(cells[2]);
    float nearest = 1e9f, soonest = 1e9f, bearing = 0;
    for (const CarState& c : cars) {
        const float dx = x - c.position.x, dy = y - c.position.y, dz = z - c.position.z;
        const float d2 = dx * dx + dy * dy + dz * dz, d = std::sqrt(d2);
        if (d < nearest) {
            nearest = d;
            if (cells.size() >= 6) {
                const float fc = as_f(cells[3]), fs = as_f(cells[5]);
                bearing = std::atan2(-dx * fs + dz * fc, -dx * fc - dz * fs);
            }
        }
        // d squared over the speed toward the place: negative when the car is leaving.
        const float closing = c.velocity.x * dx + c.velocity.y * dy + c.velocity.z * dz;
        if (std::fabs(closing) > 1e-3f) {
            const float t = d2 / closing;
            if (std::fabs(t) < std::fabs(soonest)) soonest = t;
        }
    }
    return id == 0 ? nearest : id == 1 ? soonest : id == 2 ? bearing : 0.0f;
}

void AnimatedModels::restart() {
    if (script_.empty()) return;
    state_ = mdls::ScriptState(script_);
    time_ = 0;
    const mdls::ScriptCallback none = [](u32, std::span<const u32>) { return 1e9f; };
    for (const mdls::Script::Entries& e : script_.models)
        if (e.setup >= 0) mdls::run_script(script_, e.setup, state_, none);
}

void AnimatedModels::tick(float seconds, std::span<const CarState> cars) {
    if (script_.empty()) return;
    // The time in whole milliseconds, moved on by each step's own.
    time_ += std::trunc(double(seconds) * 1000.0);
    state_.set_host(time_cell_, u32(s32(time_)));
    state_.set_host_f(step_cell_, seconds);
    const mdls::ScriptCallback cb = [&](u32 id, std::span<const u32> cells) { return ask(id, cells, cars); };
    for (const mdls::Script::Entries& e : script_.models)
        if (e.tick >= 0) mdls::run_script(script_, e.tick, state_, cb);
}

std::vector<scene::Mesh> AnimatedModels::meshes(std::span<const CarState> cars) {
    std::vector<scene::Mesh> out;
    const mdls::ScriptCallback cb = [&](u32 id, std::span<const u32> cells) { return ask(id, cells, cars); };
    out.reserve(models_.size());
    for (const Animated& a : models_) {
        if (script_.models[a.model].draw >= 0) mdls::run_script(script_, script_.models[a.model].draw, state_, cb);
        // A switch value without a case draws nothing.
        const int k = a.switch_cell >= 0 ? state_.register_i(u32(a.switch_cell)) : 0;
        if (k < 0 || size_t(k) >= a.keys.size()) continue;
        const Key& key = a.keys[size_t(k)];
        if (key.first.vertices.empty()) continue;
        const float w = a.weight_cell >= 0 ? std::clamp(state_.register_f(u32(a.weight_cell)), 0.0f, 1.0f) : 0.0f;
        scene::Mesh mesh = key.first;
        if (w > 0)
            for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                scene::Vertex& v = mesh.vertices[i];
                const scene::Vertex& to = key.second.vertices[i];
                for (int j = 0; j < 3; ++j) v.pos[j] += (to.pos[j] - v.pos[j]) * w, v.normal[j] += (to.normal[j] - v.normal[j]) * w;
            }
        out.push_back(std::move(mesh));
    }
    return out;
}

} // namespace gt4r::race
