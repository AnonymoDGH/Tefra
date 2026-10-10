#include "tefra/core/image.h"

#include <cmath>
#include <cstdlib>

#include "stb_image.h"
#include "stb_image_write.h"

namespace tefra {

bool image_write_png(const char* path, const ImageRGBA8& img) {
    if (img.width == 0 || img.height == 0 || img.pixels.size() != usize(img.width) * img.height * 4) return false;
    return stbi_write_png(path, static_cast<int>(img.width), static_cast<int>(img.height), 4, img.pixels.data(),
                          static_cast<int>(img.width * 4)) != 0;
}

bool image_read_png(const char* path, ImageRGBA8& out) {
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) return false;
    out.width = static_cast<u32>(w);
    out.height = static_cast<u32>(h);
    out.pixels.assign(data, data + usize(w) * usize(h) * 4);
    stbi_image_free(data);
    return true;
}

ImageDiff image_compare(const ImageRGBA8& a, const ImageRGBA8& b, u32 threshold) {
    ImageDiff d;
    d.size_match = a.width == b.width && a.height == b.height && a.pixels.size() == b.pixels.size();
    if (!d.size_match || a.pixels.empty()) return d;
    const usize count = usize(a.width) * a.height;
    f64 sum_sq = 0.0;
    usize over = 0;
    for (usize i = 0; i < count; ++i) {
        bool pixel_over = false;
        for (usize c = 0; c < 3; ++c) {
            const int diff = std::abs(int(a.pixels[i * 4 + c]) - int(b.pixels[i * 4 + c]));
            sum_sq += f64(diff) * f64(diff);
            if (u32(diff) > d.max_abs) d.max_abs = u32(diff);
            if (u32(diff) > threshold) pixel_over = true;
        }
        over += pixel_over ? 1 : 0;
    }
    d.rmse = std::sqrt(sum_sq / f64(count * 3));
    d.fraction_over = f64(over) / f64(count);
    return d;
}

} // namespace tefra
