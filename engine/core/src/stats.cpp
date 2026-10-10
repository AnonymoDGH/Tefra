#include "tefra/core/stats.h"

#include <algorithm>
#include <cmath>

namespace tefra {

static f64 nearest_rank(const std::vector<f64>& sorted, f64 pct) {
    const usize n = sorted.size();
    usize rank = static_cast<usize>(std::ceil(pct / 100.0 * static_cast<f64>(n)));
    if (rank < 1) rank = 1;
    if (rank > n) rank = n;
    return sorted[rank - 1];
}

Summary summarize(const std::vector<f64>& samples) {
    Summary s;
    s.count = samples.size();
    if (samples.empty()) return s;
    std::vector<f64> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    f64 sum = 0.0;
    for (f64 v : sorted) sum += v;
    s.mean = sum / static_cast<f64>(sorted.size());
    s.min = sorted.front();
    s.max = sorted.back();
    s.p50 = nearest_rank(sorted, 50.0);
    s.p95 = nearest_rank(sorted, 95.0);
    s.p99 = nearest_rank(sorted, 99.0);
    return s;
}

} // namespace tefra
