#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/jobs.h"

#include <atomic>
#include <vector>

using namespace tefra;

TEST_CASE("parallel_for covers every index exactly once") {
    JobSystem jobs(3);
    std::vector<std::atomic<u32>> hits(10007);
    jobs.parallel_for(static_cast<u32>(hits.size()), 64, [&](u32 begin, u32 end) {
        for (u32 i = begin; i < end; ++i) hits[i].fetch_add(1, std::memory_order_relaxed);
    });
    bool all_once = true;
    for (auto& h : hits) all_once = all_once && h.load() == 1;
    CHECK(all_once);
}

TEST_CASE("task groups and nested waits do not deadlock") {
    JobSystem jobs(2);
    std::atomic<u32> counter{0};
    TaskGroup outer;
    auto inner_work = [&] {
        TaskGroup inner;
        auto leaf = [&] { counter.fetch_add(1); };
        for (int i = 0; i < 8; ++i) jobs.run(inner, leaf);
        jobs.wait(inner);
    };
    for (int i = 0; i < 8; ++i) jobs.run(outer, inner_work);
    jobs.wait(outer);
    CHECK(counter.load() == 64);
}

TEST_CASE("single worker still completes") {
    JobSystem jobs(1);
    u64 sum = 0;
    std::vector<u64> partial(4, 0);
    jobs.parallel_for(4, 1, [&](u32 b, u32 e) {
        for (u32 i = b; i < e; ++i) partial[i] = i + 1;
    });
    for (u64 v : partial) sum += v;
    CHECK(sum == 10);
}
