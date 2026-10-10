#include "tefra/rendergraph/graph.h"

#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/gpu_timer.h"
#include "tefra/rhi/pipeline.h"
#include "tefra/rhi/resources.h"

#include "tefra/core/log.h"

#include <cstring>

namespace tefra::rg {

namespace {

constexpr u64 kEvictAfterFrames = 8;

struct Physical {
    VkFormat format;
    VkExtent2D extent;
    VkImageUsageFlags usage;
    rhi::Image image;
    u32 bindless = ~0u;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    u64 last_frame = 0;
    bool used = false;
};

struct Virtual {
    const char* name;
    bool imported;
    VkFormat format;
    VkExtent2D extent;
    VkImageUsageFlags usage;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    u32 bindless = ~0u;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    Physical* phys = nullptr;
    bool touched = false; // accessed by an executed pass this frame
};

struct AccessRec {
    u32 tex;
    Access access;
    Load load; // for attachments: whether previous contents matter
};

struct Attachment {
    u32 tex;
    Load load;
    VkClearValue clear;
};

struct Pass {
    const char* name;
    PassType type;
    std::vector<AccessRec> accesses;
    std::vector<Attachment> colors;
    bool has_depth = false;
    bool depth_write = true;
    Attachment depth{};
    void (*fn)(void*, const PassContext&) = nullptr;
    void* data = nullptr;
    void (*dtor)(void*) = nullptr;
};

struct RenderPassEntry {
    std::vector<u64> key;
    rhi::RenderPassObjects rp;
    u64 last_frame = 0;
};

bool has_write(VkAccessFlags2 a) {
    const VkAccessFlags2 writes = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
                                  VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    return (a & writes) != 0;
}

struct Requirement {
    VkImageLayout layout;
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageUsageFlags usage;
};

Requirement requirement(Access a, PassType type, bool depth_format, Load load) {
    const VkPipelineStageFlags2 shader_stage =
        type == PassType::Compute ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                                  : (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    const VkPipelineStageFlags2 depth_stages =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    switch (a) {
    case Access::ColorWrite:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
                    (load == Load::Load ? VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT : VK_ACCESS_2_NONE),
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    case Access::DepthWrite:
        return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depth_stages,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
    case Access::DepthRead:
        return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, depth_stages,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
    case Access::Sampled:
        return {depth_format ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                shader_stage, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_USAGE_SAMPLED_BIT};
    case Access::TransferSrc:
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    case Access::TransferDst:
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    }
    return {};
}

bool is_write_access(Access a) {
    return a == Access::ColorWrite || a == Access::DepthWrite || a == Access::TransferDst;
}

} // namespace

struct GraphImpl {
    const rhi::Device& dev;
    rhi::BindlessHeap& heap;
    rhi::GpuTimer* timer = nullptr;
    u32 slot = 0;
    u64 frame = 0;
    std::vector<Physical*> cache;
    std::vector<Virtual> textures;
    std::vector<Pass> passes;
    std::vector<RenderPassEntry> rp_cache;
    std::vector<VkImageMemoryBarrier2> barriers;
    u32 stat_barriers = 0;
    u32 stat_passes = 0;
    u64 stat_bytes = 0;

    GraphImpl(const rhi::Device& d, rhi::BindlessHeap& h) : dev(d), heap(h) {}

    Physical* acquire_physical(const Virtual& v) {
        for (Physical* p : cache) {
            if (!p->used && p->format == v.format && p->extent.width == v.extent.width &&
                p->extent.height == v.extent.height && p->usage == v.usage) {
                p->used = true;
                return p;
            }
        }
        Physical* p = new Physical();
        p->format = v.format;
        p->extent = v.extent;
        p->usage = v.usage;
        rhi::ImageDesc d;
        d.format = v.format;
        d.width = v.extent.width;
        d.height = v.extent.height;
        d.usage = v.usage;
        d.name = v.name;
        p->image = rhi::create_image(dev, d);
        if (v.usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
            const VkImageLayout l = rhi::is_depth_format(v.format) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            p->bindless = heap.add_texture(p->image.view, l);
        }
        p->used = true;
        cache.push_back(p);
        return p;
    }

