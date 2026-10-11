#include "tefra/renderer/atmosphere.h"

#include "tefra/core/log.h"
#include "tefra/core/time.h"
#include "tefra/rhi/bindless.h"
#include "tefra/rhi/upload.h"

#include <cmath>

namespace tefra::render {
namespace {

f32 tc(u32 i, u32 n) { return (static_cast<f32>(i) + 0.5f) / static_cast<f32>(n); }
f32 fu(u32 v) { return static_cast<f32>(v); }

constexpr f64 kPiD = 3.14159265358979323846;

vec3 vexp(vec3 v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }

// Distance to the outer boundary of a sphere of radius R from radius r with zenith cosine mu (r <= R).
f64 dist_to_top(f64 r, f64 mu, f64 R) {
    const f64 disc = r * r * (mu * mu - 1.0) + R * R;
    return max(0.0, -r * mu + std::sqrt(max(disc, 0.0)));
}

// Distance to the ground sphere, or -1 if missed.
f64 dist_to_ground(f64 r, f64 mu, f64 Rb) {
    const f64 disc = r * r * (mu * mu - 1.0) + Rb * Rb;
    if (mu >= 0.0 || disc < 0.0) return -1.0;
    return -r * mu - std::sqrt(disc);
}

f32 phase_rayleigh(f32 c) { return 3.0f / (16.0f * kPi) * (1.0f + c * c); }

f32 phase_mie(f32 c, f32 g) { // Cornette-Shanks
    const f32 g2 = g * g;
    const f32 k = 3.0f / (8.0f * kPi) * (1.0f - g2) / (2.0f + g2);
    return k * (1.0f + c * c) / std::pow(max(1.0f + g2 - 2.0f * g * c, 1e-6f), 1.5f);
}

vec3 bilinear(const std::vector<vec3>& lut, u32 w, u32 h, f32 u, f32 v) {
    const f32 x = clamp(u, 0.0f, 1.0f) * f32(w - 1), y = clamp(v, 0.0f, 1.0f) * f32(h - 1);
    const u32 x0 = static_cast<u32>(x), y0 = static_cast<u32>(y);
    const u32 x1 = min(x0 + 1, w - 1), y1 = min(y0 + 1, h - 1);
    const f32 fx = x - f32(x0), fy = y - f32(y0);
    const vec3 a = lerp(lut[y0 * w + x0], lut[y0 * w + x1], fx);
    const vec3 b = lerp(lut[y1 * w + x0], lut[y1 * w + x1], fx);
    return lerp(a, b, fy);
}

std::vector<u16> to_half_rgba(const std::vector<vec3>& v) {
    std::vector<u16> out(v.size() * 4);
    for (usize i = 0; i < v.size(); ++i) {
        out[i * 4 + 0] = f32_to_f16(v[i].x);
        out[i * 4 + 1] = f32_to_f16(v[i].y);
        out[i * 4 + 2] = f32_to_f16(v[i].z);
        out[i * 4 + 3] = f32_to_f16(1.0f);
    }
    return out;
}

} // namespace

Atmosphere::Atmosphere(const rhi::Device& dev, rhi::BindlessHeap& heap, const AtmosphereParams& params)
    : dev_(dev), heap_(heap), p_(params) {
    const u64 t0 = time_now_ns();
    build_transmittance();
    build_multiscatter();
    auto upload = [&](const std::vector<vec3>& data, u32 w, u32 h, const char* name, rhi::Image& img) {
        const std::vector<u16> half = to_half_rgba(data);
        rhi::TextureUpload up;
        up.pixels = half.data();
        up.width = w;
        up.height = h;
        up.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        up.generate_mips = false;
        up.name = name;
        img = rhi::upload_texture(dev_, up);
        return heap_.add_texture(img.view);
    };
    trans_index_ = upload(trans_, kTransW, kTransH, "atmo_transmittance", trans_img_);
    ms_index_ = upload(ms_, kMsSize, kMsSize, "atmo_multiscatter", ms_img_);
    heap_.flush_all_idle();
    TEFRA_LOG_INFO("Atmosphere LUTs in %.1f ms", ns_to_ms(time_now_ns() - t0));
}

Atmosphere::~Atmosphere() {
    heap_.remove_texture(trans_index_);
    heap_.remove_texture(ms_index_);
    rhi::destroy_image(dev_, trans_img_);
    rhi::destroy_image(dev_, ms_img_);
}

void Atmosphere::scattering(f32 h, vec3& rayleigh, f32& mie_s, vec3& extinction) const {
    const f32 dr = std::exp(-h / p_.rayleigh_scale_height);
    const f32 dm = std::exp(-h / p_.mie_scale_height) * p_.haze;
    const f32 doz = max(0.0f, 1.0f - std::fabs(h - p_.ozone_center) / p_.ozone_half_width);
    rayleigh = p_.rayleigh_scattering * dr;
    mie_s = p_.mie_scattering * dm;
    extinction = rayleigh + vec3{p_.mie_extinction * dm} + p_.ozone_absorption * doz;
}

void Atmosphere::build_transmittance() {
    trans_.resize(kTransW * kTransH);
    const f64 Rb = p_.bottom_radius, Rt = p_.top_radius;
    const f64 H = std::sqrt(Rt * Rt - Rb * Rb);
    for (u32 y = 0; y < kTransH; ++y) {
        for (u32 x = 0; x < kTransW; ++x) {
            const f64 xm = (x + 0.5) / kTransW, xr = (y + 0.5) / kTransH;
            const f64 rho = H * xr;
            const f64 r = std::sqrt(rho * rho + Rb * Rb);
            const f64 dmin = Rt - r, dmax = rho + H;
            const f64 d = dmin + xm * (dmax - dmin);
            const f64 mu = d == 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
            const f64 len = dist_to_top(r, mu, Rt);
            constexpr int kSteps = 40;
            vec3 od{};
            for (int i = 0; i < kSteps; ++i) {
                const f64 t = (i + 0.5) / kSteps * len;
                const f64 rs = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
                vec3 ray, ext;
                f32 mie;
                scattering(static_cast<f32>(rs - Rb), ray, mie, ext);
                od += ext * static_cast<f32>(len / kSteps);
            }
            trans_[y * kTransW + x] = vexp(-od);
        }
    }
}

vec3 Atmosphere::transmittance(f32 altitude, f32 mu) const {
    const f64 Rb = p_.bottom_radius, Rt = p_.top_radius;
    const f64 r = clamp(Rb + f64(altitude), Rb, Rt);
    if (dist_to_ground(r, mu, Rb) >= 0.0) return {};
    const f64 H = std::sqrt(Rt * Rt - Rb * Rb);
    const f64 rho = std::sqrt(max(r * r - Rb * Rb, 0.0));
    const f64 d = dist_to_top(r, mu, Rt);
    const f64 dmin = Rt - r, dmax = rho + H;
    const f32 u = static_cast<f32>((d - dmin) / (dmax - dmin)), v = static_cast<f32>(rho / H);
    // LUT texels are centered: remap like the GPU sampler does.
    return bilinear(trans_, kTransW, kTransH, (u * fu(kTransW) - 0.5f) / fu(kTransW - 1),
                    (v * fu(kTransH) - 0.5f) / fu(kTransH - 1));
}

void Atmosphere::build_multiscatter() {
    ms_.resize(kMsSize * kMsSize);
    const f64 Rb = p_.bottom_radius, Rt = p_.top_radius;
    constexpr u32 kDirs = 8; // kDirs x kDirs directions over the sphere
    constexpr int kSteps = 20;
    for (u32 y = 0; y < kMsSize; ++y) {
        for (u32 x = 0; x < kMsSize; ++x) {
            const f32 mu_s = tc(x, kMsSize) * 2.0f - 1.0f;
            const f64 r = Rb + (y + 0.5) / kMsSize * (Rt - Rb);
            const vec3 sun{std::sqrt(max(0.0f, 1.0f - mu_s * mu_s)), mu_s, 0.0f};
            vec3 L2{}, fms{};
            for (u32 i = 0; i < kDirs; ++i) {
                for (u32 j = 0; j < kDirs; ++j) {
                    const f32 ct = 1.0f - 2.0f * tc(i, kDirs);
                    const f32 st = std::sqrt(max(0.0f, 1.0f - ct * ct));
                    const f32 ph = kTwoPi * tc(j, kDirs);
                    const vec3 dir{st * std::cos(ph), ct, st * std::sin(ph)};
                    const f64 mu = dir.y;
                    const f64 dg = dist_to_ground(r, mu, Rb);
                    const f64 len = dg >= 0.0 ? dg : dist_to_top(r, mu, Rt);
                    vec3 thr{1.0f}, lum{}, f{};
                    const f32 dt = static_cast<f32>(len / kSteps);
                    for (int s = 0; s < kSteps; ++s) {
                        const f64 t = (s + 0.5) * len / kSteps;
                        const f64 rs = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
                        const vec3 up = normalize(vec3{dir.x * f32(t), f32(r + mu * t), dir.z * f32(t)});
                        vec3 ray, ext;
                        f32 mie;
                        scattering(static_cast<f32>(rs - Rb), ray, mie, ext);
                        const vec3 sigma_s = ray + vec3{mie};
                        const vec3 tstep = vexp(-ext * dt);
                        const f32 mu_sun = dot(up, sun);
                        const vec3 tsun = transmittance(static_cast<f32>(rs - Rb), mu_sun);
                        const vec3 S = tsun * sigma_s * (1.0f / (4.0f * kPi));
                        const vec3 sint =
                            (S - S * tstep) / vec3{max(ext.x, 1e-12f), max(ext.y, 1e-12f), max(ext.z, 1e-12f)};
                        const vec3 fint = (sigma_s - sigma_s * tstep) /
                                          vec3{max(ext.x, 1e-12f), max(ext.y, 1e-12f), max(ext.z, 1e-12f)};
                        lum += thr * sint;
                        f += thr * fint;
                        thr = thr * tstep;
                    }
                    if (dg >= 0.0) {
                        const vec3 gp = normalize(vec3{dir.x * f32(dg), f32(r + mu * dg), dir.z * f32(dg)});
                        const f32 ndl = max(dot(gp, sun), 0.0f);
                        lum += thr * transmittance(0.0f, ndl) * p_.ground_albedo * (ndl / kPi);
                    }
                    L2 += lum;
                    fms += f;
                }
            }
            const f32 inv = 1.0f / fu(kDirs * kDirs);
            L2 = L2 * inv;
            fms = fms * inv;
            ms_[y * kMsSize + x] = L2 / (vec3{1.0f} - fms);
        }
    }
}

vec3 Atmosphere::multiscatter(f32 r_from_center, f32 mu_sun) const {
    const f32 u = mu_sun * 0.5f + 0.5f;
    const f32 v = (r_from_center - p_.bottom_radius) / (p_.top_radius - p_.bottom_radius);
    return bilinear(ms_, kMsSize, kMsSize, (u * fu(kMsSize) - 0.5f) / fu(kMsSize - 1),
                    (v * fu(kMsSize) - 0.5f) / fu(kMsSize - 1));
}

vec3 Atmosphere::sun_illuminance(f32 altitude, vec3 sun_dir) const {
    return transmittance(altitude, normalize(sun_dir).y) * p_.sun_illuminance_top;
}

vec3 Atmosphere::sky_radiance(f32 altitude, vec3 dir, vec3 sun_dir, u32 steps) const {
    const f64 Rb = p_.bottom_radius, Rt = p_.top_radius;
    const f64 r = Rb + max(f64(altitude), 1.0);
    const vec3 d = normalize(dir), s = normalize(sun_dir);
    const f64 mu = d.y;
    const f64 dg = dist_to_ground(r, mu, Rb);
    const f64 len = min(dg >= 0.0 ? dg : dist_to_top(r, mu, Rt), 400e3);
    const f32 c = dot(d, s);
    const f32 pr = phase_rayleigh(c), pm = phase_mie(c, p_.mie_g);
    vec3 thr{1.0f}, lum{};
    // Non-uniform steps: denser near the viewer where haze lives.
    f64 t_prev = 0.0;
    for (u32 i = 0; i < steps; ++i) {
        const f64 a = (i + 1.0) / steps;
        const f64 t_next = len * a * a;
        const f64 t = 0.5 * (t_prev + t_next);
        const f32 dt = static_cast<f32>(t_next - t_prev);
        t_prev = t_next;
        const f64 rs = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
        const vec3 up = normalize(vec3{d.x * f32(t), f32(r + mu * t), d.z * f32(t)});
        vec3 ray, ext;
        f32 mie;
        scattering(static_cast<f32>(rs - Rb), ray, mie, ext);
        const f32 mu_sun = dot(up, s);
        const vec3 tsun = transmittance(static_cast<f32>(rs - Rb), mu_sun);
        const vec3 S =
            tsun * (ray * pr + vec3{mie * pm}) + (ray + vec3{mie}) * multiscatter(static_cast<f32>(rs), mu_sun);
        const vec3 tstep = vexp(-ext * dt);
        const vec3 e{max(ext.x, 1e-12f), max(ext.y, 1e-12f), max(ext.z, 1e-12f)};
        lum += thr * (S - S * tstep) / e;
        thr = thr * tstep;
    }
    if (dg >= 0.0 && dg <= 400e3) {
        const vec3 gp = normalize(vec3{d.x * f32(dg), f32(r + mu * dg), d.z * f32(dg)});
        const f32 ndl = max(dot(gp, s), 0.0f);
        lum += thr * transmittance(0.0f, ndl) * p_.ground_albedo * (ndl / kPi);
    }
    return lum * p_.sun_illuminance_top;
}

ImageRGBA32F Atmosphere::render_equirect(vec3 sun_dir, u32 width) const {
    ImageRGBA32F img;
    img.width = width;
    img.height = width / 2;
    img.pixels.resize(usize(img.width) * img.height * 4);
    const f32 alt = p_.ground_altitude + 2.0f;
    for (u32 y = 0; y < img.height; ++y) {
        for (u32 x = 0; x < img.width; ++x) {
            const f32 u = tc(x, img.width), v = tc(y, img.height);
            const f32 theta = v * kPi, phi = (u - 0.5f) * kTwoPi;
            vec3 dir{std::sin(theta) * std::sin(phi), std::cos(theta), -std::sin(theta) * std::cos(phi)};
            vec3 L;
            if (dir.y >= -0.02f) {
                dir.y = max(dir.y, 0.0005f);
                L = sky_radiance(alt, dir, sun_dir, 20);
            } else {
                // Local ground (the valley floor): lit by sun and sky, seen through a little haze.
                const vec3 e_sun = sun_illuminance(alt, sun_dir) * max(normalize(sun_dir).y, 0.0f);
                const vec3 e_sky = sky_radiance(alt, {0, 1, 0}, sun_dir, 12) * kPi * 0.8f;
                L = p_.ground_albedo * (e_sun + e_sky) * (1.0f / kPi);
            }
            f32* px = &img.pixels[(usize(y) * img.width + x) * 4];
            px[0] = L.x;
            px[1] = L.y;
            px[2] = L.z;
            px[3] = 1.0f;
        }
    }
    return img;
}

AtmosphereGPU Atmosphere::gpu(vec3 sun_dir, u32 skyview_index) const {
    AtmosphereGPU g{};
    g.rayleigh = vec4(p_.rayleigh_scattering, p_.rayleigh_scale_height);
    g.mie = {p_.mie_scattering * p_.haze, p_.mie_extinction * p_.haze, p_.mie_scale_height, p_.mie_g};
    g.ozone = vec4(p_.ozone_absorption, p_.ozone_center);
    g.radii = {p_.bottom_radius, p_.top_radius, p_.ozone_half_width, p_.ground_altitude};
    g.ground = vec4(p_.ground_albedo, p_.sun_illuminance_top);
    g.sun = vec4(normalize(sun_dir), p_.sun_angular_radius);
    g.transmittance_lut = trans_index_;
    g.multiscatter_lut = ms_index_;
    g.skyview_lut = skyview_index;
    (void)kPiD;
    return g;
}

} // namespace tefra::render
