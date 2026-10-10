#include "tefra/studio/app.h"

#include "tefra/core/fs.h"
#include "tefra/core/image.h"
#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/platform/window.h"
#include "tefra/renderer/atmosphere.h"
#include "tefra/renderer/environment.h"
#include "tefra/renderer/renderer.h"
#include "tefra/renderer/scene.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/frame.h"
#include "tefra/rhi/readback.h"
#include "tefra/rhi/swapchain.h"
#include "tefra/world/scene_sync.h"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cmath>

namespace tefra::studio {

namespace {
constexpr u32 kFramesInFlight = 2;
constexpr VkFormat kViewportFormat = VK_FORMAT_R8G8B8A8_SRGB;

// Sun direction from the hour (equinox), latitude and an azimuth offset. World: +X east, +Y up, -Z north.
vec3 sun_direction(f32 clock, f32 latitude_deg, f32 azimuth_deg) {
    const f32 h = (clock - 12.0f) / 24.0f * kTwoPi;
    const f32 phi = radians(latitude_deg);
    const f32 east = -std::sin(h);
    const f32 up = std::cos(phi) * std::cos(h);
    const f32 north = -std::sin(phi) * std::cos(h);
    vec3 d{east, up, -north};
    const f32 a = radians(azimuth_deg);
    d = {d.x * std::cos(a) - d.z * std::sin(a), d.y, d.x * std::sin(a) + d.z * std::cos(a)};
    return normalize(d);
}
} // namespace

// The log sink writes into the Output panel's ring buffer (one Studio instance per process).
static std::mutex* g_log_mutex = nullptr;
static std::deque<LogLine>* g_log = nullptr;

static void studio_log_sink(void*, LogLevel level, f64 seconds, const char* text) {
    if (!g_log_mutex) return;
    std::lock_guard<std::mutex> lock(*g_log_mutex);
    g_log->push_back({level, seconds, text});
    if (g_log->size() > 4000) g_log->pop_front();
}

App::App(const StudioConfig& cfg) : cfg_(cfg) {
    g_log_mutex = &log_mutex_;
    g_log = &log_;
    log_set_sink(studio_log_sink, nullptr);
    camera_.position = {18.0, 12.0, 22.0};
    camera_.yaw = radians(38.0f);
    camera_.pitch = radians(-22.0f);
    camera_.fovy = radians(70.0f);
}

App::~App() {
    if (device_) device_->wait_idle();
    if (ImGui::GetCurrentContext()) {
        if (viewport_tex_) ImGui_ImplVulkan_RemoveTexture(viewport_tex_);
        viewport_tex_ = VK_NULL_HANDLE;
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    sync_.reset();
    renderer_.reset();
    if (device_) {
        destroy_viewport_target();
        destroy_ui_targets();
        if (ui_pass_) vkDestroyRenderPass(device_->device(), ui_pass_, nullptr);
    }
    env_.reset();
    atmosphere_.reset();
    scene_.reset();
    heap_.reset();
    frames_.reset();
    swapchain_.reset();
    device_.reset();
    window_.reset();
    log_set_sink(nullptr, nullptr);
    g_log_mutex = nullptr;
    g_log = nullptr;
}

bool App::init(std::string* error) {
    if (!rhi::loader_init()) {
        *error = "Vulkan loader not available";
        return false;
    }
    platform::WindowDesc wd;
    wd.width = cfg_.width;
    wd.height = cfg_.height;
    wd.title = "Tefra Studio";
    window_.reset(platform::Window::create(wd));
    if (!window_) {
        *error = "cannot create window (no display?)";
        return false;
    }
    rhi::DeviceDesc dd;
    dd.app_name = "tefra_studio";
    dd.validation = cfg_.validation;
    dd.instance_extensions = platform::Window::required_instance_extensions();
    device_ = rhi::Device::create(dd);
    if (!device_) {
        *error = "cannot create Vulkan device";
        return false;
    }
    VkSurfaceKHR surface = window_->create_surface(device_->instance());
    if (!surface || !device_->supports_present(surface)) {
        *error = "GPU cannot present to the window";
        return false;
    }
    u32 w = 0, h = 0;
    window_->framebuffer_size(w, h);
    swapchain_ = std::make_unique<rhi::Swapchain>(*device_, surface, w, h, rhi::PresentMode::Fifo);
    frames_ = std::make_unique<rhi::FrameLoop>(*device_, kFramesInFlight);
    heap_ = std::make_unique<rhi::BindlessHeap>(*device_, kFramesInFlight);
    scene_ = std::make_unique<render::Scene>(*device_, *heap_);
    env_ = std::make_unique<render::Environment>(*device_, *heap_);
    atmosphere_ = std::make_unique<render::Atmosphere>(*device_, *heap_);
    renderer_ = std::make_unique<render::Renderer>(*device_, *heap_, kFramesInFlight);
    if (!create_ui_pass()) {
        *error = "cannot create the UI render pass";
        return false;
    }
    create_ui_framebuffers();
    init_imgui();

    if (!cfg_.open_path.empty()) {
        std::string err;
        if (!session_.open(cfg_.open_path, &err)) TEFRA_LOG_ERROR("Open %s: %s", cfg_.open_path.c_str(), err.c_str());
    } else if (cfg_.demo) {
        session_.new_place(false);
        build_demo_scene();
    }
    // Render defaults for the editor: crisp, responsive, no film grain.
    settings_.cascades = 3;
    settings_.shadow_distance = 160.0f;
    settings_.film_effects = false;
    settings_.grass = false;
    settings_.cloud_coverage = 0.3f;
    TEFRA_LOG_INFO("Tefra Studio ready (%s)", device_->adapter().name.c_str());
    return true;
}

bool App::create_ui_pass() {
    VkAttachmentDescription att{};
    att.format = swapchain_->format();
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    return vkCreateRenderPass(device_->device(), &ci, nullptr, &ui_pass_) == VK_SUCCESS;
}

void App::create_ui_framebuffers() {
    ui_extent_ = swapchain_->extent();
    ui_framebuffers_.resize(swapchain_->image_count());
    for (u32 i = 0; i < swapchain_->image_count(); ++i) {
        VkImageView view = swapchain_->view(i);
        VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass = ui_pass_;
        ci.attachmentCount = 1;
        ci.pAttachments = &view;
        ci.width = ui_extent_.width;
        ci.height = ui_extent_.height;
        ci.layers = 1;
        vkCreateFramebuffer(device_->device(), &ci, nullptr, &ui_framebuffers_[i]);
    }
}

void App::destroy_ui_targets() {
    for (VkFramebuffer fb : ui_framebuffers_) vkDestroyFramebuffer(device_->device(), fb, nullptr);
    ui_framebuffers_.clear();
}

void App::ensure_viewport_target(u32 w, u32 h) {
    w = max(w, 16u);
    h = max(h, 16u);
    if (viewport_image_.image && viewport_extent_.width == w && viewport_extent_.height == h) return;
    frames_->wait_all();
    destroy_viewport_target();
    rhi::ImageDesc id;
    id.format = kViewportFormat;
    id.width = w;
    id.height = h;
    id.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    id.name = "studio_viewport";
    viewport_image_ = rhi::create_image(*device_, id);
    viewport_extent_ = {w, h};
    viewport_tex_ =
        ImGui_ImplVulkan_AddTexture(VK_NULL_HANDLE, viewport_image_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void App::destroy_viewport_target() {
    if (viewport_tex_ && ImGui::GetCurrentContext()) ImGui_ImplVulkan_RemoveTexture(viewport_tex_);
    viewport_tex_ = VK_NULL_HANDLE;
    if (viewport_image_.image) rhi::destroy_image(*device_, viewport_image_);
    viewport_image_ = {};
}

void App::init_imgui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "tefra_studio.ini";
    apply_theme();
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(window_->native_handle()), true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_2;
    ii.Instance = device_->instance();
    ii.PhysicalDevice = device_->physical();
    ii.Device = device_->device();
    ii.QueueFamily = device_->queue_family();
    ii.Queue = device_->queue();
    ii.DescriptorPoolSize = 64;
    ii.MinImageCount = 2;
    ii.ImageCount = max(2u, swapchain_->image_count());
    ii.PipelineInfoMain.RenderPass = ui_pass_;
    ii.PipelineInfoMain.Subpass = 0;
    ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&ii);
}

void App::apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 3.0f;
    s.FrameRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.WindowPadding = {8, 6};
    s.FramePadding = {6, 4};
    s.ItemSpacing = {8, 5};
    s.IndentSpacing = 14.0f;
    // Sober dark-grey palette with a blue accent (sRGB values; converted below for the sRGB swapchain).
    auto c = [](f32 r, f32 g, f32 b, f32 a = 1.0f) { return ImVec4(r, g, b, a); };
    ImVec4* col = s.Colors;
    col[ImGuiCol_Text] = c(0.88f, 0.89f, 0.90f);
    col[ImGuiCol_TextDisabled] = c(0.50f, 0.52f, 0.55f);
    col[ImGuiCol_WindowBg] = c(0.165f, 0.170f, 0.180f);
    col[ImGuiCol_ChildBg] = c(0.150f, 0.155f, 0.165f);
    col[ImGuiCol_PopupBg] = c(0.140f, 0.145f, 0.155f, 0.98f);
    col[ImGuiCol_Border] = c(0.08f, 0.08f, 0.09f);
    col[ImGuiCol_FrameBg] = c(0.120f, 0.125f, 0.135f);
    col[ImGuiCol_FrameBgHovered] = c(0.20f, 0.21f, 0.23f);
    col[ImGuiCol_FrameBgActive] = c(0.22f, 0.24f, 0.27f);
    col[ImGuiCol_TitleBg] = c(0.11f, 0.115f, 0.125f);
    col[ImGuiCol_TitleBgActive] = c(0.13f, 0.135f, 0.145f);
    col[ImGuiCol_MenuBarBg] = c(0.13f, 0.135f, 0.145f);
    col[ImGuiCol_Header] = c(0.21f, 0.37f, 0.62f, 0.55f);
    col[ImGuiCol_HeaderHovered] = c(0.25f, 0.43f, 0.72f, 0.65f);
    col[ImGuiCol_HeaderActive] = c(0.26f, 0.47f, 0.80f, 0.85f);
    col[ImGuiCol_Button] = c(0.22f, 0.23f, 0.25f);
    col[ImGuiCol_ButtonHovered] = c(0.28f, 0.30f, 0.33f);
    col[ImGuiCol_ButtonActive] = c(0.26f, 0.47f, 0.80f);
    col[ImGuiCol_Tab] = c(0.13f, 0.135f, 0.145f);
    col[ImGuiCol_TabHovered] = c(0.26f, 0.45f, 0.75f);
    col[ImGuiCol_TabSelected] = c(0.19f, 0.20f, 0.215f);
    col[ImGuiCol_TabDimmed] = c(0.12f, 0.125f, 0.135f);
    col[ImGuiCol_TabDimmedSelected] = c(0.165f, 0.170f, 0.180f);
    col[ImGuiCol_CheckMark] = c(0.38f, 0.62f, 0.98f);
    col[ImGuiCol_SliderGrab] = c(0.33f, 0.55f, 0.90f);
    col[ImGuiCol_SliderGrabActive] = c(0.40f, 0.63f, 1.0f);
    col[ImGuiCol_DockingPreview] = c(0.30f, 0.52f, 0.90f, 0.6f);
    col[ImGuiCol_Separator] = c(0.09f, 0.09f, 0.10f);
    // The swapchain is sRGB: ImGui blends in the attachment's space, so feed it linear colours.
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        ImVec4& v = col[i];
        v.x = std::pow(v.x, 2.2f);
        v.y = std::pow(v.y, 2.2f);
        v.z = std::pow(v.z, 2.2f);
    }
}

