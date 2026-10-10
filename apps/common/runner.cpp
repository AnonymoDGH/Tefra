#include "runner.h"
#include "scenes.h"

#include "tefra/core/fs.h"
#include "tefra/core/json.h"
#include "tefra/core/log.h"
#include "tefra/core/stats.h"
#include "tefra/core/sysinfo.h"
#include "tefra/core/time.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/frame.h"
#include "tefra/rhi/gpu_timer.h"
#include "tefra/rhi/readback.h"
#include "tefra/rhi/resources.h"

#if defined(TEFRA_WITH_WINDOW)
#include "tefra/platform/window.h"
#endif

#include <cstdio>
#include <memory>

namespace tefra::app {
namespace {

constexpr u32 kFramesInFlight = 2;
constexpr VkFormat kHeadlessFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkImageUsageFlags kHeadlessUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

#if defined(TEFRA_WITH_WINDOW)
void update_fly_camera(platform::Window& w, render::Camera& cam, f32 dt) {
    f64 dx = 0, dy = 0;
    w.cursor_delta(dx, dy);
    if (w.mouse_button_down(1)) { // right button: mouse look
        cam.yaw -= static_cast<f32>(dx) * 0.0025f;
        cam.pitch = clamp(cam.pitch - static_cast<f32>(dy) * 0.0025f, -1.55f, 1.55f);
    }
    vec3 move{};
    const vec3 fwd = cam.forward();
    const vec3 flat_fwd = normalize(vec3{fwd.x, 0.0f, fwd.z});
    const vec3 right = cam.right();
    if (w.key_down(platform::Key::W)) move += flat_fwd;
    if (w.key_down(platform::Key::S)) move -= flat_fwd;
    if (w.key_down(platform::Key::D)) move += right;
    if (w.key_down(platform::Key::A)) move -= right;
    if (w.key_down(platform::Key::E)) move += vec3{0.0f, 1.0f, 0.0f};
    if (w.key_down(platform::Key::Q)) move -= vec3{0.0f, 1.0f, 0.0f};
    const f32 speed = w.key_down(platform::Key::LeftShift) ? 25.0f : 4.0f; // m/s (walking ~1.4, running ~4)
    const vec3 delta = normalize(move) * (speed * dt);
    cam.position =
        cam.position + dvec3{static_cast<f64>(delta.x), static_cast<f64>(delta.y), static_cast<f64>(delta.z)};
    if (cam.position.y < 0.1) cam.position.y = 0.1;
}
#endif

std::string next_capture_path() {
    make_dirs("captures");
    for (u32 i = 0; i < 10000; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "captures/tefra_%04u.png", i);
        if (!file_exists(buf)) return buf;
    }
    return "captures/tefra_overflow.png";
}

} // namespace

