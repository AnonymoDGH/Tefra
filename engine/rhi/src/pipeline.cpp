#include "tefra/rhi/pipeline.h"
#include "tefra/rhi/device.h"

#include "tefra/core/fs.h"
#include "tefra/core/log.h"

#include <string>
#include <vector>

namespace tefra::rhi {

VkShaderModule load_shader(const Device& dev, const char* name) {
    const std::string path = path_join(path_join(exe_dir(), "shaders"), std::string(name) + ".spv");
    std::vector<u8> code;
    if (!file_read_all(path.c_str(), code) || code.size() % 4 != 0 || code.empty())
        TEFRA_PANIC("cannot load shader '%s'", path.c_str());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const u32*>(static_cast<const void*>(code.data()));
    VkShaderModule mod = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateShaderModule(dev.device(), &ci, nullptr, &mod));
    dev.set_name(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<u64>(mod), name);
    return mod;
}

RenderPassObjects create_render_pass(const Device& dev, const RenderPassDesc& desc, VkExtent2D extent,
                                     const char* name) {
    RenderPassObjects rp;
    rp.extent = extent;
    rp.color_count = static_cast<u32>(desc.colors.size());
    rp.has_depth = desc.depth != nullptr;

    std::vector<VkAttachmentDescription2> atts;
    std::vector<VkAttachmentReference2> color_refs;
    std::vector<VkFramebufferAttachmentImageInfo> fb_infos;
    std::vector<VkFormat> formats(desc.colors.size() + 1);
    VkAttachmentReference2 depth_ref{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};

    auto add = [&](const AttachmentDesc& a, bool depth) {
        VkAttachmentDescription2 d{VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
        d.format = a.format;
        d.samples = VK_SAMPLE_COUNT_1_BIT;
        d.loadOp = a.load;
        d.storeOp = a.store;
        d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        d.initialLayout = a.layout;
        d.finalLayout = a.layout;
        const u32 index = static_cast<u32>(atts.size());
        atts.push_back(d);
        VkAttachmentReference2 ref{VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
        ref.attachment = index;
        ref.layout = a.layout;
        ref.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        if (depth)
            depth_ref = ref;
        else
            color_refs.push_back(ref);

        formats[index] = a.format;
        VkFramebufferAttachmentImageInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO};
        fi.usage = a.usage;
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layerCount = 1;
        fi.viewFormatCount = 1;
        fb_infos.push_back(fi);
    };
    for (const auto& c : desc.colors) add(c, false);
    if (desc.depth) add(*desc.depth, true);
    for (usize i = 0; i < fb_infos.size(); ++i) fb_infos[i].pViewFormats = &formats[i];

    VkSubpassDescription2 sub{VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = static_cast<u32>(color_refs.size());
    sub.pColorAttachments = color_refs.data();
    sub.pDepthStencilAttachment = desc.depth ? &depth_ref : nullptr;

    VkRenderPassCreateInfo2 rci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    rci.attachmentCount = static_cast<u32>(atts.size());
    rci.pAttachments = atts.data();
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    TEFRA_VK_CHECK(vkCreateRenderPass2(dev.device(), &rci, nullptr, &rp.pass));

    VkFramebufferAttachmentsCreateInfo faci{VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO};
    faci.attachmentImageInfoCount = static_cast<u32>(fb_infos.size());
    faci.pAttachmentImageInfos = fb_infos.data();
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.pNext = &faci;
    fci.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
    fci.renderPass = rp.pass;
    fci.attachmentCount = static_cast<u32>(fb_infos.size());
    fci.width = extent.width;
    fci.height = extent.height;
    fci.layers = 1;
    TEFRA_VK_CHECK(vkCreateFramebuffer(dev.device(), &fci, nullptr, &rp.framebuffer));
    if (name) {
        dev.set_name(VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<u64>(rp.pass), name);
        dev.set_name(VK_OBJECT_TYPE_FRAMEBUFFER, reinterpret_cast<u64>(rp.framebuffer), name);
    }
    return rp;
}

void destroy_render_pass(const Device& dev, RenderPassObjects& rp) {
    if (rp.framebuffer) vkDestroyFramebuffer(dev.device(), rp.framebuffer, nullptr);
    if (rp.pass) vkDestroyRenderPass(dev.device(), rp.pass, nullptr);
    rp = {};
}

void cmd_begin_render_pass(VkCommandBuffer cmd, const RenderPassObjects& rp, std::span<const VkImageView> views,
                           std::span<const VkClearValue> clears) {
    VkRenderPassAttachmentBeginInfo abi{VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO};
    abi.attachmentCount = static_cast<u32>(views.size());
    abi.pAttachments = views.data();
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.pNext = &abi;
    bi.renderPass = rp.pass;
    bi.framebuffer = rp.framebuffer;
    bi.renderArea = {{0, 0}, rp.extent};
    bi.clearValueCount = static_cast<u32>(clears.size());
    bi.pClearValues = clears.data();
    VkSubpassBeginInfo sbi{VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO};
    sbi.contents = VK_SUBPASS_CONTENTS_INLINE;
    vkCmdBeginRenderPass2(cmd, &bi, &sbi);

    VkViewport vp{0.0f, 0.0f, static_cast<f32>(rp.extent.width), static_cast<f32>(rp.extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, rp.extent};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void cmd_end_render_pass(VkCommandBuffer cmd) {
    VkSubpassEndInfo sei{VK_STRUCTURE_TYPE_SUBPASS_END_INFO};
    vkCmdEndRenderPass2(cmd, &sei);
}

VkPipeline create_graphics_pipeline(const Device& dev, const GraphicsPipelineDesc& d) {
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = d.vs;
    stages[0].pName = "main";
    stages[0].pSpecializationInfo = d.specialization;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = d.fs;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = d.specialization;
    const u32 stage_count = d.fs ? 2u : 1u;

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = d.topology;
    VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vps.viewportCount = 1;
    vps.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = d.polygon;
    rs.cullMode = d.cull;
    rs.frontFace = d.front_face;
    rs.depthClampEnable = d.depth_clamp ? VK_TRUE : VK_FALSE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = d.depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = d.depth_compare;

    std::vector<VkPipelineColorBlendAttachmentState> blends(d.color_count);
    for (auto& b : blends) {
        b = {};
        b.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (d.premultiplied_blend) {
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.colorBlendOp = VK_BLEND_OP_ADD;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            b.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (d.alpha_blend) {
            b.blendEnable = VK_TRUE;
            b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.colorBlendOp = VK_BLEND_OP_ADD;
            b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            b.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = d.color_count;
    cb.pAttachments = blends.data();

    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = stage_count;
    pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vps;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = d.layout;
    pci.renderPass = d.render_pass;
    pci.subpass = 0;
    VkPipeline pipeline = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateGraphicsPipelines(dev.device(), VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline));
    if (d.name) dev.set_name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(pipeline), d.name);
    return pipeline;
}

VkPipeline create_compute_pipeline(const Device& dev, VkShaderModule cs, VkPipelineLayout layout, const char* name) {
    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pci.stage.module = cs;
    pci.stage.pName = "main";
    pci.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateComputePipelines(dev.device(), VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline));
    if (name) dev.set_name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(pipeline), name);
    return pipeline;
}

VkPipelineLayout create_pipeline_layout(const Device& dev, std::span<const VkDescriptorSetLayout> sets,
                                        u32 push_constant_bytes) {
    TEFRA_VERIFY(push_constant_bytes <= dev.caps().max_push_constants);
    VkPushConstantRange range{VK_SHADER_STAGE_ALL, 0, push_constant_bytes};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = static_cast<u32>(sets.size());
    lci.pSetLayouts = sets.data();
    lci.pushConstantRangeCount = push_constant_bytes > 0 ? 1u : 0u;
    lci.pPushConstantRanges = &range;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreatePipelineLayout(dev.device(), &lci, nullptr, &layout));
    return layout;
}

} // namespace tefra::rhi
