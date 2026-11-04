#pragma once

#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace miniredis {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Accepts move-only callables, which std::function cannot hold.
    template <typename F>
    std::future<void> submit(F&& task) {
        std::packaged_task<void()> packaged(std::forward<F>(task));
        auto future = packaged.get_future();
        enqueue(std::move(packaged));
        return future;
    }

    // Stops accepting work, runs everything already queued, then joins the workers.
    void shutdown();

    std::size_t thread_count() const noexcept { return workers_.size(); }

private:
    void enqueue(std::packaged_task<void()> task);
    void worker_loop();

    std::mutex mutex_;
    std::condition_variable work_available_;
    std::queue<std::packaged_task<void()>> tasks_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace miniredis
