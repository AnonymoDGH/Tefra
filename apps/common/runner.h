#pragma once

// Shared frame loop for viewer, bench and GPU tests: one place that drives device, swapchain or headless
// target, timing, capture and validation counting.

#include "tefra/core/image.h"
#include "tefra/renderer/testpattern.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/swapchain.h"

#include <map>
#include <string>
#include <vector>

namespace tefra::app {

// Default view: 1.7 m eye height, 6 m behind the origin so both axis markers are visible.
inline render::TestPatternParams default_scene_params() {
    render::TestPatternParams p;
    p.camera.position = {0.0, 1.7, 6.0};
    return p;
}

struct RunConfig {
    bool window = false;
    u32 width = 1280;
    u32 height = 720;
    u32 frames = 0; // 0 = until the window closes (window mode only)
    u32 warmup = 0;
    i32 gpu = -1;
    bool validation = false;
    bool strict_baseline = false;
    rhi::PresentMode present = rhi::PresentMode::Fifo;
    std::string capture_path;          // write the last frame as PNG
    bool capture_to_memory = false;    // keep the last frame in RunResult::capture
    bool fly_camera = false;           // interactive camera (window mode)
    bool animate = false;              // advance time/camera between frames
    std::string scene = "testpattern"; // testpattern | materials | gltf
    std::string gltf_path;
    std::string hdr_path;
    f32 ev100 = -1.0f;           // < 0: scene default
    std::string tier = "native"; // native | low | medium | high
    bool no_taa = false;
    bool terrain_detail = true; // terrain POM / stochastic tiling / macro variation
    bool has_pitch = false;     // --pitch: overrides the scene camera pitch
    f32 pitch_deg = 0.0f;
    bool camera_override = false; // --camera x,y,z,yaw_deg,pitch_deg (scene3d)
    f64 camera_xyz[3] = {0.0, 0.0, 0.0};
    f32 camera_yaw_pitch[2] = {0.0f, 0.0f};
    bool no_volumetric = false;  // disable volumetric fog (analytic height fog only)
    bool has_camera_yaw = false; // --yaw DEG overrides the scene camera
    f32 camera_yaw_deg = 0.0f;
    bool no_bloom = false;
    bool auto_exposure = false; // on for this run (scene default is off)
    bool no_film = false;
    bool no_canopy = false; // disable the canopy occlusion map (--no-canopy)
    render::TestPatternParams params = default_scene_params();
};

struct RunStats {
    std::vector<f64> frame_ms; // wall time between frame starts
    std::vector<f64> cpu_ms;   // CPU time per frame excluding GPU waits
    std::vector<f64> gpu_ms;   // GPU timestamps, first to last scope
    std::map<std::string, std::vector<f64>> pass_ms;
};

struct RunResult {
    bool ok = false;
    std::string error;
    ImageRGBA8 capture;
    RunStats stats;
    u32 validation_errors = 0;
    u32 validation_warnings = 0;
    bool validation_enabled = false;
    rhi::AdapterInfo adapter;
    f64 gpu_mem_used_mb = 0, gpu_mem_budget_mb = 0, cpu_rss_mb = 0;
    u32 draw_calls = 0;
    std::string present_used = "headless";
    u32 frames_rendered = 0;
};

RunResult run(const RunConfig& cfg);

// Writes the tefra-bench/1 JSON report (spec §8.4).
bool write_bench_json(const char* path, const RunConfig& cfg, const RunResult& result);

} // namespace tefra::app
