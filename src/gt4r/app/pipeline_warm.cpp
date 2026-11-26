#include "gt4r/app/pipeline_warm.h"

#include "base/log.h"

#include <SDL3/SDL_timer.h>

namespace gt4r {

using gt3r::render::BlendMode;
using gt3r::render::CullMode;
using gt3r::render::PipelineKey;

std::vector<PipelineKey> gt4_warm_keys() {
    // The frame-alpha weighted overlays (GS ALPHA 0x58) in the general variant, clipped at the
    // guard band: the engine warms them only unclipped.
    std::vector<PipelineKey> keys;
    for (CullMode c : {CullMode::Back, CullMode::None, CullMode::Front}) {
        PipelineKey k;
        k.blend = BlendMode::DestAlphaAdd, k.cull = c, k.depth_write = false;
        k.depth_bias = k.clipped = true;
        keys.push_back(k);
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
