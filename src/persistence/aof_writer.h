#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "store/write_log.h"
#include "util/unique_fd.h"

namespace miniredis {

enum class FsyncPolicy {
    // fsync before the client gets its reply: no acknowledged write is ever lost.
    Always,
    // fsync from a background thread once a second: up to ~1s of writes lost on power failure.
    EverySecond,
};

class AofWriter final : public WriteLog {
public:
    AofWriter(const std::string& path, FsyncPolicy policy);
    ~AofWriter() override;

    AofWriter(const AofWriter&) = delete;
    AofWriter& operator=(const AofWriter&) = delete;
    AofWriter(AofWriter&&) = delete;
    AofWriter& operator=(AofWriter&&) = delete;

    void append(const Command& command) override;

private:
    void sync_periodically();

    UniqueFd fd_;
    FsyncPolicy policy_;
    std::string record_;
    std::atomic<bool> unsynced_{false};

    std::mutex mutex_;
    std::condition_variable stop_requested_;
    bool stopping_ = false;
    std::thread syncer_;
};

}  // namespace miniredis
