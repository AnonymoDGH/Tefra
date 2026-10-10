#pragma once

#include "tefra/core/types.h"

namespace tefra {

// Monotonic clock.
u64 time_now_ns();
inline f64 time_now_s() { return static_cast<f64>(time_now_ns()) * 1e-9; }
inline f64 ns_to_ms(u64 ns) { return static_cast<f64>(ns) * 1e-6; }

void sleep_ms(u32 ms);

} // namespace tefra