void App::update_lighting(bool force) {
    world::Instance* light = session_.model().service("Lighting");
    if (!light) return;
    const f32 clock = light->get_float("ClockTime", 14.0f);
    const f32 lat = light->get_float("GeographicLatitude", 20.0f);
    const f32 az = light->get_float("SunAzimuthOffset", 0.0f);
    settings_.cloud_coverage = light->get_float("CloudCover", 0.3f);
    settings_.fog_density = light->get_float("FogDensity", 0.0f);
    settings_.fog_falloff = light->get_float("FogHeightFalloff", 0.03f);
    settings_.grade_saturation = light->get_float("Saturation", 1.05f);
    settings_.grade_contrast = light->get_float("Contrast", 1.05f);
    const vec3 sun = sun_direction(clock, lat, az);
    // Exposure follows the sun height (bright noon, darker dusk), plus the user's compensation.
    const f32 day = saturate((sun.y + 0.05f) / 0.45f);
    settings_.ev100 = lerp(8.5f, 14.8f, day) - light->get_float("ExposureCompensation", 0.0f);
    if (!force && clock == applied_clock_ && lat == applied_lat_ && az == applied_az_) return;
    applied_clock_ = clock;
    applied_lat_ = lat;
    applied_az_ = az;
    settings_.sun_direction = sun.y > -0.02f ? sun : normalize(vec3{sun.x, -0.02f, sun.z});
    frames_->wait_all();
    env_->build_from_equirect(atmosphere_->render_equirect(settings_.sun_direction, 128));
}

