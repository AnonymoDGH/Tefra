#include "tefra/renderer/environment.h"

#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/commands.h"
#include "tefra/rhi/device.h"
#include "tefra/rhi/pipeline.h"
#include "tefra/rhi/upload.h"

#include <cmath>
#include <vector>

namespace tefra::render {

// ---- SH9 and BRDF math (CPU) ------------------------------------------------------------------------

namespace {

f32 tc(u32 i, u32 n) { return (static_cast<f32>(i) + 0.5f) / static_cast<f32>(n); }

void sh9_basis(vec3 d, f32 y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * d.y;
    y[2] = 0.488603f * d.z;
    y[3] = 0.488603f * d.x;
    y[4] = 1.092548f * d.x * d.y;
    y[5] = 1.092548f * d.y * d.z;
    y[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    y[7] = 1.092548f * d.x * d.z;
    y[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

f32 luminance(vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

vec3 normalize_luminance(vec3 c) { return c / luminance(c); }

f32 radical_inverse_vdc(u32 bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<f32>(bits) * 2.3283064365386963e-10f;
}

// Box-filter downsample of an equirect to the given width (height = width / 2).
ImageRGBA32F downsample_equirect(const ImageRGBA32F& src, u32 width) {
    if (src.width <= width) return src;
    ImageRGBA32F dst;
    dst.width = width;
    dst.height = width / 2;
    dst.pixels.assign(usize(dst.width) * dst.height * 4, 0.0f);
    const f64 sx = f64(src.width) / dst.width, sy = f64(src.height) / dst.height;
    for (u32 y = 0; y < dst.height; ++y) {
        const u32 y0 = u32(y * sy), y1 = max(u32((y + 1) * sy), y0 + 1);
        for (u32 x = 0; x < dst.width; ++x) {
            const u32 x0 = u32(x * sx), x1 = max(u32((x + 1) * sx), x0 + 1);
            f64 acc[4] = {};
            for (u32 j = y0; j < y1 && j < src.height; ++j)
                for (u32 i = x0; i < x1 && i < src.width; ++i)
                    for (int c = 0; c < 4; ++c)
                        acc[c] += static_cast<f64>(src.pixels[(usize(j) * src.width + i) * 4 + usize(c)]);
            const f64 n = f64((y1 - y0) * (x1 - x0));
            for (int c = 0; c < 4; ++c) dst.pixels[(usize(y) * dst.width + x) * 4 + usize(c)] = f32(acc[c] / n);
        }
    }
    return dst;
}

f32 texel_solid_angle(const ImageRGBA32F& img, u32 y) {
    const f32 theta = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(img.height) * kPi;
    return (kTwoPi / static_cast<f32>(img.width)) * (kPi / static_cast<f32>(img.height)) * std::sin(theta);
}

} // namespace

vec3 equirect_direction(f32 u, f32 v) {
    const f32 theta = v * kPi;
    const f32 phi = (u - 0.5f) * kTwoPi;
    return {std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
}

void sh9_project_equirect(const ImageRGBA32F& img, vec4 out[9]) {
    f64 acc[9][3] = {};
    for (u32 y = 0; y < img.height; ++y) {
        const f32 dw = texel_solid_angle(img, y);
        const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(img.height);
        for (u32 x = 0; x < img.width; ++x) {
            const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(img.width);
            const vec3 d = equirect_direction(u, v);
            f32 basis[9];
            sh9_basis(d, basis);
            const f32* px = &img.pixels[(usize(y) * img.width + x) * 4];
            for (int i = 0; i < 9; ++i)
                for (int c = 0; c < 3; ++c) acc[i][c] += f64(px[c]) * f64(basis[i]) * f64(dw);
        }
    }
    for (int i = 0; i < 9; ++i) out[i] = {f32(acc[i][0]), f32(acc[i][1]), f32(acc[i][2]), 0.0f};
}

void sh9_convolve_cosine(vec4 sh[9]) {
    const f32 a[9] = {kPi,        2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, kPi / 4.0f,
                      kPi / 4.0f, kPi / 4.0f,        kPi / 4.0f,        kPi / 4.0f};
    for (int i = 0; i < 9; ++i) sh[i] = sh[i] * a[i];
}

vec3 sh9_eval(const vec4 sh[9], vec3 n) {
    f32 b[9];
    sh9_basis(n, b);
    vec3 r{};
    for (int i = 0; i < 9; ++i) r += sh[i].xyz() * b[i];
    return {max(r.x, 0.0f), max(r.y, 0.0f), max(r.z, 0.0f)};
}

vec2 brdf_lut_integrate(f32 n_dot_v, f32 roughness, u32 samples) {
    n_dot_v = max(n_dot_v, 1e-4f);
    const vec3 v{std::sqrt(1.0f - n_dot_v * n_dot_v), 0.0f, n_dot_v};
    const f32 alpha = roughness * roughness;
    f32 a = 0.0f, b = 0.0f;
    for (u32 i = 0; i < samples; ++i) {
        const f32 e1 = (static_cast<f32>(i) + 0.5f) / static_cast<f32>(samples);
        const f32 e2 = radical_inverse_vdc(i);
        const f32 phi = kTwoPi * e2;
        const f32 cos_t = std::sqrt((1.0f - e1) / (1.0f + (alpha * alpha - 1.0f) * e1));
        const f32 sin_t = std::sqrt(1.0f - cos_t * cos_t);
        const vec3 h{sin_t * std::cos(phi), sin_t * std::sin(phi), cos_t};
        const vec3 l = h * (2.0f * dot(v, h)) - v;
        const f32 n_dot_l = saturate(l.z), n_dot_h = saturate(h.z), v_dot_h = saturate(dot(v, h));
        if (n_dot_l <= 0.0f) continue;
        // Height-correlated Smith visibility (Heitz 2014).
        const f32 a2 = alpha * alpha;
        const f32 gv = n_dot_l * std::sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
        const f32 gl = n_dot_v * std::sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
        const f32 vis = 0.5f / (gv + gl);
        const f32 g_term = vis * 4.0f * n_dot_l * v_dot_h / n_dot_h; // pdf-weighted
        const f32 fc = std::pow(1.0f - v_dot_h, 5.0f);
        a += (1.0f - fc) * g_term;
        b += fc * g_term;
    }
    return {a / static_cast<f32>(samples), b / static_cast<f32>(samples)};
}

// ---- Environment ------------------------------------------------------------------------------------

Environment::Environment(const rhi::Device& dev, rhi::BindlessHeap& heap, const EnvironmentSettings& settings)
    : dev_(dev), heap_(heap), settings_(settings) {
    settings_.specular_mips =
        min(settings_.specular_mips, rhi::mip_count_for(settings_.cube_size, settings_.cube_size));
    build_brdf_lut();
}

Environment::~Environment() {
    release_cube();
    if (lut_index_ != ~0u) heap_.remove_texture(lut_index_);
    rhi::destroy_image(dev_, lut_);
}

void Environment::release_cube() {
    if (cube_index_ != ~0u) heap_.remove_cube(cube_index_);
    cube_index_ = ~0u;
    rhi::destroy_image(dev_, prefiltered_);
    if (background_index_ != ~0u) heap_.remove_texture(background_index_);
    background_index_ = ~0u;
    rhi::destroy_image(dev_, background_);
}

void Environment::build_brdf_lut() {
    constexpr u32 kSize = 64;
    const u64 t0 = time_now_ns();
    std::vector<u16> px(kSize * kSize * 2);
    for (u32 y = 0; y < kSize; ++y) {
        const f32 rough = (static_cast<f32>(y) + 0.5f) / kSize;
        for (u32 x = 0; x < kSize; ++x) {
            const f32 nv = (static_cast<f32>(x) + 0.5f) / kSize;
            const vec2 ab = brdf_lut_integrate(nv, rough, 256);
            px[(y * kSize + x) * 2 + 0] = f32_to_f16(ab.x);
            px[(y * kSize + x) * 2 + 1] = f32_to_f16(ab.y);
        }
    }
    rhi::TextureUpload up;
    up.pixels = px.data();
    up.width = kSize;
    up.height = kSize;
    up.format = VK_FORMAT_R16G16_SFLOAT;
    up.generate_mips = false;
    up.name = "brdf_lut";
    lut_ = rhi::upload_texture(dev_, up);
    lut_index_ = heap_.add_texture(lut_.view);
    heap_.flush_all_idle();
    TEFRA_LOG_DEBUG("BRDF LUT %ux%u in %.1f ms", kSize, kSize, ns_to_ms(time_now_ns() - t0));
}

vec3 Environment::irradiance(vec3 n) const { return sh9_eval(sh_, n); }

void Environment::build_procedural(const ProceduralSky& sky) {
    const u32 w = max(256u, settings_.cube_size * 4), h = w / 2;
    ImageRGBA32F img;
    img.width = w;
    img.height = h;
    img.pixels.resize(usize(w) * h * 4);
    const vec3 zenith_col = normalize_luminance({0.28f, 0.40f, 0.62f}) * sky.zenith_nits;
    const vec3 horizon_col = normalize_luminance({0.74f, 0.79f, 0.85f}) * sky.horizon_nits;
    const vec3 sun_col = normalize_luminance({1.0f, 0.86f, 0.70f});
    const vec3 s = normalize(sky.sun_direction);
    // Ground irradiance: sun on a horizontal plane + average sky (pi * mean radiance).
    const f32 sky_irr = kPi * 0.5f * (sky.zenith_nits + sky.horizon_nits);
    const vec3 ground_l = sky.ground_albedo * ((sky.sun_illuminance * max(s.y, 0.0f) + sky_irr) / kPi);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            const vec3 d = equirect_direction(tc(x, w), tc(y, h));
            const f32 mu = dot(d, s);
            vec3 c;
            const f32 up = max(d.y, 0.0f);
            vec3 skyc = lerp(horizon_col, zenith_col, std::pow(up, 0.45f));
            skyc *= 0.92f + 0.08f * 0.75f * (1.0f + mu * mu);
            // Forward Mie glow: a fraction of the sun's illuminance scattered towards the viewer.
            const f32 g = 0.80f, g2 = g * g;
            const f32 phase = (1.0f - g2) / (4.0f * kPi * std::pow(1.0f + g2 - 2.0f * g * mu, 1.5f));
            skyc += sun_col * (sky.sun_illuminance * 0.04f * phase * (1.0f - 0.6f * up));
            if (d.y >= 0.0f) {
                c = skyc;
            } else {
                // Haze band just below the horizon fades into the ground.
                const f32 t = saturate(-d.y / 0.08f);
                c = lerp(horizon_col * 0.8f, ground_l, t);
            }
            f32* px = &img.pixels[(usize(y) * w + x) * 4];
            px[0] = c.x;
            px[1] = c.y;
            px[2] = c.z;
            px[3] = 1.0f;
        }
    }
    dominant_dir_ = s;
    dominant_lux_ = sky.sun_illuminance;
    build_from_equirect(img);
}

bool Environment::build_from_hdr(const char* path, f32 sky_nits) {
    ImageRGBA32F src;
    if (!image_read_hdr(path, src)) {
        TEFRA_LOG_ERROR("cannot read HDR %s", path);
        return false;
    }
    ImageRGBA32F img = downsample_equirect(src, 2048);
    // Mean luminance of the upper hemisphere (solid-angle weighted).
    f64 lum_sum = 0.0, w_sum = 0.0;
    f32 max_lum = 0.0f;
    u32 max_x = 0, max_y = 0;
    for (u32 y = 0; y < img.height / 2; ++y) {
        const f32 dw = texel_solid_angle(img, y);
        for (u32 x = 0; x < img.width; ++x) {
            const f32* p = &img.pixels[(usize(y) * img.width + x) * 4];
            const f32 l = luminance({p[0], p[1], p[2]});
            lum_sum += f64(l) * f64(dw);
            w_sum += f64(dw);
            if (l > max_lum) {
                max_lum = l;
                max_x = x;
                max_y = y;
            }
        }
    }
    const f32 mean = static_cast<f32>(lum_sum / max(w_sum, 1e-9));
    const f32 k = (sky_nits > 0.0f && mean > 0.0f) ? sky_nits / mean : 1.0f;
    // Sun detection: texels far above the mean are cut out of the map and returned as an analytic light.
    const f32 threshold = mean * 40.0f;
    dominant_dir_ = equirect_direction(tc(max_x, img.width), tc(max_y, img.height));
    dominant_lux_ = 0.0f;
    for (u32 y = 0; y < img.height; ++y) {
        const f32 dw = texel_solid_angle(img, y);
        for (u32 x = 0; x < img.width; ++x) {
            f32* p = &img.pixels[(usize(y) * img.width + x) * 4];
            const f32 l = luminance({p[0], p[1], p[2]});
            if (max_lum > threshold && l > threshold) {
                const f32 excess = (l - threshold) / l;
                const vec3 d = equirect_direction(tc(x, img.width), tc(y, img.height));
                dominant_lux_ += (l - threshold) * k * dw * max(dot(d, dominant_dir_), 0.0f);
                for (int c = 0; c < 3; ++c) p[c] *= (1.0f - excess);
            }
            for (int c = 0; c < 3; ++c) p[c] *= k;
        }
    }
    TEFRA_LOG_INFO("HDR %s: %ux%u, mean sky %.1f -> %.0f nits, sun %.0f lux", path, src.width, src.height, f64(mean),
                   f64(mean * k), f64(dominant_lux_));
    build_from_equirect(img);
    return true;
}

void Environment::build_from_equirect(const ImageRGBA32F& source) {
    const u64 t0 = time_now_ns();
    release_cube();
    {
        // Full-resolution (<= 2048) equirect for the visible background, with mips against aliasing.
        const ImageRGBA32F bg = downsample_equirect(source, 2048);
        std::vector<u16> h(bg.pixels.size());
        for (usize i = 0; i < h.size(); ++i) h[i] = f32_to_f16(min(bg.pixels[i], 65000.0f));
        rhi::TextureUpload bu;
        bu.pixels = h.data();
        bu.width = bg.width;
        bu.height = bg.height;
        bu.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        bu.name = "env_background";
        background_ = rhi::upload_texture(dev_, bu);
        background_index_ = heap_.add_texture(background_.view);
    }
    const ImageRGBA32F eq = downsample_equirect(source, max(256u, settings_.cube_size * 4));
    sh9_project_equirect(eq, sh_);
    sh9_convolve_cosine(sh_);

    // Upload the equirect as half floats.
    std::vector<u16> half(eq.pixels.size());
    for (usize i = 0; i < half.size(); ++i) half[i] = f32_to_f16(min(eq.pixels[i], 65000.0f));
    rhi::TextureUpload up;
    up.pixels = half.data();
    up.width = eq.width;
    up.height = eq.height;
    up.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    up.generate_mips = false;
    up.name = "env_equirect";
    rhi::Image equirect = rhi::upload_texture(dev_, up);
    const u32 equirect_index = heap_.add_texture(equirect.view);

    const u32 size = settings_.cube_size;
    const u32 src_mips = rhi::mip_count_for(size, size);
    rhi::ImageDesc cd;
    cd.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    cd.width = size;
    cd.height = size;
    cd.layers = 6;
    cd.view_type = VK_IMAGE_VIEW_TYPE_CUBE;
    cd.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cd.mip_levels = src_mips;
    cd.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    cd.name = "env_source_cube";
    rhi::Image source_cube = rhi::create_image(dev_, cd);
    cd.mip_levels = settings_.specular_mips;
    cd.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    cd.name = "env_prefiltered";
    prefiltered_ = rhi::create_image(dev_, cd);
    const u32 source_cube_index = heap_.add_cube(source_cube.view);
    heap_.flush_all_idle();

    // Storage views (2D array, one mip each) and their descriptor sets.
    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &b;
    VkDescriptorSetLayout storage_layout = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateDescriptorSetLayout(dev_.device(), &lci, nullptr, &storage_layout));
    const u32 view_count = 1 + settings_.specular_mips;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, view_count};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = view_count;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    TEFRA_VK_CHECK(vkCreateDescriptorPool(dev_.device(), &pci, nullptr, &pool));
    std::vector<VkImageView> views(view_count);
    std::vector<VkDescriptorSet> sets(view_count);
    for (u32 i = 0; i < view_count; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = i == 0 ? source_cube.image : prefiltered_.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i == 0 ? 0u : i - 1u, 1, 0, 6};
        TEFRA_VK_CHECK(vkCreateImageView(dev_.device(), &vci, nullptr, &views[i]));
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &storage_layout;
        TEFRA_VK_CHECK(vkAllocateDescriptorSets(dev_.device(), &ai, &sets[i]));
        VkDescriptorImageInfo ii{VK_NULL_HANDLE, views[i], VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet wd{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wd.dstSet = sets[i];
        wd.descriptorCount = 1;
        wd.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wd.pImageInfo = &ii;
        vkUpdateDescriptorSets(dev_.device(), 1, &wd, 0, nullptr);
    }

    struct Push {
        u32 src_index;
        u32 size;
        f32 roughness;
        u32 samples;
        f32 src_size;
        u32 pad[3];
    };
    const VkDescriptorSetLayout set_layouts[2] = {heap_.layout(), storage_layout};
    VkPipelineLayout layout = rhi::create_pipeline_layout(dev_, set_layouts, sizeof(Push));
    VkShaderModule cs_convert = rhi::load_shader(dev_, "ibl_equirect_to_cube.comp");
    VkShaderModule cs_prefilter = rhi::load_shader(dev_, "ibl_prefilter.comp");
    VkPipeline p_convert = rhi::create_compute_pipeline(dev_, cs_convert, layout, "ibl_convert");
    VkPipeline p_prefilter = rhi::create_compute_pipeline(dev_, cs_prefilter, layout, "ibl_prefilter");

    rhi::ImmediateContext ctx(dev_);
    VkCommandBuffer cmd = ctx.begin();
    VkDescriptorSet bindless = heap_.set(0);

    // 1. Equirect -> source cube mip 0.
    rhi::ImageBarrier ib;
    ib.image = source_cube.image;
    ib.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.new_layout = VK_IMAGE_LAYOUT_GENERAL;
    ib.base_mip = 0;
    ib.mip_count = 1;
    ib.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.dst_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    rhi::cmd_image_barrier(cmd, ib);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_convert);
    VkDescriptorSet s0[2] = {bindless, sets[0]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, s0, 0, nullptr);
    Push push{equirect_index, size, 0.0f, 0, f32(size), {}};
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(push), &push);
    vkCmdDispatch(cmd, (size + 7) / 8, (size + 7) / 8, 6);

