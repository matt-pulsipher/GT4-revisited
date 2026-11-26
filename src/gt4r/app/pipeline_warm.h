#pragma once

// The 3D pipelines GT4's draws use that the engine's warm-up (GpuContext::warm_pipelines, made
// for GT3's) leaves out: the coplanar overlays' depth-biased variants (scene::kCoplanarOverlay,
// PipelineKey::depth_bias), which every GT4 course and car draws. Built on a thread of their own
// at start, beside the engine's, so a race does not build them on its drawing thread (35-113 ms
// a pipeline on a Pixel Fold, Mali-G710: a stutter each in a race's first seconds).

#include "render/gpu_context.h"

#include <atomic>
#include <thread>
#include <vector>

namespace gt4r {

// The keys, the most used first (measured: first-use builds over nine courses on the Mac).
std::vector<gt3r::render::PipelineKey> gt4_warm_keys();

class PipelineWarmer {
public:
    ~PipelineWarmer() { stop(); }
    // Builds gt4_warm_keys() into `gpu` on a thread (again after stop(): a new sample count).
    void start(gt3r::render::GpuContext& gpu);
    // Waits for the thread (before the device goes or its pipelines are released).
    void stop();

private:
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

} // namespace gt4r
