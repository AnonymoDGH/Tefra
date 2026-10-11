#pragma once

// Frame graph (spec 1 §2). Per frame: declare passes and the textures they touch, then execute.
// The graph derives layouts, sync2 barriers, render passes and imageless framebuffers from declared usage.
// Transient textures are cached across frames by description, so steady-state frames allocate nothing.

#include "tefra/rhi/vk.h"

#include "tefra/core/memory.h"

#include <new>
#include <type_traits>
#include <vector>

namespace tefra::rhi {
class Device;
class BindlessHeap;
class GpuTimer;
} // namespace tefra::rhi

namespace tefra::rg {

struct TextureHandle {
    u32 index = ~0u;
    bool valid() const { return index != ~0u; }
};

struct TextureDesc {
    VkFormat format = VK_FORMAT_UNDEFINED;
    u32 width = 0, height = 0;
    const char* name = "texture";
};

enum class PassType : u8 { Raster, Compute, Transfer };

enum class Access : u8 {
    ColorWrite, // color attachment
    DepthWrite, // depth attachment, test + write
    DepthRead,  // depth attachment, test only (read-only layout, can be sampled at the same time)
    Sampled,    // sampled in shaders of the pass stage
    TransferSrc,
    TransferDst,
};

enum class Load : u8 { Clear, Load, DontCare };

struct PassContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkExtent2D extent{}; // render area for raster passes
    u32 slot = 0;        // frame slot (for per-frame resources)

    VkImage image(TextureHandle h) const;
    VkImageView view(TextureHandle h) const;
    // Bindless index of a texture declared as Sampled/DepthRead in this pass.
    u32 sampled(TextureHandle h) const;

    const struct GraphImpl* impl = nullptr;
};

class Graph;

class PassBuilder {
  public:
    TextureHandle create(const TextureDesc& desc);
    void read(TextureHandle h, Access access = Access::Sampled);
    void write(TextureHandle h, Access access);
    // Raster attachments (order of color() calls = attachment index / shader output location).
    void color(TextureHandle h, Load load = Load::DontCare, VkClearColorValue clear = {});
    void depth(TextureHandle h, Load load = Load::Clear, f32 clear_depth = 0.0f, bool write = true);

  private:
    friend class Graph;
    PassBuilder(Graph& g, u32 pass) : g_(g), pass_(pass) {}
    Graph& g_;
    u32 pass_;
};

struct ImportDesc {
    const char* name = "imported";
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    VkImageUsageFlags usage = 0;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 initial_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkAccessFlags2 initial_access = VK_ACCESS_2_NONE;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED; // UNDEFINED = leave as is
    u32 bindless = ~0u;                                     // persistent bindless index if sampled
};

struct FinalState {
    VkImageLayout layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

class Graph {
  public:
    Graph(const rhi::Device& dev, rhi::BindlessHeap& heap);
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    // Starts a new frame graph. `slot` is the frame-in-flight slot (its GPU work must be complete).
    void begin(u32 slot, rhi::GpuTimer* timer);

    TextureHandle import(const ImportDesc& desc);

    template <typename Setup, typename Exec>
    void add_pass(const char* name, PassType type, Setup&& setup, Exec&& exec) {
        const u32 index = add_pass_record(name, type);
        PassBuilder b(*this, index);
        setup(b);
        using E = std::decay_t<Exec>;
        void* mem = arena_.alloc(sizeof(E), alignof(E));
        TEFRA_VERIFY(mem != nullptr);
        E* stored = new (mem) E(static_cast<Exec&&>(exec));
        set_pass_exec(
            index, [](void* p, const PassContext& ctx) { (*static_cast<E*>(p))(ctx); }, stored,
            [](void* p) { static_cast<E*>(p)->~E(); });
    }

    // Records every pass with barriers. Imported textures end in their final_layout.
    void execute(VkCommandBuffer cmd);

    // State of an imported texture after execute() (to chain with work outside the graph).
    FinalState final_state(TextureHandle h) const;

    // Statistics of the last execute().
    u32 barrier_count() const;
    u32 pass_count() const;
    u64 transient_bytes() const;

  private:
    friend class PassBuilder;
    using ExecFn = void (*)(void*, const PassContext&);
    using DtorFn = void (*)(void*);
    u32 add_pass_record(const char* name, PassType type);
    void set_pass_exec(u32 pass, ExecFn fn, void* data, DtorFn dtor);

    GraphImpl* impl_;
    LinearArena arena_;
};

} // namespace tefra::rg
