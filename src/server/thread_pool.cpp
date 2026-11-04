#include "server/thread_pool.h"

#include <stdexcept>

namespace miniredis {

ThreadPool::ThreadPool(std::size_t thread_count) {
    if (thread_count == 0) {
        throw std::invalid_argument("thread pool needs at least one thread");
    }
    workers_.reserve(thread_count);
    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        shutdown();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    work_available_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::packaged_task<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("submit on a stopped thread pool");
        }
        tasks_.push(std::move(task));
    }
    work_available_.notify_one();
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::packaged_task<void()> task;
        {
            std::unique_lock lock(mutex_);
            work_available_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

}  // namespace miniredis
