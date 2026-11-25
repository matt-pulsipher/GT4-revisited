#include "gt4r/settings/video.h"

#include "base/config.h"
#include "platform/render_scale.h"
#include "render/gamma_pass.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gt4r::settings {

namespace {

constexpr const char* kSection = "video";

bool flag(const gt3r::IniDocument& c, const char* key, bool otherwise) {
    const std::optional<std::string> v = c.get(kSection, key);
    if (!v) return otherwise;
    const std::string t = gt3r::to_lower(gt3r::trim(*v));
    if (t == "on" || t == "1" || t == "true" || t == "yes") return true;
    if (t == "off" || t == "0" || t == "false" || t == "no") return false;
    return otherwise;
}

// The largest of `steps` not above v (the first when none is).
int step_of(int v, std::initializer_list<int> steps) {
    int out = *steps.begin();
    for (int s : steps)
        if (s <= v) out = s;
    return out;
}

std::string two_digits(float v) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2f", double(v));
    return b;
}

const char* gpu_value(Gpu g) {
    switch (g) {
    case Gpu::Gles: return "gles";
    case Gpu::GlesDriver: return "gles_driver";
    case Gpu::Off: return "off";
    default: return "auto";
    }
}

} // namespace

Video Video::load(const gt3r::IniDocument& c) {
    Video v;
    v.fullscreen = flag(c, "fullscreen", v.fullscreen);
    v.vsync = flag(c, "vsync", v.vsync);
    v.limit_60fps = flag(c, "limit_60fps", v.limit_60fps);
    v.show_fps = flag(c, "show_fps", v.show_fps);
    v.dynamic_resolution = flag(c, "dynamic_resolution", v.dynamic_resolution);
    v.texture_packs = flag(c, "texture_packs", v.texture_packs);
    v.sharp_menu_models = gt3r::to_lower(c.get(kSection, "menu_models").value_or("screen")) != "console";
    if (const auto t = c.get(kSection, "msaa")) v.msaa = step_of(std::atoi(t->c_str()), {1, 2, 4, 8});
    if (const auto t = c.get(kSection, "anisotropy")) v.anisotropy = step_of(std::atoi(t->c_str()), {1, 2, 4, 8, 16});
    v.render_scale = gt3r::platform::parse_render_scale(c.get(kSection, "render_scale").value_or("auto"));
    v.gamma = gt3r::render::parse_gamma(c.get(kSection, "gamma").value_or(""));
    v.aspect = gt3r::platform::parse_aspect(gt3r::to_lower(c.get(kSection, "aspect").value_or("")));
    const std::string gpu = gt3r::to_lower(c.get(kSection, "gpu").value_or("auto"));
    v.gpu = gpu == "gles" ? Gpu::Gles : gpu == "gles_driver" ? Gpu::GlesDriver : gpu == "off" || gpu == "0" ? Gpu::Off : Gpu::Automatic;
    return v;
}

void Video::save(gt3r::IniDocument& c) const {
    const Video was = load(c);
    const auto on_off = [&c](const char* key, bool now, bool before) {
        if (now != before) c.set(kSection, key, now ? "on" : "off");
    };
    on_off("fullscreen", fullscreen, was.fullscreen);
    on_off("vsync", vsync, was.vsync);
    on_off("limit_60fps", limit_60fps, was.limit_60fps);
    on_off("show_fps", show_fps, was.show_fps);
    on_off("dynamic_resolution", dynamic_resolution, was.dynamic_resolution);
    on_off("texture_packs", texture_packs, was.texture_packs);
    if (sharp_menu_models != was.sharp_menu_models) c.set(kSection, "menu_models", sharp_menu_models ? "screen" : "console");
    if (msaa != was.msaa) c.set(kSection, "msaa", std::to_string(msaa));
    if (anisotropy != was.anisotropy) c.set(kSection, "anisotropy", std::to_string(anisotropy));
    if (render_scale != was.render_scale) c.set(kSection, "render_scale", render_scale ? two_digits(*render_scale) : "auto");
    if (gamma != was.gamma) {
        if (gt3r::render::gamma_changes(gamma)) c.set(kSection, "gamma", two_digits(gamma));
        else c.remove(kSection, "gamma");
    }
    if (aspect != was.aspect) c.set(kSection, "aspect", gt3r::platform::aspect_value(aspect));
    if (gpu != was.gpu) c.set(kSection, "gpu", gpu_value(gpu));
}

double limited_rate(double hz, bool limit) {
    if (!limit || !(hz > 0)) return 0;
    const int every = std::max(1, int(std::floor(hz / 60.0 + 0.25)));
    return every > 1 ? hz / every : 0;
}

std::string percent(float v) { return std::to_string(int(std::lround(v * 100))) + "%"; }

std::vector<Resolution> resolutions(float auto_scale, float auto_top, float limit) {
    static constexpr float kSteps[] = {0.5f, 0.6f, 0.7f, 0.75f, 0.8f, 0.9f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f};
    std::vector<Resolution> out;
    const Resolution automatic{std::nullopt, auto_top > auto_scale + 0.001f ? "Automatic (up to " + percent(auto_top) + ")" : "Automatic (" + percent(auto_scale) + ")"};
    bool placed = false;
    for (float s : kSteps) {
        if (s > limit + 1e-3f) break;
        if (!placed && auto_scale <= s) out.push_back(automatic), placed = true;
        out.push_back({s, percent(s)});
    }
    if (!placed) out.push_back(automatic);
    return out;
}

size_t resolution_index(const std::vector<Resolution>& list, std::optional<float> scale) {
    size_t best = 0;
    float near = 1e9f;
    for (size_t i = 0; i < list.size(); ++i) {
        if (!scale && !list[i].scale) return i;
        if (!scale || !list[i].scale) continue;
        if (const float d = std::fabs(*list[i].scale - *scale); d < near) near = d, best = i;
    }
    return best;
}

} // namespace gt4r::settings
