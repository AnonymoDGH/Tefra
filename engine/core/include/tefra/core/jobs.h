#pragma once

// Small job system sized for few cores (reference PC: 2C/4T). A waiting thread helps execute jobs,
// so nested waits never deadlock as long as jobs do not block on external events.

#include "tefra/core/types.h"

#include <atomic>

namespace tefra {

struct TaskGroup {
    std::atomic<u32> pending{0};
    bool done() const { return pending.load(std::memory_order_acquire) == 0; }
};

using JobFn = void (*)(void* user);

class JobSystem {
  public:
    // worker_count 0 = hardware threads - 1 (at least 1).
    explicit JobSystem(u32 worker_count = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    u32 worker_count() const;

    // Queue fn(user). The caller keeps `user` alive until wait(group) returns.
    void run(TaskGroup& group, JobFn fn, void* user);

    template <typename F> void run(TaskGroup& group, F& f) {
        run(group, [](void* p) { (*static_cast<F*>(p))(); }, &f);
    }

    // Blocks until the group is done, executing queued jobs meanwhile.
    void wait(TaskGroup& group);

    // Calls f(begin, end) over [0, count) in chunks of `grain`; returns when all chunks are done.
    template <typename F> void parallel_for(u32 count, u32 grain, F&& f) {
        if (count == 0) return;
        if (grain == 0) grain = 1;
        struct Ctx {
            F* fn;
            u32 count, grain;
            std::atomic<u32> next{0};
        } ctx{&f, count, grain};
        auto body = [](void* p) {
            Ctx& c = *static_cast<Ctx*>(p);
            for (;;) {
                const u32 begin = c.next.fetch_add(c.grain, std::memory_order_relaxed);
                if (begin >= c.count) break;
                const u32 end = begin + c.grain < c.count ? begin + c.grain : c.count;
                (*c.fn)(begin, end);
            }
        };
        TaskGroup group;
        const u32 chunks = (count + grain - 1) / grain;
        const u32 helpers = chunks - 1 < worker_count() ? chunks - 1 : worker_count();
        for (u32 i = 0; i < helpers; ++i) run(group, body, &ctx);
        body(&ctx);
        wait(group);
    }

  private:
    struct Impl;
    Impl* impl_;
};

// Process-wide job system (hardware threads - 1 workers; the calling thread helps while it waits). Used for the
// load-time work (terrain fields, glTF simplification); created on first use.
JobSystem& global_jobs();

} // namespace tefra
