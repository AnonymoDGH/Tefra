#include "tefra/renderer/terrain_field.h"

#include "tefra/core/fs.h"
#include "tefra/core/jobs.h"
#include "tefra/core/log.h"
#include "tefra/core/time.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace tefra::render {
namespace {

u32 hash2(i32 x, i32 y, u32 seed) {
    u32 h = static_cast<u32>(x) * 0x8da6b343u ^ static_cast<u32>(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

// cos / sin of every gradient angle code (16 bits). Built with the same expressions the noise used to evaluate per
// corner, so the samples are bit-identical; the table replaces ~8 transcendental calls per noise lookup.
struct GradientTable {
    std::vector<f32> cs, sn;
    GradientTable() : cs(65536), sn(65536) {
        for (u32 code = 0; code < 65536u; ++code) {
            const f32 a = static_cast<f32>(code) * (kTwoPi / 65536.0f);
            cs[code] = std::cos(a);
            sn[code] = std::sin(a);
        }
    }
};

const GradientTable& gradient_table() {
    static const GradientTable table;
    return table;
}

// 2D gradient noise in [-1, 1] (quintic fade).
f32 gnoise(f32 x, f32 y, u32 seed) {
    const f32 fx = std::floor(x), fy = std::floor(y);
    const i32 ix = static_cast<i32>(fx), iy = static_cast<i32>(fy);
    const f32 tx = x - fx, ty = y - fy;
    const GradientTable& gt = gradient_table();
    auto grad = [&](i32 cx, i32 cy, f32 dx, f32 dy) {
        const u32 code = hash2(cx, cy, seed) & 0xFFFFu;
        return gt.cs[code] * dx + gt.sn[code] * dy;
    };
    const f32 n00 = grad(ix, iy, tx, ty), n10 = grad(ix + 1, iy, tx - 1, ty);
    const f32 n01 = grad(ix, iy + 1, tx, ty - 1), n11 = grad(ix + 1, iy + 1, tx - 1, ty - 1);
    auto fade = [](f32 t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const f32 u = fade(tx), v = fade(ty);
    return 1.41f * lerp(lerp(n00, n10, u), lerp(n01, n11, u), v);
}

f32 fbm(f32 x, f32 y, u32 seed, int octaves, f32 lacunarity = 2.03f, f32 gain = 0.5f) {
    f32 sum = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gnoise(x * freq, y * freq, seed + static_cast<u32>(i) * 101u);
        freq *= lacunarity;
        amp *= gain;
    }
    return sum;
}

f32 ridged(f32 x, f32 y, u32 seed, int octaves) {
    f32 sum = 0.0f, amp = 0.5f, freq = 1.0f, prev = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        f32 n = 1.0f - std::fabs(gnoise(x * freq, y * freq, seed + static_cast<u32>(i) * 57u));
        n *= n;
        sum += n * amp * prev;
        prev = n;
        freq *= 2.1f;
        amp *= 0.5f;
    }
    return sum;
}

f32 smoothstep(f32 a, f32 b, f32 x) {
    const f32 t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Felzenszwalb & Huttenlocher 1D squared distance transform (f: 0 at sites, large elsewhere).
void edt_1d(const f32* f, f32* d, u32 n, std::vector<i32>& v, std::vector<f32>& z) {
    v.resize(n);
    z.resize(n + 1);
    u32 k = 0;
    v[0] = 0;
    z[0] = -1e30f;
    z[1] = 1e30f;
    for (u32 q = 1; q < n; ++q) {
        f32 sq = 0;
        for (;;) {
            const i32 p = v[k];
            sq = ((f[q] + f32(q) * f32(q)) - (f[p] + f32(p) * f32(p))) / (2.0f * f32(q) - 2.0f * f32(p));
            if (sq <= z[k] && k > 0) {
                --k;
                continue;
            }
            break;
        }
        ++k;
        v[k] = i32(q);
        z[k] = sq;
        z[k + 1] = 1e30f;
    }
    k = 0;
    for (u32 q = 0; q < n; ++q) {
        while (z[k + 1] < f32(q)) ++k;
        const f32 dq = f32(q) - f32(v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

// 2D Euclidean distance (texels) to the nearest site (sites[i] != 0). Every column and row is independent: the passes
// run as parallel jobs with per-chunk scratch buffers (the result is identical to a serial sweep).
std::vector<f32> edt_2d(const std::vector<u8>& sites, u32 n) {
    std::vector<f32> g(usize(n) * n);
    global_jobs().parallel_for(n, 16, [&](u32 y0, u32 y1) {
        for (usize i = usize(y0) * n; i < usize(y1) * n; ++i) g[i] = sites[i] ? 0.0f : 1e20f;
    });
    global_jobs().parallel_for(n, 16, [&](u32 x0, u32 x1) {
        std::vector<f32> col(n), out(n);
        std::vector<i32> v;
        std::vector<f32> z;
        for (u32 x = x0; x < x1; ++x) {
            for (u32 y = 0; y < n; ++y) col[y] = g[usize(y) * n + x];
            edt_1d(col.data(), out.data(), n, v, z);
            for (u32 y = 0; y < n; ++y) g[usize(y) * n + x] = out[y];
        }
    });
    global_jobs().parallel_for(n, 16, [&](u32 y0, u32 y1) {
        std::vector<f32> out(n);
        std::vector<i32> v;
        std::vector<f32> z;
        for (u32 y = y0; y < y1; ++y) {
            edt_1d(&g[usize(y) * n], out.data(), n, v, z);
            for (u32 x = 0; x < n; ++x) g[usize(y) * n + x] = std::sqrt(out[x]);
        }
    });
    return g;
}

// ---- Disk cache
// ------------------------------------------------------------------------------------------------------ The generated
// samples are stored under config_dir()/cache/terrain, one file per field: a small header (the descriptor that produced
// it) followed by the raw heights, mask and shore arrays. Bump kFieldCacheVersion whenever a generator changes its
// output. TEFRA_NO_FIELD_CACHE=1 disables the cache (generation is always bit-identical).
constexpr u32 kFieldCacheVersion = 1;
constexpr char kFieldMagic[4] = {'T', 'F', 'L', 'D'};

u32 bits_of(f32 v) {
    u32 u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

f32 float_of(u32 u) {
    f32 v;
    std::memcpy(&v, &u, sizeof v);
    return v;
}

// Only the fields that change the samples belong to the key (sea level only shapes a coast).
std::string field_cache_path(const TerrainDesc& d) {
    if (const char* off = std::getenv("TEFRA_NO_FIELD_CACHE"); off && off[0] == '1') return {};
    const char* kind = d.kind == TerrainKind::Coast ? "coast" : d.kind == TerrainKind::Plains ? "plains" : "valley";
    const u32 sea_bits = d.kind == TerrainKind::Coast ? bits_of(d.sea_level) : 0u;
    char name[192];
    std::snprintf(name, sizeof name, "%s-s%u-n%u-t%08x-h%08x-v%u.tfld", kind, d.seed, d.resolution,
                  bits_of(d.texel_size), sea_bits, kFieldCacheVersion);
    return path_join(path_join(config_dir(), "cache/terrain"), name);
}

void put_u32(std::vector<u8>& out, u32 v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
}

u32 get_u32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }

constexpr usize kFieldHeaderBytes = 4 + 4 * 11; // magic + 11 words

struct FieldArrays {
    std::vector<f32>& heights;
    std::vector<u8>& mask;
    std::vector<f32>& shore;
    f32& min_h;
    f32& max_h;
};

// Header: magic, version, kind, seed, resolution, texel bits, sea bits, heights, mask bytes, shore count, min, max.
bool read_field_file(const std::string& path, const TerrainDesc& d, FieldArrays a) {
    std::vector<u8> bytes;
    if (path.empty() || !file_read_all(path.c_str(), bytes) || bytes.size() < kFieldHeaderBytes) return false;
    const u8* p = bytes.data();
    if (std::memcmp(p, kFieldMagic, 4) != 0) return false;
    const u8* w = p + 4;
    const u32 n = d.resolution;
    const u32 sea_bits = d.kind == TerrainKind::Coast ? bits_of(d.sea_level) : 0u;
    if (get_u32(w) != kFieldCacheVersion || get_u32(w + 4) != u32(d.kind) || get_u32(w + 8) != d.seed ||
        get_u32(w + 12) != n || get_u32(w + 16) != bits_of(d.texel_size) || get_u32(w + 20) != sea_bits)
        return false;
    const u32 heights = get_u32(w + 24), mask_bytes = get_u32(w + 28), shore = get_u32(w + 32);
    if (heights != n * n || mask_bytes != (n / 2) * (n / 2) * 4 ||
        shore != (d.kind == TerrainKind::Coast ? (n / 2) * (n / 2) : 0u))
        return false;
    const usize expected = kFieldHeaderBytes + usize(heights) * 4 + mask_bytes + usize(shore) * 4;
    if (bytes.size() != expected) return false;
    a.min_h = float_of(get_u32(w + 36));
    a.max_h = float_of(get_u32(w + 40));
    usize off = kFieldHeaderBytes;
    a.heights.resize(heights);
    std::memcpy(a.heights.data(), p + off, usize(heights) * 4);
    off += usize(heights) * 4;
    a.mask.resize(mask_bytes);
    std::memcpy(a.mask.data(), p + off, mask_bytes);
    off += mask_bytes;
    a.shore.resize(shore);
    if (shore) std::memcpy(a.shore.data(), p + off, usize(shore) * 4);
    return true;
}

void write_field_file(const std::string& path, const TerrainDesc& d, const FieldArrays& a) {
    if (path.empty()) return;
    std::vector<u8> out;
    out.reserve(kFieldHeaderBytes + a.heights.size() * 4 + a.mask.size() + a.shore.size() * 4);
    out.insert(out.end(), kFieldMagic, kFieldMagic + 4);
    put_u32(out, kFieldCacheVersion);
    put_u32(out, u32(d.kind));
    put_u32(out, d.seed);
    put_u32(out, d.resolution);
    put_u32(out, bits_of(d.texel_size));
    put_u32(out, d.kind == TerrainKind::Coast ? bits_of(d.sea_level) : 0u);
    put_u32(out, u32(a.heights.size()));
    put_u32(out, u32(a.mask.size()));
    put_u32(out, u32(a.shore.size()));
    put_u32(out, bits_of(a.min_h));
    put_u32(out, bits_of(a.max_h));
    const u8* hp = reinterpret_cast<const u8*>(a.heights.data());
    out.insert(out.end(), hp, hp + a.heights.size() * 4);
    out.insert(out.end(), a.mask.begin(), a.mask.end());
    const u8* sp = reinterpret_cast<const u8*>(a.shore.data());
    out.insert(out.end(), sp, sp + a.shore.size() * 4);
    // Written beside the target and renamed: a crash never leaves a truncated file behind.
    make_dirs(path_parent(path).c_str());
    static std::atomic<u32> writes{0}; // acquire() may generate the same field twice on two threads
    const std::string tmp = path + ".tmp" + std::to_string(writes.fetch_add(1));
    if (file_write_all(tmp.c_str(), out.data(), out.size())) {
        rename_path(tmp, path);
        // A sea-level drag writes one field per value: keep the most recent few (each is ~24 MB at 2048^2).
        keep_newest_files(path_parent(path), ".tfld", 6);
    }
}

// Process-wide cache of recent fields (most recent first).
struct FieldCache {
    std::mutex mutex;
    std::vector<std::shared_ptr<const TerrainField>> fields;
};
FieldCache& field_cache() {
    static FieldCache cache;
    return cache;
}

void cache_insert(const std::shared_ptr<const TerrainField>& f) {
    FieldCache& c = field_cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    for (auto it = c.fields.begin(); it != c.fields.end(); ++it)
        if (TerrainField::same_field((*it)->desc(), f->desc())) {
            c.fields.erase(it);
            break;
        }
    c.fields.insert(c.fields.begin(), f);
    if (c.fields.size() > TerrainField::kCacheSize) c.fields.resize(TerrainField::kCacheSize);
}

} // namespace

bool TerrainField::same_field(const TerrainDesc& a, const TerrainDesc& b) {
    if (a.kind != b.kind || a.seed != b.seed || a.resolution != b.resolution || a.texel_size != b.texel_size)
        return false;
    return a.kind != TerrainKind::Coast || a.sea_level == b.sea_level;
}

std::shared_ptr<const TerrainField> TerrainField::find_cached(const TerrainDesc& desc) {
    FieldCache& c = field_cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    for (const auto& f : c.fields)
        if (same_field(f->desc(), desc)) return f;
    return nullptr;
}

void TerrainField::clear_cache() {
    FieldCache& c = field_cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    c.fields.clear();
}

std::shared_ptr<const TerrainField> TerrainField::acquire(const TerrainDesc& desc) {
    if (auto f = find_cached(desc)) {
        cache_insert(f); // most recently used
        return f;
    }
    std::shared_ptr<const TerrainField> base;
    if (desc.kind == TerrainKind::Coast) {
        FieldCache& c = field_cache();
        std::lock_guard<std::mutex> lock(c.mutex);
        for (const auto& f : c.fields) {
            const TerrainDesc& d = f->desc();
            if (d.kind == desc.kind && d.seed == desc.seed && d.resolution == desc.resolution &&
                d.texel_size == desc.texel_size) {
                base = f;
                break;
            }
        }
    }
    std::shared_ptr<const TerrainField> made;
    if (base)
        made.reset(new TerrainField(*base, desc.sea_level, Shifted{}));
    else
        made = std::make_shared<const TerrainField>(desc);
    cache_insert(made);
    return made;
}

TerrainField::TerrainField(const TerrainDesc& desc) : desc_(desc) {
    desc_.resolution = max(desc_.resolution, 4u) & ~1u; // even: the mask and shore fields are half resolution
    desc_.texel_size = max(desc_.texel_size, 1e-3f);
    const u64 t0 = time_now_ns();
    const std::string cache = field_cache_path(desc_);
    FieldArrays arrays{heights_, mask_, shore_, min_h_, max_h_};
    if (read_field_file(cache, desc_, arrays)) {
        build_ms_ = ns_to_ms(time_now_ns() - t0);
        TEFRA_LOG_INFO("TerrainField: loaded %s from the cache in %.0f ms", path_filename(cache).c_str(), build_ms_);
        return;
    }
    generate();
    build_ms_ = ns_to_ms(time_now_ns() - t0);
    write_field_file(cache, desc_, arrays);
}

TerrainField::TerrainField(const TerrainField& src, f32 sea_level, Shifted)
    : desc_(src.desc_), heights_(src.heights_), mask_(src.mask_), shore_(src.shore_) {
    // The coast is built relative to the sea: moving the sea moves the whole landscape, the mask and the shore
    // distance stay the same.
    const f32 delta = sea_level - src.desc_.sea_level;
    desc_.sea_level = sea_level;
    for (f32& h : heights_) h += delta;
    min_h_ = src.min_h_ + delta;
    max_h_ = src.max_h_ + delta;
}

f32 TerrainField::river_center_x(f32 z) const {
    return 110.0f * std::sin(z / 420.0f) + 45.0f * std::sin(z / 160.0f + 1.3f) + 12.0f * std::sin(z / 47.0f + 0.4f);
}

f32 TerrainField::river_distance(f32 x, f32 z) const { return x - river_center_x(z); }

f32 TerrainField::valley_height(f32 x, f32 z) const {
    const u32 seed = desc_.seed;
    // Domain warp for organic shapes.
    const f32 wx = x + 60.0f * fbm(x / 700.0f, z / 700.0f, seed + 7, 3);
    const f32 wz = z + 60.0f * fbm(x / 700.0f + 5.2f, z / 700.0f + 1.3f, seed + 9, 3);
    const f32 d = std::fabs(wx - river_center_x(wz));
    // Valley walls: flat floor (~60 m wide), steepening slopes, crest ridges near the map edge.
    const f32 dv = min(d, std::fabs(x - river_center_x(z)) + 25.0f); // keep the floor around the channel
    const f32 wall = std::pow(max(dv - 30.0f, 0.0f) / 900.0f, 1.55f) * 260.0f;
    const f32 ridges = ridged(wx / 520.0f, wz / 520.0f, seed + 3, 5) * 70.0f * smoothstep(60.0f, 400.0f, d);
    const f32 hills = fbm(wx / 230.0f, wz / 230.0f, seed + 11, 5) * 14.0f * smoothstep(20.0f, 200.0f, d);
    const f32 floor_bumps = fbm(x / 35.0f, z / 35.0f, seed + 13, 4) * 1.6f;
    f32 h = 20.0f + wall + ridges + hills + floor_bumps;
    // River channel and banks follow the unwarped centerline (water and gameplay use it).
    const f32 dc = std::fabs(x - river_center_x(z));
    const f32 channel = 1.0f - smoothstep(3.0f, 9.0f, dc);
    h -= channel * 2.6f;
    // Lake basin downstream.
    const f32 lx = x - river_center_x(520.0f), lz = (z - 520.0f) * 0.7f;
    const f32 lake_d = std::sqrt(lx * lx + lz * lz);
    const f32 lake = 1.0f - smoothstep(60.0f, 160.0f, lake_d);
    h = lerp(h, 15.5f + floor_bumps * 0.3f, lake);
    return h;
}

// Meadow: a broad, gently rolling plain (slopes under ~8 deg near the centre) with shallow swales, a few low knolls
// and a rim of higher, smoother hills towards the map edge so the horizon is not a flat line.
f32 TerrainField::plains_height(f32 x, f32 z) const {
    const u32 seed = desc_.seed;
    const f32 wx = x + 40.0f * fbm(x / 500.0f, z / 500.0f, seed + 7, 3);
    const f32 wz = z + 40.0f * fbm(x / 500.0f + 4.3f, z / 500.0f + 2.1f, seed + 9, 3);
    const f32 rolling = fbm(wx / 320.0f, wz / 320.0f, seed + 11, 4) * 9.0f;
    const f32 swells = fbm(wx / 110.0f, wz / 110.0f, seed + 15, 3) * 2.2f;
    const f32 knolls = std::pow(max(fbm(wx / 180.0f + 9.1f, wz / 180.0f, seed + 27, 3), 0.0f), 2.0f) * 10.0f;
    const f32 r = std::sqrt(x * x + z * z);
    const f32 rim =
        smoothstep(420.0f, 980.0f, r) * (35.0f + 45.0f * (0.5f + 0.5f * fbm(wx / 260.0f, wz / 260.0f, seed + 3, 4)));
    const f32 bumps = fbm(x / 14.0f, z / 14.0f, seed + 13, 3) * 0.18f;
    // Keep the spawn area (r < 60 m) nearly flat.
    const f32 calm = smoothstep(20.0f, 90.0f, r);
    return 12.0f + (rolling + swells + knolls) * lerp(0.35f, 1.0f, calm) + rim + bumps;
}

// Shoreline of the cove (world z where the beach meets the still sea): bowed inland at the centre, two rocky
// headlands reaching out to sea at |x| ~ 450 m.
f32 TerrainField::coast_shore_z(f32 x) const {
    const f32 u = x / 400.0f;
    const f32 ax = std::fabs(x);
    return -70.0f * u * u - 260.0f * smoothstep(320.0f, 560.0f, ax) + 10.0f * fbm(x / 140.0f, 0.5f, desc_.seed + 5, 3);
}

f32 TerrainField::coast_shore_z_dist(f32 x, f32 z) const { return z - coast_shore_z(x); }

f32 TerrainField::coast_height(f32 x, f32 z) const {
    const u32 seed = desc_.seed;
    const f32 sea = desc_.sea_level;
    // Gentle domain warp so the beach and dunes are not ruler-straight.
    const f32 wx = x + 18.0f * fbm(x / 260.0f, z / 260.0f, seed + 7, 3);
    const f32 wz = z + 18.0f * fbm(x / 260.0f + 3.1f, z / 260.0f + 8.4f, seed + 9, 3);
    const f32 s = wz - coast_shore_z(wx); // > 0 on land, < 0 at sea
    const f32 ax = std::fabs(wx);
    const f32 headland = smoothstep(300.0f, 470.0f, ax);
    f32 h;
    if (s >= 0.0f) {
        // Beach face (~2.6 deg) up to the berm, a flat backshore, dunes, then the rising hinterland.
        const f32 face = min(s, 35.0f) * 0.045f;
        const f32 back = smoothstep(35.0f, 75.0f, s) * 0.8f;
        const f32 dunes =
            smoothstep(60.0f, 120.0f, s) * (2.5f + 4.0f * (0.5f + 0.5f * fbm(wx / 55.0f, wz / 40.0f, seed + 3, 4)));
        const f32 inland = max(s - 150.0f, 0.0f);
        const f32 hills = std::pow(inland / 600.0f, 1.3f) * 90.0f +
                          fbm(wx / 240.0f, wz / 240.0f, seed + 11, 5) * 22.0f * smoothstep(150.0f, 400.0f, s);
        h = face + back + dunes + hills;
    } else {
        const f32 d = -s;
        // Foreshore, a longshore sand bar, the shelf and a steeper drop to deep water.
        h = -min(d, 40.0f) * 0.04f - max(d - 40.0f, 0.0f) * 0.022f - max(d - 320.0f, 0.0f) * 0.045f;
        h += 0.9f * std::exp(-((d - 78.0f) / 20.0f) * ((d - 78.0f) / 20.0f)) * (1.0f - headland);
        h = max(h, -42.0f);
    }
    // Rocky headlands: cliffs rising steeply from the waterline, reefs below it.
    if (headland > 0.0f) {
        const f32 r = ridged(wx / 160.0f, wz / 160.0f, seed + 17, 5);
        const f32 cliff = smoothstep(-6.0f, 22.0f, s) * (22.0f + 38.0f * r);
        const f32 reef = (1.0f - smoothstep(0.0f, 70.0f, -s)) * 2.5f * r;
        h += headland * (s > 0.0f ? cliff : reef + cliff);
    }
    // Small undulations: wind ripples are in the textures, this is the metre scale.
    h += fbm(x / 9.0f, z / 9.0f, seed + 13, 3) * 0.08f +
         fbm(x / 40.0f, z / 40.0f, seed + 19, 3) * 0.25f * smoothstep(-5.0f, 5.0f, s);
    return sea + h;
}

void TerrainField::generate() {
    const u32 n = desc_.resolution;
    const f32 half = world_size() * 0.5f;
    heights_.resize(usize(n) * n);
    mask_.resize(usize(n / 2) * (n / 2) * 4);
    const u32 seed = desc_.seed;
    const bool coast = desc_.kind == TerrainKind::Coast;
    const bool plains = desc_.kind == TerrainKind::Plains;
    auto height_fn = [&](f32 x, f32 z) {
        return coast ? coast_height(x, z) : plains ? plains_height(x, z) : valley_height(x, z);
    };
    // Parallel over rows on the job system: every sample is independent, so the result does not depend on the thread
    // count.
    global_jobs().parallel_for(n, 8, [&](u32 y0, u32 y1) {
        for (u32 y = y0; y < y1; ++y)
            for (u32 x = 0; x < n; ++x) {
                const f32 wx = (f32(x) + 0.5f) * desc_.texel_size - half;
                const f32 wz = (f32(y) + 0.5f) * desc_.texel_size - half;
                heights_[usize(y) * n + x] = height_fn(wx, wz);
            }
    });
    min_h_ = 1e30f;
    max_h_ = -1e30f;
    for (f32 h : heights_) {
        min_h_ = min(min_h_, h);
        max_h_ = max(max_h_, h);
    }
    // Mask (half resolution): r = wetness (river/lake proximity), g = patchiness, b = path, a = unused.
    const u32 m = n / 2;
    global_jobs().parallel_for(m, 4, [&](u32 y0, u32 y1) {
        for (u32 y = y0; y < y1; ++y)
            for (u32 x = 0; x < m; ++x) {
                const f32 wx = (f32(x) + 0.5f) * desc_.texel_size * 2.0f - half;
                const f32 wz = (f32(y) + 0.5f) * desc_.texel_size * 2.0f - half;
                const f32 h = height_at(wx, wz);
                f32 wet;
                if (coast) {
                    // Wet sand below the swash limit (~0.9 m above the still sea); the sea bed is fully wet.
                    wet = 1.0f - smoothstep(desc_.sea_level + 0.15f, desc_.sea_level + 0.9f, h);
                } else if (plains) {
                    wet = 0.0f; // well-drained grassland
                } else {
                    const f32 d = std::fabs(river_distance(wx, wz));
                    wet = max(1.0f - smoothstep(4.0f, 22.0f, d), 1.0f - smoothstep(15.6f, 17.5f, h));
                }
                const f32 patch = 0.5f + 0.5f * fbm(wx / 9.0f, wz / 9.0f, seed + 21, 4);
                u8* p = &mask_[(usize(y) * m + x) * 4];
                p[0] = static_cast<u8>(saturate(wet) * 255.0f);
                p[1] = static_cast<u8>(saturate(patch) * 255.0f);
                // b: vegetation cover (coast: scrub and grass behind the dunes).
                f32 cover = 0.0f;
                if (coast) {
                    const f32 land = coast_shore_z_dist(wx, wz);
                    cover = smoothstep(90.0f, 170.0f, land + 40.0f * fbm(wx / 80.0f, wz / 80.0f, seed + 23, 3));
                    cover = max(cover, smoothstep(9.0f, 16.0f, h - desc_.sea_level)); // headland tops and hills
                }
                p[2] = static_cast<u8>(saturate(cover) * 255.0f);
                p[3] = 255;
            }
    });
    if (coast) build_shore_distance();
}

void TerrainField::build_shore_distance() {
    const u32 m = desc_.resolution / 2;
    const f32 texel = desc_.texel_size * 2.0f, half = world_size() * 0.5f;
    std::vector<u8> land(usize(m) * m), sea(usize(m) * m);
    for (u32 y = 0; y < m; ++y)
        for (u32 x = 0; x < m; ++x) {
            const f32 h = height_at((f32(x) + 0.5f) * texel - half, (f32(y) + 0.5f) * texel - half);
            land[usize(y) * m + x] = h > desc_.sea_level ? 1 : 0;
            sea[usize(y) * m + x] = h > desc_.sea_level ? 0 : 1;
        }
    const std::vector<f32> to_land = edt_2d(land, m), to_sea = edt_2d(sea, m);
    shore_.resize(usize(m) * m);
    global_jobs().parallel_for(m, 16, [&](u32 y0, u32 y1) {
        for (usize i = usize(y0) * m; i < usize(y1) * m; ++i)
            shore_[i] = (land[i] ? -(to_sea[i] - 0.5f) : (to_land[i] - 0.5f)) * texel;
    });
    // Two box passes: the medial-axis kinks of the distance field would show up as kinks in the surf.
    std::vector<f32> tmp(shore_.size());
    for (int pass = 0; pass < 2; ++pass) {
        global_jobs().parallel_for(m, 16, [&](u32 y0, u32 y1) {
            for (u32 y = y0; y < y1; ++y)
                for (u32 x = 0; x < m; ++x) {
                    f32 acc = 0;
                    for (i32 k = -2; k <= 2; ++k) acc += shore_[usize(y) * m + u32(clamp(i32(x) + k, 0, i32(m) - 1))];
                    tmp[usize(y) * m + x] = acc / 5.0f;
                }
        });
        global_jobs().parallel_for(m, 16, [&](u32 y0, u32 y1) {
            for (u32 y = y0; y < y1; ++y)
                for (u32 x = 0; x < m; ++x) {
                    f32 acc = 0;
                    for (i32 k = -2; k <= 2; ++k) acc += tmp[usize(u32(clamp(i32(y) + k, 0, i32(m) - 1))) * m + x];
                    shore_[usize(y) * m + x] = acc / 5.0f;
                }
        });
    }
}

f32 TerrainField::shore_distance(f32 x, f32 z) const {
    if (shore_.empty()) return 1e4f;
    const u32 m = desc_.resolution / 2;
    const f32 texel = desc_.texel_size * 2.0f, half = world_size() * 0.5f;
    const f32 fx = clamp((x + half) / texel - 0.5f, 0.0f, f32(m - 1));
    const f32 fz = clamp((z + half) / texel - 0.5f, 0.0f, f32(m - 1));
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = min(x0 + 1, m - 1), z1 = min(z0 + 1, m - 1);
    const f32 tx = fx - f32(x0), tz = fz - f32(z0);
    const f32 a = lerp(shore_[usize(z0) * m + x0], shore_[usize(z0) * m + x1], tx);
    const f32 b = lerp(shore_[usize(z1) * m + x0], shore_[usize(z1) * m + x1], tx);
    return lerp(a, b, tz);
}

f32 TerrainField::height_at(f32 x, f32 z) const {
    const u32 n = desc_.resolution;
    const f32 half = world_size() * 0.5f;
    const f32 fx = clamp((x + half) / desc_.texel_size - 0.5f, 0.0f, f32(n - 1));
    const f32 fz = clamp((z + half) / desc_.texel_size - 0.5f, 0.0f, f32(n - 1));
    const u32 x0 = static_cast<u32>(fx), z0 = static_cast<u32>(fz);
    const u32 x1 = min(x0 + 1, n - 1), z1 = min(z0 + 1, n - 1);
    const f32 tx = fx - f32(x0), tz = fz - f32(z0);
    const f32 a = lerp(heights_[usize(z0) * n + x0], heights_[usize(z0) * n + x1], tx);
    const f32 b = lerp(heights_[usize(z1) * n + x0], heights_[usize(z1) * n + x1], tx);
    return lerp(a, b, tz);
}

vec3 TerrainField::normal_at(f32 x, f32 z) const {
    const f32 e = desc_.texel_size;
    const f32 hx = height_at(x + e, z) - height_at(x - e, z);
    const f32 hz = height_at(x, z + e) - height_at(x, z - e);
    return normalize(vec3{-hx, 2.0f * e, -hz});
}

vec2 TerrainField::sample_origin() const {
    const f32 o = desc_.texel_size * 0.5f - world_size() * 0.5f;
    return {o, o};
}

bool TerrainField::raycast(vec3 origin, vec3 dir, f32 max_distance, f32& t_out) const {
    const f32 len = length(dir);
    if (len <= 0.0f || max_distance <= 0.0f || heights_.empty()) return false;
    const vec3 d = dir / len;
    const f32 half = world_size() * 0.5f;
    auto inside = [&](vec3 p) { return std::fabs(p.x) <= half && std::fabs(p.z) <= half; };
    auto above = [&](vec3 p) { return p.y - height_at(p.x, p.z); };
    // March with a step that shrinks near the surface, then bisect the bracketing interval.
    const f32 min_step = desc_.texel_size * 0.25f;
    f32 t = 0.0f, prev_t = 0.0f;
    vec3 p = origin;
    bool prev_inside = inside(p);
    f32 prev_h = prev_inside ? above(p) : 1e30f;
    if (prev_inside && prev_h <= 0.0f) {
        t_out = 0.0f;
        return true;
    }
    while (t < max_distance) {
        const f32 step = prev_inside ? clamp(prev_h * 0.3f, min_step, 4.0f) : 4.0f;
        t = min(t + step, max_distance);
        p = origin + d * t;
        const bool in = inside(p);
        const f32 h = in ? above(p) : 1e30f;
        if (in && h <= 0.0f) {
            f32 lo = prev_t, hi = t;
            for (int i = 0; i < 24; ++i) {
                const f32 mid = 0.5f * (lo + hi);
                const vec3 q = origin + d * mid;
                (inside(q) && above(q) <= 0.0f ? hi : lo) = mid;
            }
            t_out = hi;
            return true;
        }
        prev_t = t;
        prev_h = h;
        prev_inside = in;
        if (t >= max_distance) break;
    }
    return false;
}

} // namespace tefra::render