namespace {

void run_impl(const RunConfig& cfg, RunResult& result) {
    if (!rhi::loader_init()) {
        result.error = "Vulkan loader not available";
        return;
    }

#if defined(TEFRA_WITH_WINDOW)
    std::unique_ptr<platform::Window> window;
    if (cfg.window) {
        platform::WindowDesc wd;
        wd.width = cfg.width;
        wd.height = cfg.height;
        wd.title = "Tefra";
        window.reset(platform::Window::create(wd));
        if (!window) {
            result.error = "cannot create window (no display?)";
            return;
        }
    }
#else
    if (cfg.window) {
        result.error = "built without window support (TEFRA_WITH_WINDOW=OFF)";
        return;
    }
#endif

    rhi::DeviceDesc dd;
    dd.app_name = "tefra";
    dd.validation = cfg.validation;
    dd.strict_baseline = cfg.strict_baseline;
    dd.gpu_index = cfg.gpu;
#if defined(TEFRA_WITH_WINDOW)
    if (window) dd.instance_extensions = platform::Window::required_instance_extensions();
#endif
    auto device = rhi::Device::create(dd);
    if (!device) {
        result.error = "cannot create Vulkan device";
        return;
    }
    result.adapter = device->adapter();
    result.validation_enabled = device->validation_enabled();

    std::unique_ptr<rhi::Swapchain> swapchain;
#if defined(TEFRA_WITH_WINDOW)
    if (window) {
        VkSurfaceKHR surface = window->create_surface(device->instance());
        if (!surface || !device->supports_present(surface)) {
            if (surface) vkDestroySurfaceKHR(device->instance(), surface, nullptr);
            result.error = "selected GPU cannot present to the window";
            return;
        }
        u32 w = 0, h = 0;
        window->framebuffer_size(w, h);
        swapchain = std::make_unique<rhi::Swapchain>(*device, surface, w, h, cfg.present);
        result.present_used = rhi::present_mode_name(swapchain->present_mode());
    }
#endif

    rhi::FrameLoop frames(*device, kFramesInFlight);
    rhi::GpuTimer timer(*device, kFramesInFlight);

    rhi::Image offscreen;
    VkFormat target_format = kHeadlessFormat;
    VkImageUsageFlags target_usage = kHeadlessUsage;
    if (swapchain) {
        target_format = swapchain->format();
        target_usage = swapchain->usage();
    } else {
        rhi::ImageDesc id;
        id.format = kHeadlessFormat;
        id.width = cfg.width;
        id.height = cfg.height;
        id.usage = kHeadlessUsage;
        id.name = "offscreen_output";
        offscreen = rhi::create_image(*device, id);
    }
    auto testpattern = std::make_unique<render::TestPattern>(*device, target_format, target_usage);

    render::TestPatternParams params = cfg.params;
    const bool scene3d = cfg.scene != "testpattern";
    std::unique_ptr<rhi::BindlessHeap> heap;
    std::unique_ptr<render::Scene> scene;
    std::unique_ptr<render::Environment> env;
    std::unique_ptr<render::Atmosphere> atmosphere;
    std::unique_ptr<render::Terrain> terrain;
    std::unique_ptr<render::Vegetation> vegetation;
    std::unique_ptr<render::Renderer> renderer;
    SceneSetup setup;
    if (scene3d) {
        heap = std::make_unique<rhi::BindlessHeap>(*device, kFramesInFlight);
        scene = std::make_unique<render::Scene>(*device, *heap);
        env = std::make_unique<render::Environment>(*device, *heap);
        atmosphere = std::make_unique<render::Atmosphere>(*device, *heap);
        bool built = false;
        if (cfg.scene == "materials") {
            build_materials_scene(*scene, *env, setup);
            built = true;
        } else if (cfg.scene == "materials_sky") {
            build_materials_sky_scene(*scene, *env, *atmosphere, setup);
            built = true;
        } else if (cfg.scene == "valley") {
            render::TerrainDesc td;
            td.texture_dir = file_exists("assets/cache/textures") ? "assets/cache/textures" : "";
            terrain = std::make_unique<render::Terrain>(*device, *heap, td);
            vegetation = std::make_unique<render::Vegetation>(*device, *scene);
            build_valley_scene(*scene, *env, *atmosphere, *terrain, *vegetation, setup);
            built = true;
        } else if (cfg.scene == "gltf") {
            built = build_gltf_scene(*scene, *env, setup, cfg.gltf_path, cfg.hdr_path);
        }
        if (!built) {
            result.error = "unknown scene or failed to load: " + cfg.scene;
            renderer.reset();
            vegetation.reset();
            env.reset();
            atmosphere.reset();
            terrain.reset();
            scene.reset();
            heap.reset();
            rhi::destroy_image(*device, offscreen);
            return;
        }
        if (cfg.ev100 >= 0.0f) setup.settings.ev100 = cfg.ev100;
        renderer = std::make_unique<render::Renderer>(*device, *heap, kFramesInFlight);
        params.camera = setup.camera;
    }
    const u32 total_frames = cfg.frames;
    u64 prev_start = 0;
    u64 frame_index = 0;
    bool capture_requested_by_key = false;

    for (;;) {
        const bool last_frame = total_frames > 0 && frame_index + 1 >= total_frames;
#if defined(TEFRA_WITH_WINDOW)
        if (window) {
            window->poll_events();
            if (window->should_close() || window->key_pressed(platform::Key::Escape)) break;
            if (window->key_pressed(platform::Key::F12)) capture_requested_by_key = true;
        }
#endif
        const u64 t_start = time_now_ns();
        const f64 frame_ms = prev_start ? ns_to_ms(t_start - prev_start) : 0.0;
        prev_start = t_start;

#if defined(TEFRA_WITH_WINDOW)
        if (window && cfg.fly_camera) update_fly_camera(*window, params.camera, static_cast<f32>(frame_ms * 1e-3));
#endif
        if (cfg.animate) {
            params.time = static_cast<f32>(frame_index) / 60.0f;
            params.camera.yaw = (scene3d ? setup.camera.yaw : cfg.params.camera.yaw) + params.time * 0.2f;
        }

        // Swapchain acquire (with recreate on resize/out-of-date).
        u32 image_index = 0;
        VkExtent2D extent{cfg.width, cfg.height};
        rhi::FrameContext& fc = frames.begin();
        const u64 wait_ns = frames.last_wait_ns();
#if defined(TEFRA_WITH_WINDOW)
        if (swapchain) {
            bool acquired = swapchain->acquire(fc.slot, image_index);
            while (!acquired) {
                u32 w = 0, h = 0;
                window->framebuffer_size(w, h);
                while ((w == 0 || h == 0) && !window->should_close()) { // minimized
                    window->poll_events();
                    sleep_ms(16);
                    window->framebuffer_size(w, h);
                }
                if (window->should_close()) break;
                swapchain->recreate(w, h);
                acquired = swapchain->acquire(fc.slot, image_index);
            }
            if (!acquired) {
                // Window closed while minimized: submit an empty frame to keep the loop consistent.
                frames.end();
                break;
            }
            extent = swapchain->extent();
        }
#endif

        VkCommandBuffer cmd = fc.cmd;
        // Results now available belong to frame (frame_index - frames_in_flight).
        timer.begin_frame(cmd, fc.slot);
        if (timer.has_results() && frame_index >= kFramesInFlight + cfg.warmup) {
            result.stats.gpu_ms.push_back(timer.frame_ms());
            for (const auto& r : timer.results()) result.stats.pass_ms[r.name].push_back(r.ms);
        }

        const VkImage target_image = swapchain ? swapchain->image(image_index) : offscreen.image;
        const VkImageView target_view = swapchain ? swapchain->view(image_index) : offscreen.view;

        const VkPipelineStageFlags2 prev_stage =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
        const VkAccessFlags2 prev_access = swapchain ? VK_ACCESS_2_NONE : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        if (scene3d) {
            render::OutputTarget out;
            out.image = target_image;
            out.view = target_view;
            out.format = target_format;
            out.extent = extent;
            out.usage = target_usage;
            out.final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            out.initial_stage = prev_stage;
            out.initial_access = prev_access;
            render::RenderWorld world;
            world.scene = scene.get();
            world.env = env.get();
            world.atmosphere = setup.use_atmosphere ? atmosphere.get() : nullptr;
            world.terrain = setup.use_terrain ? terrain.get() : nullptr;
            world.vegetation = vegetation.get();
            renderer->render(cmd, fc.slot, frame_index, &timer, world, params.camera, params.time, setup.settings, out);
            result.draw_calls = renderer->draw_calls();
        } else {
            rhi::ImageBarrier to_color;
            to_color.image = target_image;
            to_color.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            to_color.new_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            to_color.src_stage = prev_stage;
            to_color.src_access = prev_access;
            to_color.dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            to_color.dst_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            rhi::cmd_image_barrier(cmd, to_color);
            testpattern->record(cmd, &timer, target_view, extent, params);
            result.draw_calls = testpattern->draw_calls_last_frame();
        }

        const bool can_capture = (target_usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        const bool capture_now = can_capture && ((last_frame && (!cfg.capture_path.empty() || cfg.capture_to_memory)) ||
                                                 capture_requested_by_key);
        rhi::ReadbackRequest readback;
        VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkPipelineStageFlags2 last_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkAccessFlags2 last_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        if (capture_now) {
            readback = rhi::cmd_readback_image(*device, cmd, target_image, target_format, extent, layout, last_stage,
                                               last_access);
            layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            last_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
            last_access = VK_ACCESS_2_NONE;
        }

        if (swapchain) {
            rhi::ImageBarrier to_present;
            to_present.image = target_image;
            to_present.old_layout = layout;
            to_present.new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_present.src_stage = last_stage;
            to_present.src_access = last_access;
            to_present.dst_stage = VK_PIPELINE_STAGE_2_NONE;
            to_present.dst_access = VK_ACCESS_2_NONE;
            rhi::cmd_image_barrier(cmd, to_present);
            frames.end(swapchain->acquire_semaphore(fc.slot), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       swapchain->present_semaphore(image_index));
#if defined(TEFRA_WITH_WINDOW)
            if (!swapchain->present(image_index)) {
                u32 w = 0, h = 0;
                window->framebuffer_size(w, h);
                swapchain->recreate(w, h);
            }
#endif
        } else {
            frames.end();
        }

        const u64 t_end = time_now_ns();
        if (frame_index >= cfg.warmup) {
            // frame_ms measured at this frame's start is the previous frame's duration.
            if (frame_index > cfg.warmup && frame_ms > 0.0) result.stats.frame_ms.push_back(frame_ms);
            result.stats.cpu_ms.push_back(ns_to_ms(t_end - t_start - wait_ns));
        }

        if (capture_now) {
            frames.wait_all();
            ImageRGBA8 img;
            if (rhi::finish_readback(*device, readback, img)) {
                const std::string path = capture_requested_by_key ? next_capture_path() : cfg.capture_path;
                if (!path.empty()) {
                    if (image_write_png(path.c_str(), img))
                        TEFRA_LOG_INFO("Captured %s", path.c_str());
                    else
                        TEFRA_LOG_ERROR("Cannot write %s", path.c_str());
                }
                if (cfg.capture_to_memory && last_frame) result.capture = std::move(img);
            }
            capture_requested_by_key = false;
        }

#if defined(TEFRA_WITH_WINDOW)
        if (window && frame_index % 30 == 0 && !result.stats.frame_ms.empty()) {
            char title[160];
            const f64 gpu = timer.has_results() ? timer.frame_ms() : 0.0;
            std::snprintf(title, sizeof(title), "Tefra | %s | %.2f ms (%.0f fps) | GPU %.2f ms",
                          result.adapter.name.c_str(), frame_ms, frame_ms > 0.0 ? 1000.0 / frame_ms : 0.0, gpu);
            window->set_title(title);
        }
#endif
        ++frame_index;
        if (last_frame) break;
    }

    frames.wait_all();
    result.frames_rendered = static_cast<u32>(frame_index);
    device->memory_usage_mb(result.gpu_mem_used_mb, result.gpu_mem_budget_mb);
    result.cpu_rss_mb = process_rss_mb();

    renderer.reset();
    vegetation.reset();
    env.reset();
    atmosphere.reset();
    terrain.reset();
    scene.reset();
    heap.reset();
    testpattern.reset();
    rhi::destroy_image(*device, offscreen);
    swapchain.reset();
    result.ok = true;
}

} // namespace

RunResult run(const RunConfig& cfg) {
    RunResult result;
    const u32 errors_before = rhi::validation_error_count();
    const u32 warnings_before = rhi::validation_warning_count();
    run_impl(cfg, result);
    // Counted after the device is destroyed, so leaked Vulkan objects show up as errors too.
    result.validation_errors = rhi::validation_error_count() - errors_before;
    result.validation_warnings = rhi::validation_warning_count() - warnings_before;
    return result;
}

bool write_bench_json(const char* path, const RunConfig& cfg, const RunResult& r) {
    const SystemInfo sys = system_info();
    JsonWriter j;
    auto summary = [&j](const char* key, const std::vector<f64>& v) {
        const Summary s = summarize(v);
        j.begin_object(key)
            .kv("mean", s.mean)
            .kv("p50", s.p50)
            .kv("p95", s.p95)
            .kv("p99", s.p99)
            .kv("max", s.max)
            .kv("samples", static_cast<u64>(s.count))
            .end_object();
    };

    j.begin_object();
    j.kv("schema", "tefra-bench/1");
    j.begin_object("engine")
        .kv("version", TEFRA_VERSION)
        .kv("git", TEFRA_GIT_SHA)
        .kv("build", TEFRA_BUILD_TYPE)
#if defined(__clang__)
        .kv("compiler", "Clang " __clang_version__)
#elif defined(__GNUC__)
        .kv("compiler", "GCC " __VERSION__)
#else
        .kv("compiler", "unknown")
#endif
        .end_object();
    j.begin_object("system")
        .kv("os", sys.os)
        .kv("cpu", sys.cpu)
        .kv("threads", sys.threads)
        .kv("ram_mb", sys.ram_mb)
        .end_object();
    j.begin_object("device")
        .kv("index", r.adapter.index)
        .kv("name", r.adapter.name)
        .kv("vendor_id", r.adapter.vendor_id)
        .kv("device_id", r.adapter.device_id)
        .kv("type", r.adapter.type)
        .kv("api", r.adapter.api_version)
        .kv("driver", r.adapter.driver_name + " " + r.adapter.driver_info)
        .kv("software", r.adapter.software)
        .end_object();
    j.begin_object("config")
        .kv("scene", cfg.scene)
        .kv("tier", cfg.tier)
        .kv("width", cfg.width)
        .kv("height", cfg.height)
        .kv("frames", r.frames_rendered)
        .kv("warmup", cfg.warmup)
        .kv("present", r.present_used)
        .kv("strict_baseline", cfg.strict_baseline)
        .end_object();
    summary("frame_ms", r.stats.frame_ms);
    summary("cpu_ms", r.stats.cpu_ms);
    j.begin_object("gpu_ms");
    summary("total", r.stats.gpu_ms);
    j.begin_object("passes");
    for (const auto& [name, samples] : r.stats.pass_ms) summary(name.c_str(), samples);
    j.end_object();
    j.end_object();
    j.begin_object("memory")
        .kv("cpu_rss_mb", r.cpu_rss_mb)
        .kv("gpu_used_mb", r.gpu_mem_used_mb)
        .kv("gpu_budget_mb", r.gpu_mem_budget_mb)
        .end_object();
    j.kv("draw_calls", r.draw_calls);
    j.begin_object("validation")
        .kv("enabled", r.validation_enabled)
        .kv("errors", r.validation_errors)
        .kv("warnings", r.validation_warnings)
        .end_object();
    j.end_object();

    if (!path || !*path || std::string(path) == "-") {
        std::fputs(j.str().c_str(), stdout);
        return true;
    }
    return file_write_all(path, j.str().data(), j.str().size());
}

} // namespace tefra::app
