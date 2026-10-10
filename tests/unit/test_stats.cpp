#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/stats.h"

using namespace tefra;

TEST_CASE("nearest-rank percentiles") {
    std::vector<f64> v;
    for (int i = 100; i >= 1; --i) v.push_back(i);
    const Summary s = summarize(v);
    CHECK(s.count == 100);
    CHECK(s.mean == doctest::Approx(50.5));
    CHECK(s.p50 == 50);
    CHECK(s.p95 == 95);
    CHECK(s.p99 == 99);
    CHECK(s.max == 100);
    CHECK(s.min == 1);
    CHECK(summarize({}).count == 0);
    CHECK(summarize({7.0}).p99 == 7.0);
}
