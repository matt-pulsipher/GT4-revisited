#include "gt4r/app/pipeline_warm.h"

#include "base/log.h"

#include <SDL3/SDL_timer.h>

namespace gt4r {

using gt3r::render::AlphaWrite;
using gt3r::render::BlendMode;
using gt3r::render::CullMode;
using gt3r::render::PipelineKey;

std::vector<PipelineKey> gt4_warm_keys() {
    std::vector<PipelineKey> keys;
    auto add = [&keys](BlendMode b, CullMode c, bool depth_write, AlphaWrite a, bool clipped, bool plain) {
        PipelineKey k;
        k.blend = b, k.cull = c, k.depth_write = depth_write, k.alpha = a, k.clipped = clipped, k.plain = plain;
        k.depth_bias = true;
        keys.push_back(k);
    };
    // The overlays as nine courses drew them, the most used first: opaque and blended in the PLAIN
    // variant, culled either way or not at all, with and without depth and alpha writes, as they
    // are and clipped at the guard band (PipelineKey::clipped).
    for (bool clipped : {false, true})
        for (BlendMode b : {BlendMode::Opaque, BlendMode::Alpha})
            for (CullMode c : {CullMode::Back, CullMode::None, CullMode::Front})
                for (bool depth_write : {true, false})
                    for (AlphaWrite a : {AlphaWrite::Write, AlphaWrite::Keep})
                        if (b == BlendMode::Alpha || depth_write) add(b, c, depth_write, a, clipped, true);
    // The frame-alpha weighted overlays (GS ALPHA 0x58) in the general variant, and the additive
    // ones.
    for (bool clipped : {false, true})
        for (CullMode c : {CullMode::Back, CullMode::None}) {
            add(BlendMode::DestAlphaAdd, c, false, AlphaWrite::Keep, clipped, false);
            add(BlendMode::DestAlphaAdd, c, false, AlphaWrite::Keep, clipped, true);
            add(BlendMode::Additive, c, false, AlphaWrite::Write, clipped, true);
            add(BlendMode::Additive, c, false, AlphaWrite::Keep, clipped, true);
        }
    return keys;
}

void PipelineWarmer::start(gt3r::render::GpuContext& gpu) {
    stop();
    if (gpu.gles()) return; // (its programs are built on first use, quickly)
    stop_ = false;
    thread_ = std::thread([this, &gpu, keys = gt4_warm_keys()] {
        // (GpuContext::pipeline logs each as "built on first use": the engine builds its own list's
        // without a line, but has no way to take more keys.)
        LOG_INFO("GT4's 3D pipelines: building %zu (the next lines \"built on first use\" until \"GT4's 3D pipelines warmed\" are these)", keys.size());
        const Uint64 t0 = SDL_GetTicksNS();
        for (const PipelineKey& k : keys) {
            if (stop_) return;
            gpu.pipeline(k);
        }
        LOG_INFO("GT4's 3D pipelines warmed: %zu in %.0f ms", keys.size(), double(SDL_GetTicksNS() - t0) / 1e6);
    });
}

void PipelineWarmer::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

} // namespace gt4r