void App::sync_world() {
    if (session_.generation() != synced_generation_) {
        if (!sync_)
            sync_ = std::make_unique<world::SceneSync>(*scene_, session_.model());
        else
            sync_->set_model(session_.model());
        synced_generation_ = session_.generation();
        open_scripts_.clear();
        update_lighting(true);
    }
    update_lighting(false);
    sync_->update();
}

void App::request_viewport_capture(std::string path, std::function<void(bool)> done) {
    pending_capture_path_ = std::move(path);
    pending_capture_done_ = std::move(done);
}

std::vector<LogLine> App::log_tail(usize max_lines) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::vector<LogLine> out;
    const usize n = min(max_lines, log_.size());
    for (usize i = log_.size() - n; i < log_.size(); ++i) out.push_back(log_[i]);
    return out;
}

void App::frame(f32 dt, bool capture_window) {
    // Resize the viewport target to last frame's panel size before any draw data references its texture.
    ensure_viewport_target(viewport_wanted_.width, viewport_wanted_.height);
    // ---- UI (decides the viewport size and edits the model) ----
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw_ui(dt);
    ImGui::Render();

    // ---- Simulation and world sync ----
    session_.step(dt);
    for (auto& cb : frame_callbacks_) cb(dt);
    sync_world();

    // ---- GPU frame ----
    rhi::FrameContext& fc = frames_->begin();
    u32 image_index = 0;
    bool acquired = swapchain_->acquire(fc.slot, image_index);
    while (!acquired) {
        u32 w = 0, h = 0;
        window_->framebuffer_size(w, h);
        while ((w == 0 || h == 0) && !window_->should_close()) {
            window_->poll_events();
            sleep_ms(16);
            window_->framebuffer_size(w, h);
        }
        if (window_->should_close()) {
            frames_->end();
            quit_ = true;
            return;
        }
        device_->wait_idle();
        swapchain_->recreate(w, h);
        destroy_ui_targets();
        create_ui_framebuffers();
        ImGui_ImplVulkan_SetMinImageCount(2);
        acquired = swapchain_->acquire(fc.slot, image_index);
    }
    VkCommandBuffer cmd = fc.cmd;

    // Scene -> viewport image.
    render::OutputTarget out;
    out.image = viewport_image_.image;
    out.view = viewport_image_.view;
    out.format = kViewportFormat;
    out.extent = viewport_extent_;
    out.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    out.final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    out.initial_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    out.initial_access = VK_ACCESS_2_NONE;
    render::RenderWorld rw;
    rw.scene = scene_.get();
    rw.env = env_.get();
    rw.atmosphere = atmosphere_.get();
    renderer_->render(cmd, fc.slot, frame_index_, nullptr, rw, camera_, f32(time_), settings_, out);

    VkImageLayout vp_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkPipelineStageFlags2 vp_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 vp_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    rhi::ReadbackRequest vp_readback;
    const bool vp_capture = !pending_capture_path_.empty();
    if (vp_capture) {
        vp_readback = rhi::cmd_readback_image(*device_, cmd, viewport_image_.image, kViewportFormat, viewport_extent_,
                                              vp_layout, vp_stage, vp_access);
        vp_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vp_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        vp_access = VK_ACCESS_2_NONE;
    }
    rhi::ImageBarrier to_read;
    to_read.image = viewport_image_.image;
    to_read.old_layout = vp_layout;
    to_read.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_read.src_stage = vp_stage;
    to_read.src_access = vp_access;
    to_read.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_read.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    rhi::cmd_image_barrier(cmd, to_read);

    // UI -> swapchain.
    VkClearValue clear{};
    clear.color = {{0.02f, 0.02f, 0.025f, 1.0f}};
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = ui_pass_;
    bi.framebuffer = ui_framebuffers_[image_index];
    bi.renderArea.extent = ui_extent_;
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    VkImageLayout sc_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkPipelineStageFlags2 sc_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 sc_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    rhi::ReadbackRequest sc_readback;
    if (capture_window) {
        sc_readback = rhi::cmd_readback_image(*device_, cmd, swapchain_->image(image_index), swapchain_->format(),
                                              ui_extent_, sc_layout, sc_stage, sc_access);
        sc_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        sc_stage = VK_PIPELINE_STAGE_2_COPY_BIT;
        sc_access = VK_ACCESS_2_NONE;
    }
    rhi::ImageBarrier to_present;
    to_present.image = swapchain_->image(image_index);
    to_present.old_layout = sc_layout;
    to_present.new_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.src_stage = sc_stage;
    to_present.src_access = sc_access;
    // The layout transition must complete before the semaphore signal (ALL_COMMANDS) that present waits on.
    to_present.dst_stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    rhi::cmd_image_barrier(cmd, to_present);
    frames_->end(swapchain_->acquire_semaphore(fc.slot), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 swapchain_->present_semaphore(image_index));
    if (!swapchain_->present(image_index)) {
        u32 w = 0, h = 0;
        window_->framebuffer_size(w, h);
        device_->wait_idle();
        swapchain_->recreate(w, h);
        destroy_ui_targets();
        create_ui_framebuffers();
    }

    if (vp_capture || capture_window) {
        frames_->wait_all();
        if (vp_capture) {
            ImageRGBA8 img;
            const bool ok =
                rhi::finish_readback(*device_, vp_readback, img) && image_write_png(pending_capture_path_.c_str(), img);
            if (pending_capture_done_) pending_capture_done_(ok);
            pending_capture_path_.clear();
            pending_capture_done_ = nullptr;
        }
        if (capture_window) {
            ImageRGBA8 img;
            if (rhi::finish_readback(*device_, sc_readback, img)) {
                for (usize i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;
                if (image_write_png(cfg_.capture_path.c_str(), img))
                    TEFRA_LOG_INFO("Captured %s", cfg_.capture_path.c_str());
            }
        }
    }
    ++frame_index_;
}

int App::run() {
    u64 prev = time_now_ns();
    u32 frames_done = 0;
    const bool scripted = !cfg_.capture_path.empty() && cfg_.frames > 0;
    while (!quit_) {
        window_->poll_events();
        if (window_->should_close()) break;
        const u64 now = time_now_ns();
        f32 dt = static_cast<f32>(ns_to_ms(now - prev) * 1e-3);
        prev = now;
        if (scripted) dt = 1.0f / 60.0f;
        dt = min(dt, 0.1f);
        time_ += dt;
        const bool last = scripted && frames_done + 1 >= cfg_.frames;
        frame(dt, last);
        ++frames_done;
        if (last) break;
    }
    frames_->wait_all();
    return 0;
}

} // namespace tefra::studio