    // 2. Mip chain of the source cube (for filtered importance sampling).
    ib.old_layout = VK_IMAGE_LAYOUT_GENERAL;
    ib.new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    ib.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    ib.src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    ib.dst_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
    ib.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
    rhi::cmd_image_barrier(cmd, ib);
    i32 mw = static_cast<i32>(size);
    for (u32 level = 1; level < src_mips; ++level) {
        rhi::ImageBarrier dst;
        dst.image = source_cube.image;
        dst.base_mip = level;
        dst.mip_count = 1;
        dst.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        dst.new_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dst.dst_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        dst.dst_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        rhi::cmd_image_barrier(cmd, dst);
        const i32 nw = max(mw / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 6};
        blit.srcOffsets[1] = {mw, mw, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 6};
        blit.dstOffsets[1] = {nw, nw, 1};
        vkCmdBlitImage(cmd, source_cube.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source_cube.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        dst.old_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dst.new_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        dst.src_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
        dst.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        dst.dst_access = VK_ACCESS_2_TRANSFER_READ_BIT;
        rhi::cmd_image_barrier(cmd, dst);
        mw = nw;
    }
    rhi::ImageBarrier to_read;
    to_read.image = source_cube.image;
    to_read.old_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_read.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_read.src_stage = VK_PIPELINE_STAGE_2_BLIT_BIT;
    to_read.src_access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_read.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    to_read.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    rhi::cmd_image_barrier(cmd, to_read);

    // 3. GGX prefilter into each mip of the output cube.
    rhi::ImageBarrier pf;
    pf.image = prefiltered_.image;
    pf.old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    pf.new_layout = VK_IMAGE_LAYOUT_GENERAL;
    pf.dst_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    pf.dst_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    rhi::cmd_image_barrier(cmd, pf);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p_prefilter);
    for (u32 m = 0; m < settings_.specular_mips; ++m) {
        const u32 msize = max(size >> m, 1u);
        VkDescriptorSet sm[2] = {bindless, sets[1 + m]};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, sm, 0, nullptr);
        const f32 rough = settings_.specular_mips > 1 ? f32(m) / f32(settings_.specular_mips - 1) : 0.0f;
        Push p{source_cube_index, msize, rough, settings_.samples, f32(size), {}};
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_ALL, 0, sizeof(p), &p);
        vkCmdDispatch(cmd, (msize + 7) / 8, (msize + 7) / 8, 6);
    }
    pf.old_layout = VK_IMAGE_LAYOUT_GENERAL;
    pf.new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pf.src_stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    pf.src_access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    pf.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    pf.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    rhi::cmd_image_barrier(cmd, pf);
    ctx.submit_and_wait();

    vkDestroyPipeline(dev_.device(), p_convert, nullptr);
    vkDestroyPipeline(dev_.device(), p_prefilter, nullptr);
    vkDestroyShaderModule(dev_.device(), cs_convert, nullptr);
    vkDestroyShaderModule(dev_.device(), cs_prefilter, nullptr);
    vkDestroyPipelineLayout(dev_.device(), layout, nullptr);
    for (VkImageView v : views) vkDestroyImageView(dev_.device(), v, nullptr);
    vkDestroyDescriptorPool(dev_.device(), pool, nullptr);
    vkDestroyDescriptorSetLayout(dev_.device(), storage_layout, nullptr);
    heap_.remove_cube(source_cube_index);
    heap_.remove_texture(equirect_index);
    rhi::destroy_image(dev_, source_cube);
    rhi::destroy_image(dev_, equirect);

    cube_index_ = heap_.add_cube(prefiltered_.view);
    heap_.flush_all_idle();
    TEFRA_LOG_INFO("Environment: cube %u^2 x %u mips, %u samples, built in %.1f ms", size, settings_.specular_mips,
                   settings_.samples, ns_to_ms(time_now_ns() - t0));
}

} // namespace tefra::render
