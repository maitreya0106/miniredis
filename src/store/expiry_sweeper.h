#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "store/store.h"

namespace miniredis {

// Reclaims expired keys nobody reads again; lazy expiry alone would let them leak memory.
class ExpirySweeper {
public:
    explicit ExpirySweeper(Store& store,
                           std::chrono::milliseconds interval = std::chrono::milliseconds(100));
    ~ExpirySweeper();

    ExpirySweeper(const ExpirySweeper&) = delete;
    ExpirySweeper& operator=(const ExpirySweeper&) = delete;
    ExpirySweeper(ExpirySweeper&&) = delete;
    ExpirySweeper& operator=(ExpirySweeper&&) = delete;

private:
    void run();

    Store& store_;
    std::chrono::milliseconds interval_;
    std::mutex mutex_;
    std::condition_variable stop_requested_;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace miniredis
