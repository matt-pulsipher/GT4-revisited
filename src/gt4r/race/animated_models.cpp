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
    std::vector<AnimatedModels::Move> moves; // matrix commands by register, before any shape is drawn
    bool drawn = false;                     // a shape was drawn on some path
    std::array<float, 16> pivot{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // the matrix at the first move
};

using Mat = std::array<float, 16>;
constexpr Mat kId{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r[size_t(i * 4 + j)] += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
    return r;
}
// The row-vector turn by an angle about an axis (as the shared engine's GTM1 reader turns).
Mat rotation(float ax, float ay, float az, float radians) {
    const float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(len > 0)) return kId;
    ax /= len, ay /= len, az /= len;
    const float c = std::cos(radians), s = std::sin(radians), t = 1 - c;
    return Mat{t * ax * ax + c, t * ax * ay + s * az, t * ax * az - s * ay, 0, t * ax * ay - s * az, t * ay * ay + c, t * ay * az + s * ax, 0,
               t * ax * az + s * ay, t * ay * az - s * ax, t * az * az + c, 0, 0, 0, 0, 1};
}
// The inverse of a matrix of turns, scales and a translation (row vectors).
Mat inverse_affine(const Mat& m) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (std::fabs(det) < 1e-12f) return kId;
    Mat r = kId;
    r[0] = (e * i - f * h) / det, r[1] = (c * h - b * i) / det, r[2] = (b * f - c * e) / det;
    r[4] = (f * g - d * i) / det, r[5] = (a * i - c * g) / det, r[6] = (c * d - a * f) / det;
    r[8] = (d * h - e * g) / det, r[9] = (b * g - a * h) / det, r[10] = (a * e - b * d) / det;
    for (int j = 0; j < 3; ++j) r[size_t(12 + j)] = -(m[12] * r[size_t(j)] + m[13] * r[size_t(4 + j)] + m[14] * r[size_t(8 + j)]);
    return r;
}

