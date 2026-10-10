#pragma once

#include <cstddef>
#include <cstdint>

namespace tefra {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;
using usize = std::size_t;

template <typename T, usize N> constexpr usize count_of(const T (&)[N]) { return N; }

} // namespace tefra

#if defined(__MINGW32__)
#define TEFRA_PRINTF(fmt_idx, args_idx) __attribute__((format(gnu_printf, fmt_idx, args_idx)))
#elif defined(__GNUC__) || defined(__clang__)
#define TEFRA_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
#define TEFRA_PRINTF(fmt_idx, args_idx)
#endif

#if defined(_WIN32)
#define TEFRA_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#define TEFRA_PLATFORM_LINUX 1
#endif