    void destroy_physical(Physical* p) {
        if (p->bindless != ~0u) heap.remove_texture(p->bindless);
        rhi::destroy_image(dev, p->image);
        delete p;
    }

    const rhi::RenderPassObjects& render_pass_for(const Pass& pass, VkExtent2D extent) {
        std::vector<u64> key;
        key.push_back((u64(extent.width) << 32) | extent.height);
        auto push_att = [&](const Attachment& a, VkImageLayout layout) {
            const Virtual& v = textures[a.tex];
            key.push_back((u64(v.format) << 32) | (u64(v.usage) << 8) | u64(a.load));
            key.push_back(u64(layout));
        };
        for (const auto& c : pass.colors) push_att(c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (pass.has_depth)
            push_att(pass.depth, pass.depth_write ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                  : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        key.push_back(pass.has_depth ? 1 : 0);
        for (auto& e : rp_cache) {
            if (e.key == key) {
                e.last_frame = frame;
                return e.rp;
            }
        }
        auto load_op = [](Load l) {
            switch (l) {
            case Load::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case Load::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case Load::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        };
        std::vector<rhi::AttachmentDesc> colors;
        for (const auto& c : pass.colors) {
            const Virtual& v = textures[c.tex];
            rhi::AttachmentDesc d;
            d.format = v.format;
            d.usage = v.usage;
            d.load = load_op(c.load);
            d.store = VK_ATTACHMENT_STORE_OP_STORE;
            d.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colors.push_back(d);
        }
        rhi::AttachmentDesc depth;
        if (pass.has_depth) {
            const Virtual& v = textures[pass.depth.tex];
            depth.format = v.format;
            depth.usage = v.usage;
            depth.load = load_op(pass.depth.load);
            depth.store = VK_ATTACHMENT_STORE_OP_STORE;
            depth.layout = pass.depth_write ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                            : VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        rhi::RenderPassDesc rpd;
        rpd.colors = colors;
        rpd.depth = pass.has_depth ? &depth : nullptr;
        RenderPassEntry e;
        e.key = std::move(key);
        e.rp = rhi::create_render_pass(dev, rpd, extent, pass.name);
        e.last_frame = frame;
        rp_cache.push_back(std::move(e));
        return rp_cache.back().rp;
    }

    void transition(u32 tex, const Requirement& req, bool discard) {
        Virtual& v = textures[tex];
        const bool layout_change = v.layout != req.layout;
        const bool needs = layout_change || has_write(v.access) || has_write(req.access) || discard;
        if (!needs) {
            // Read after read in the same layout: widen the scope so a later write waits for all readers.
            v.stage |= req.stage;
            v.access |= req.access;
            return;
        }
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = v.stage ? v.stage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.srcAccessMask = v.access;
        b.dstStageMask = req.stage;
        b.dstAccessMask = req.access;
        b.oldLayout = discard ? VK_IMAGE_LAYOUT_UNDEFINED : v.layout;
        b.newLayout = req.layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = v.image;
        b.subresourceRange = {rhi::aspect_of(v.format), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        barriers.push_back(b);
        v.layout = req.layout;
        v.stage = req.stage;
        v.access = req.access;
    }

    void flush_barriers(VkCommandBuffer cmd) {
        if (barriers.empty()) return;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = static_cast<u32>(barriers.size());
        dep.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2KHR(cmd, &dep);
        stat_barriers += static_cast<u32>(barriers.size());
        barriers.clear();
    }
};

// ---- PassContext ------------------------------------------------------------------------------------

VkImage PassContext::image(TextureHandle h) const { return impl->textures[h.index].image; }
VkImageView PassContext::view(TextureHandle h) const { return impl->textures[h.index].view; }
u32 PassContext::sampled(TextureHandle h) const {
    const u32 i = impl->textures[h.index].bindless;
    TEFRA_ASSERT(i != ~0u);
    return i;
}

// ---- PassBuilder ------------------------------------------------------------------------------------

TextureHandle PassBuilder::create(const TextureDesc& desc) {
    GraphImpl& g = *g_.impl_;
    Virtual v{};
    v.name = desc.name;
    v.imported = false;
    v.format = desc.format;
    v.extent = {desc.width, desc.height};
    v.usage = 0;
    g.textures.push_back(v);
    return {static_cast<u32>(g.textures.size() - 1)};
}

void PassBuilder::read(TextureHandle h, Access access) {
    TEFRA_VERIFY(h.valid());
    g_.impl_->passes[pass_].accesses.push_back({h.index, access, Load::Load});
}

void PassBuilder::write(TextureHandle h, Access access) {
    TEFRA_VERIFY(h.valid());
    g_.impl_->passes[pass_].accesses.push_back({h.index, access, Load::DontCare});
}

void PassBuilder::color(TextureHandle h, Load load, VkClearColorValue clear) {
    TEFRA_VERIFY(h.valid());
    Pass& p = g_.impl_->passes[pass_];
    VkClearValue cv{};
    cv.color = clear;
    p.colors.push_back({h.index, load, cv});
    p.accesses.push_back({h.index, Access::ColorWrite, load});
}

void PassBuilder::depth(TextureHandle h, Load load, f32 clear_depth, bool write) {
    TEFRA_VERIFY(h.valid());
    Pass& p = g_.impl_->passes[pass_];
    VkClearValue cv{};
    cv.depthStencil = {clear_depth, 0};
    p.has_depth = true;
    p.depth_write = write;
    p.depth = {h.index, load, cv};
    p.accesses.push_back({h.index, write ? Access::DepthWrite : Access::DepthRead, load});
}

// ---- Graph ------------------------------------------------------------------------------------------

Graph::Graph(const rhi::Device& dev, rhi::BindlessHeap& heap) : impl_(new GraphImpl(dev, heap)), arena_(256 * 1024) {}

Graph::~Graph() {
    for (auto& p : impl_->passes)
        if (p.dtor) p.dtor(p.data);
    for (Physical* p : impl_->cache) impl_->destroy_physical(p);
    for (auto& e : impl_->rp_cache) rhi::destroy_render_pass(impl_->dev, e.rp);
    delete impl_;
}

void Graph::begin(u32 slot, rhi::GpuTimer* timer) {
    GraphImpl& g = *impl_;
    for (auto& p : g.passes)
        if (p.dtor) p.dtor(p.data);
    g.passes.clear();
    g.textures.clear();
    arena_.reset();
    g.slot = slot;
    g.timer = timer;
    ++g.frame;
    for (Physical* p : g.cache) p->used = false;
}

TextureHandle Graph::import(const ImportDesc& d) {
    Virtual v{};
    v.name = d.name;
    v.imported = true;
    v.format = d.format;
    v.extent = d.extent;
    v.usage = d.usage;
    v.image = d.image;
    v.view = d.view;
    v.bindless = d.bindless;
    v.layout = d.initial_layout;
    v.stage = d.initial_stage;
    v.access = d.initial_access;
    v.final_layout = d.final_layout;
    impl_->textures.push_back(v);
    return {static_cast<u32>(impl_->textures.size() - 1)};
}

u32 Graph::add_pass_record(const char* name, PassType type) {
    Pass p;
    p.name = name;
    p.type = type;
    impl_->passes.push_back(std::move(p));
    return static_cast<u32>(impl_->passes.size() - 1);
}

void Graph::set_pass_exec(u32 pass, ExecFn fn, void* data, DtorFn dtor) {
    Pass& p = impl_->passes[pass];
    p.fn = fn;
    p.data = data;
    p.dtor = dtor;
}

void Graph::execute(VkCommandBuffer cmd) {
    GraphImpl& g = *impl_;
    g.stat_barriers = 0;
    g.stat_passes = 0;
    g.stat_bytes = 0;

    // 1. Accumulate usage of transient textures from every declared access.
    for (const Pass& p : g.passes) {
        for (const AccessRec& a : p.accesses) {
            Virtual& v = g.textures[a.tex];
            if (!v.imported)
                v.usage |= requirement(a.access, p.type, rhi::is_depth_format(v.format), a.load).usage;
        }
    }
    // 2. Bind transient textures to cached physical images.
    for (Virtual& v : g.textures) {
        if (v.imported || v.usage == 0) continue;
        Physical* p = g.acquire_physical(v);
        p->last_frame = g.frame;
        v.phys = p;
        v.image = p->image.image;
        v.view = p->image.view;
        v.bindless = p->bindless;
        v.layout = p->layout;
        v.stage = p->stage;
        v.access = p->access;
        const u32 bpp = rhi::is_depth_format(v.format) ? 4u : 8u; // estimate for statistics
        g.stat_bytes += u64(v.extent.width) * v.extent.height * bpp;
    }
    g.heap.flush(g.slot);

    // 3. Record passes.
    PassContext ctx;
    ctx.cmd = cmd;
    ctx.slot = g.slot;
    ctx.impl = &g;
    for (Pass& p : g.passes) {
        for (const AccessRec& a : p.accesses) {
            Virtual& v = g.textures[a.tex];
            const Requirement req = requirement(a.access, p.type, rhi::is_depth_format(v.format), a.load);
            // First touch of a transient that is fully overwritten: previous contents can be discarded.
            const bool discard = !v.touched && !v.imported && is_write_access(a.access) && a.load != Load::Load;
            g.transition(a.tex, req, discard);
            v.touched = true;
        }
        const u32 scope = g.timer ? g.timer->begin(cmd, p.name) : ~0u;
        if (!g.timer) rhi::cmd_begin_label(cmd, p.name);
        g.flush_barriers(cmd);

        if (p.type == PassType::Raster) {
            VkExtent2D extent{};
            if (!p.colors.empty()) extent = g.textures[p.colors[0].tex].extent;
            else if (p.has_depth) extent = g.textures[p.depth.tex].extent;
            TEFRA_VERIFY(extent.width > 0);
            const rhi::RenderPassObjects& rp = g.render_pass_for(p, extent);
            VkImageView views[9];
            VkClearValue clears[9];
            u32 n = 0;
            for (const auto& c : p.colors) {
                views[n] = g.textures[c.tex].view;
                clears[n++] = c.clear;
            }
            if (p.has_depth) {
                views[n] = g.textures[p.depth.tex].view;
                clears[n++] = p.depth.clear;
            }
            rhi::cmd_begin_render_pass(cmd, rp, {views, n}, {clears, n});
            ctx.extent = extent;
            if (p.fn) p.fn(p.data, ctx);
            rhi::cmd_end_render_pass(cmd);
        } else {
            ctx.extent = {};
            if (p.fn) p.fn(p.data, ctx);
        }
        if (g.timer) g.timer->end(cmd, scope);
        else rhi::cmd_end_label(cmd);
        ++g.stat_passes;
    }

    // 4. Final layouts for imported textures.
    for (u32 i = 0; i < g.textures.size(); ++i) {
        Virtual& v = g.textures[i];
        if (!v.imported || v.final_layout == VK_IMAGE_LAYOUT_UNDEFINED || v.final_layout == v.layout) continue;
        g.transition(i, {v.final_layout, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, 0}, false);
    }
    g.flush_barriers(cmd);

    // 5. Persist physical state; evict textures and render passes unused for a while.
    for (Virtual& v : g.textures) {
        if (!v.phys) continue;
        v.phys->layout = v.layout;
        v.phys->stage = v.stage;
        v.phys->access = v.access;
    }
    usize keep = 0;
    for (Physical* p : g.cache) {
        if (g.frame - p->last_frame > kEvictAfterFrames) g.destroy_physical(p);
        else g.cache[keep++] = p;
    }
    g.cache.resize(keep);
    usize keep_rp = 0;
    for (auto& e : g.rp_cache) {
        if (g.frame - e.last_frame > kEvictAfterFrames) rhi::destroy_render_pass(g.dev, e.rp);
        else g.rp_cache[keep_rp++] = std::move(e);
    }
    g.rp_cache.resize(keep_rp);
}

FinalState Graph::final_state(TextureHandle h) const {
    const Virtual& v = impl_->textures[h.index];
    return {v.layout, v.stage, v.access};
}

u32 Graph::barrier_count() const { return impl_->stat_barriers; }
u32 Graph::pass_count() const { return impl_->stat_passes; }
u64 Graph::transient_bytes() const { return impl_->stat_bytes; }

} // namespace tefra::rg