// Walks a draw program from its start over every path. (The commands' sizes are the converter's:
// docs/formats/model.md "Draw programs".)
ProgramUse scan_program(ByteSpan b, size_t start) {
    ProgramUse use;
    std::set<size_t> seen;
    std::vector<size_t> work{start};
    std::vector<Mat> stack{kId}; // the literal matrix commands' stack, on the first path walked
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
            if (op >= 0x02 && op <= 0x05) use.drawn = true;
            if (use.moves.empty() && !use.drawn) { // the matrix the model's moves are made in
                auto f = [&](size_t o) {
                    float v;
                    std::memcpy(&v, b.data() + o, 4);
                    return v;
                };
                Mat& top = stack.back();
                if (op == 0x0C) stack.push_back(top);
                else if (op == 0x0D && stack.size() > 1) stack.pop_back();
                else if (op == 0x0F || op == 0x10) {
                    Mat m;
                    for (int k = 0; k < 16; ++k) m[size_t(k)] = f(a + 4 * size_t(k));
                    top = op == 0x0F ? m : mul(m, top);
                } else if (op == 0x11) {
                    for (int j = 0; j < 4; ++j) top[size_t(12 + j)] += f(a) * top[size_t(j)] + f(a + 4) * top[size_t(4 + j)] + f(a + 8) * top[size_t(8 + j)];
                } else if (op == 0x12) {
                    for (int r = 0; r < 3; ++r)
                        for (int j = 0; j < 4; ++j) top[size_t(r * 4 + j)] *= f(a + 4 * size_t(r));
                } else if (op == 0x13) {
                    top = mul(rotation(f(a), f(a + 4), f(a + 8), f(a + 12)), top);
                } else if (op >= 0x14 && op <= 0x16) {
                    top = mul(rotation(op == 0x14, op == 0x15, op == 0x16, f(a) * 3.14159265f / 180.0f), top);
                }
            }
            if (op >= 0x44 && op <= 0x4B && op != 0x47) {
                // A move of the whole model (before its shapes, once on every path) is taken; any other is not.
                AnimatedModels::Move m{op, int(rd16(b, a))};
                bool known = false;
                for (const AnimatedModels::Move& have : use.moves) known |= have.op == m.op && have.cell == m.cell;
                if (use.drawn) use.other = true;
                else if (!known) {
                    if (use.moves.empty()) use.pivot = stack.back();
                    use.moves.push_back(m);
                }
            }
            if (op == 0x54 || op == 0x55) use.other = true;
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
        if (use.other || (use.switch_cell < 0 && use.weight_cell < 0 && use.moves.empty())) continue;
        Animated a;
        a.model = i, a.switch_cell = use.switch_cell, a.weight_cell = use.weight_cell, a.moves = use.moves;
        a.pivot = use.pivot, a.pivot_inverse = inverse_affine(use.pivot);
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
                    scene::Mesh mesh;
                    if (!m.meshes.empty()) mesh = std::move(m.meshes[0]);
                    // A model drawn with ALPHA 0x48 (Cs x As + Cd) adds to what is behind it: the
                    // spectators' camera flashes (a star on black). The shared engine's decode adds
                    // so only for cars; here the blended passes of such a model are made additive.
                    bool adds = false;
                    for (const gt3r::gtm1::Draw& d : g.run(a.model)) adds |= d.alpha_mode == 0x48;
                    if (adds)
                        for (scene::Primitive& prim : mesh.primitives)
                            if (prim.flags & scene::kAlphaBlend) prim.flags = (prim.flags & ~u32(scene::kAlphaBlend)) | scene::kAdditive | scene::kNoDepthWrite;
                    // Its pictures into the one list every pose draws with (a decode lists the
                    // pictures in its own order).
                    for (scene::Primitive& prim : mesh.primitives) {
                        if (prim.texture < 0 || size_t(prim.texture) >= m.textures.size()) continue;
                        const scene::Texture& t = m.textures[size_t(prim.texture)];
                        int found = -1;
                        for (size_t k = 0; k < textures_.textures.size() && found < 0; ++k) {
                            const scene::Texture& have = textures_.textures[k];
                            if (have.width == t.width && have.height == t.height && have.clamp_u == t.clamp_u && have.clamp_v == t.clamp_v && have.rgba == t.rgba) found = int(k);
                        }
                        if (found < 0) found = int(textures_.textures.size()), textures_.textures.push_back(t);
                        prim.texture = found;
                    }
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

// The registers' matrix commands in program order, as the draw program applies them to its matrix
// (each multiplies it from the left, as 0x10 .. 0x16 do): 0x44 a 4 x 4 matrix (16 cells), 0x45
// translate (3), 0x46 scale (3), 0x48 a turn by an angle in degrees about an axis (4 cells: the angle,
// then the axis: builtin_ra, rx, ry, rz), 0x49 / 0x4A / 0x4B a turn about x / y / z (degrees).
std::array<float, 16> AnimatedModels::move_matrix(std::span<const Move> moves) const {
    auto f = [&](int cell) { return cell >= 0 && size_t(cell) < state_.registers.size() ? state_.register_f(u32(cell)) : 0.0f; };
    constexpr float kRad = 3.14159265f / 180.0f;
    Mat out = kId;
    for (const Move& mv : moves) {
        Mat r = kId;
        switch (mv.op) {
        case 0x44:
            for (int k = 0; k < 16; ++k) r[size_t(k)] = f(mv.cell + k);
            break;
        case 0x45: r[12] = f(mv.cell), r[13] = f(mv.cell + 1), r[14] = f(mv.cell + 2); break;
        case 0x46: r[0] = f(mv.cell), r[5] = f(mv.cell + 1), r[10] = f(mv.cell + 2); break;
        case 0x48: r = rotation(f(mv.cell + 1), f(mv.cell + 2), f(mv.cell + 3), f(mv.cell) * kRad); break;
        case 0x49: r = rotation(1, 0, 0, f(mv.cell) * kRad); break;
        case 0x4A: r = rotation(0, 1, 0, f(mv.cell) * kRad); break;
        case 0x4B: r = rotation(0, 0, 1, f(mv.cell) * kRad); break;
        default: break;
        }
        out = mul(r, out);
    }
    return out;
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
        if (!a.moves.empty()) {
            // The model moved and turned as its program asks by the registers (row vectors: v' = v M).
            // The shapes as decoded stand where the program's matrix put them without the moves: taken back
            // into the frame the moves turn in (the matrix at the first move), moved, and put back.
            const std::array<float, 16> m = mul(mul(a.pivot_inverse, move_matrix(a.moves)), a.pivot);
            for (scene::Vertex& v : mesh.vertices) {
                const float p[3] = {v.pos[0], v.pos[1], v.pos[2]}, n[3] = {v.normal[0], v.normal[1], v.normal[2]};
                for (int j = 0; j < 3; ++j) {
                    v.pos[j] = p[0] * m[size_t(j)] + p[1] * m[size_t(4 + j)] + p[2] * m[size_t(8 + j)] + m[size_t(12 + j)];
                    v.normal[j] = n[0] * m[size_t(j)] + n[1] * m[size_t(4 + j)] + n[2] * m[size_t(8 + j)];
                }
            }
        }
        out.push_back(std::move(mesh));
    }
    return out;
}

} // namespace gt4r::race
