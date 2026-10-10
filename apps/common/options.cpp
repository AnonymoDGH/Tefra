#include "options.h"

#include "tefra/core/log.h"

#include <cstdio>

namespace tefra::app {

bool parse_common_options(const CommandLine& cl, RunConfig& cfg) {
    cfg.width = static_cast<u32>(cl.get_int("width", cfg.width));
    cfg.height = static_cast<u32>(cl.get_int("height", cfg.height));
    cfg.frames = static_cast<u32>(cl.get_int("frames", cfg.frames));
    cfg.warmup = static_cast<u32>(cl.get_int("warmup", cfg.warmup));
    cfg.gpu = static_cast<i32>(cl.get_int("gpu", cfg.gpu));
#if defined(NDEBUG)
    cfg.validation = cl.has("validation");
#else
    cfg.validation = !cl.has("no-validation");
#endif
    cfg.strict_baseline = cl.has("strict");
    if (const char* pm = cl.get("present")) {
        if (!rhi::parse_present_mode(pm, cfg.present)) {
            TEFRA_LOG_ERROR("invalid --present '%s' (fifo|mailbox|immediate)", pm);
            return false;
        }
    }
    if (const char* c = cl.get("capture")) cfg.capture_path = c;
    if (const char* t = cl.get("tier")) cfg.tier = t;
    if (const char* cam = cl.get("camera")) {
        double v[5] = {};
        if (std::sscanf(cam, "%lf,%lf,%lf,%lf,%lf", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5) {
            TEFRA_LOG_ERROR("invalid --camera '%s' (x,y,z,yaw_deg,pitch_deg)", cam);
            return false;
        }
        cfg.camera_override = true;
        for (int i = 0; i < 3; ++i) cfg.camera_xyz[i] = v[i];
        cfg.camera_yaw_pitch[0] = static_cast<f32>(v[3]) * 3.14159265f / 180.0f;
        cfg.camera_yaw_pitch[1] = static_cast<f32>(v[4]) * 3.14159265f / 180.0f;
    }
    if (const char* sc = cl.get("scene")) cfg.scene = sc;
    if (const char* g = cl.get("gltf")) {
        cfg.gltf_path = g;
        if (!cl.get("scene")) cfg.scene = "gltf";
    }
    if (const char* h = cl.get("hdr")) cfg.hdr_path = h;
    cfg.ev100 = static_cast<f32>(cl.get_float("ev", static_cast<f64>(cfg.ev100)));
    cfg.animate = cl.has("animate");
    cfg.no_taa = cl.has("no-taa");
    cfg.terrain_detail = !cl.has("no-terrain-detail");
    if (cl.get("pitch")) {
        cfg.pitch_deg = static_cast<f32>(cl.get_float("pitch", 0.0));
        cfg.has_pitch = true;
    }
    cfg.no_volumetric = cl.has("no-volumetric");
    if (cl.has("yaw")) {
        cfg.has_camera_yaw = true;
        cfg.camera_yaw_deg = static_cast<f32>(cl.get_float("yaw", 0.0));
    }
    cfg.no_bloom = cl.has("no-bloom");
    cfg.auto_exposure = cl.has("auto-exposure");
    cfg.no_film = cl.has("no-film");
    if (cfg.width == 0 || cfg.height == 0 || cfg.width > 8192 || cfg.height > 8192) {
        TEFRA_LOG_ERROR("invalid resolution %ux%u", cfg.width, cfg.height);
        return false;
    }
    return true;
}

void print_common_usage() {
    std::printf("  --width N --height N    resolution (default 1280x720)\n"
                "  --frames N --warmup N   frames to render / to discard from statistics\n"
                "  --gpu N                 adapter index (see tefra_info)\n"
                "  --present MODE          fifo | mailbox | immediate (window mode)\n"
                "  --capture FILE.png      save the last frame\n"
                "  --validation            enable Vulkan validation (default on in Debug; --no-validation)\n"
                "  --strict                only baseline features of the reference hardware\n"
                "  --animate               move the camera/time between frames\n"
                "  --scene NAME            testpattern | materials | gltf\n"
                "  --gltf FILE --hdr FILE  glTF model and Radiance HDR environment\n"
                "  --ev EV100              camera exposure\n"
                "  --tier T                native | low | medium | high (low = 0.75 render scale + TAAU)\n"
                "  --no-taa                disable temporal anti-aliasing\n"
                "  --no-terrain-detail     terrain: disable POM, stochastic tiling and macro variation\n"
                "  --pitch DEG             camera pitch (degrees, negative looks down) for scenes\n"
                "  --yaw DEG               camera yaw (degrees) for scenes\n"
                "  --no-volumetric         disable volumetric fog (keeps the analytic height fog)\n"
                "  --camera x,y,z,yaw,pitch  override the scene camera (degrees for yaw/pitch)\n"
                "  --no-bloom              disable bloom\n"
                "  --auto-exposure         enable auto exposure (EV compensation clamped to +-2 EV)\n"
                "  --no-film               disable vignette, chromatic aberration and grain\n");
}

} // namespace tefra::app
