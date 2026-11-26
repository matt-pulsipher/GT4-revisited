#pragma once

// The 3D pipelines GT4's draws use that the engine's warm-up (GpuContext::warm_pipelines) leaves
// out, built on a thread of their own at start, beside the engine's, so a race does not build them
// on its drawing thread (35-113 ms a pipeline on a Pixel Fold, Mali-G710). Measured: first-use
// builds in races at Laguna Seca, Tsukuba and Infineon on the Mac.

#include "render/gpu_context.h"

#include <atomic>
#include <thread>
#include <vector>

namespace gt4r {

// The keys.
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
