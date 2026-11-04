#include "store/expiry_sweeper.h"

namespace miniredis {

ExpirySweeper::ExpirySweeper(Store& store, std::chrono::milliseconds interval)
    : store_(store), interval_(interval), thread_([this] { run(); }) {}

ExpirySweeper::~ExpirySweeper() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    stop_requested_.notify_one();
    thread_.join();
}

void ExpirySweeper::run() {
    std::unique_lock lock(mutex_);
    // wait_for doubles as an interruptible sleep: the destructor wakes it immediately.
    while (!stop_requested_.wait_for(lock, interval_, [this] { return stopping_; })) {
        lock.unlock();
        store_.sweep_expired();
        lock.lock();
    }
}

}  // namespace miniredis
