#include "tefra/core/jobs.h"
#include "tefra/core/assert.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace tefra {

struct Job {
    JobFn fn;
    void* user;
    TaskGroup* group;
};

struct JobSystem::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::vector<std::thread> workers;
    bool stop = false;

    bool try_pop(Job& job) {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return false;
        job = queue.front();
        queue.pop_front();
        return true;
    }

    static void execute(const Job& job) {
        job.fn(job.user);
        job.group->pending.fetch_sub(1, std::memory_order_acq_rel);
    }

    void worker_loop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return stop || !queue.empty(); });
                if (stop && queue.empty()) return;
                job = queue.front();
                queue.pop_front();
            }
            execute(job);
        }
    }
};

JobSystem::JobSystem(u32 worker_count) : impl_(new Impl) {
    if (worker_count == 0) {
        const u32 hw = std::thread::hardware_concurrency();
        worker_count = hw > 1 ? hw - 1 : 1;
    }
    impl_->workers.reserve(worker_count);
    for (u32 i = 0; i < worker_count; ++i) impl_->workers.emplace_back([this] { impl_->worker_loop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stop = true;
    }
    impl_->cv.notify_all();
    for (auto& t : impl_->workers) t.join();
    delete impl_;
}

u32 JobSystem::worker_count() const { return static_cast<u32>(impl_->workers.size()); }

void JobSystem::run(TaskGroup& group, JobFn fn, void* user) {
    group.pending.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->queue.push_back({fn, user, &group});
    }
    impl_->cv.notify_one();
}

void JobSystem::wait(TaskGroup& group) {
    while (!group.done()) {
        Job job;
        if (impl_->try_pop(job)) {
            Impl::execute(job);
        } else {
            std::this_thread::yield();
        }
    }
}

} // namespace tefra
