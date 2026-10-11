// Chunk decompressors of the binary format: an LZ4 block decoder written from the public LZ4 block format
// description, and the vendored zstd single-file decoder (third_party/zstd, BSD).
#include "tefra/rbx/document.h"

#include <zstd.h>

#include <cstring>

namespace tefra::rbx {

// LZ4 block: a sequence of (token, literals, offset, match) groups. Token high nibble = literal count, low nibble =
// match length - 4; a nibble of 15 continues in following bytes (each adds 0-255, 255 = keep going). The offset is
// a little-endian u16 back-reference into the output (overlapping copies are allowed). The last sequence has only
// literals. Every read and write is bounds-checked; the output must come out exactly dst_size bytes long.
bool lz4_decompress_block(const u8* src, usize src_size, u8* dst, usize dst_size) {
    if (!src || (!dst && dst_size)) return false;
    usize ip = 0, op = 0;
    auto read_length = [&](usize& len) -> bool {
        for (;;) {
            if (ip >= src_size) return false;
            const u8 b = src[ip++];
            len += b;
            if (len > dst_size) return false; // can never fit: reject early (also prevents overflow)
            if (b != 255) return true;
        }
    };
    while (ip < src_size) {
        const u8 token = src[ip++];
        usize lit = token >> 4;
        if (lit == 15 && !read_length(lit)) return false;
        if (lit > src_size - ip || lit > dst_size - op) return false;
        std::memcpy(dst + op, src + ip, lit);
        ip += lit;
        op += lit;
        if (ip == src_size) break; // last sequence: literals only
        if (src_size - ip < 2) return false;
        const usize offset = usize(src[ip]) | (usize(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) return false;
        usize match = token & 15u;
        if (match == 15 && !read_length(match)) return false;
        match += 4;
        if (match > dst_size - op) return false;
        const u8* from = dst + (op - offset);
        for (usize k = 0; k < match; ++k) dst[op + k] = from[k]; // byte by byte: overlap repeats the pattern
        op += match;
    }
    return op == dst_size;
}

bool zstd_decompress_frame(const u8* src, usize src_size, u8* dst, usize dst_size) {
    if (!src || (!dst && dst_size)) return false;
    const size_t r = ZSTD_decompress(dst, dst_size, src, src_size);
    return !ZSTD_isError(r) && r == dst_size;
}

} // namespace tefra::rbx
