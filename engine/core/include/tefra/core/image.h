#pragma once

#include "tefra/core/types.h"

#include <vector>

namespace tefra {

struct ImageRGBA8 {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels; // width * height * 4, row-major, top row first
};

struct ImageRGBA32F {
    u32 width = 0;
    u32 height = 0;
    std::vector<f32> pixels; // width * height * 4, linear
};

bool image_write_png(const char* path, const ImageRGBA8& img);
// PNG or JPEG from file / memory, expanded to RGBA8.
bool image_read_png(const char* path, ImageRGBA8& out);
bool image_read(const char* path, ImageRGBA8& out);
bool image_read_memory(const u8* data, usize size, ImageRGBA8& out);
// Radiance .hdr, expanded to RGBA32F.
bool image_read_hdr(const char* path, ImageRGBA32F& out);

struct ImageDiff {
    bool size_match = false;
    f64 rmse = 0.0;          // root mean square error over RGB channels, 0..255
    u32 max_abs = 0;         // largest per-channel difference
    f64 fraction_over = 0.0; // fraction of pixels with any RGB channel differing by more than threshold
};

// Golden-image comparison (alpha ignored).
ImageDiff image_compare(const ImageRGBA8& a, const ImageRGBA8& b, u32 threshold);

} // namespace tefra
