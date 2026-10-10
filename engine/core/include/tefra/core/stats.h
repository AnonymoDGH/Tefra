#pragma once

#include "tefra/core/types.h"

#include <vector>

namespace tefra {

struct Summary {
    f64 mean = 0, min = 0, max = 0, p50 = 0, p95 = 0, p99 = 0;
    usize count = 0;
};

// Percentiles use the nearest-rank method on a sorted copy.
Summary summarize(const std::vector<f64>& samples);

} // namespace tefra
