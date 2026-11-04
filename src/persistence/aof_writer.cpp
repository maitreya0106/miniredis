#include "persistence/aof_writer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <system_error>

#include "protocol/resp.h"

namespace miniredis {

namespace {

// Durably flushes file data. Plain fsync() on macOS only reaches the drive's cache.
int sync_to_disk(int fd) {
#ifdef F_FULLFSYNC
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return 0;
    }
    return ::fsync(fd);
#elif defined(__linux__)
    return ::fdatasync(fd);
#else
    return ::fsync(fd);
#endif
}

// Once a write may have only partly reached the log, carrying on would leave a hole in the middle
// of the file. Dying instead leaves at worst a torn final record, which startup truncates.
[[noreturn]] void fail_stop(const char* operation) {
    std::cerr << "miniredis: AOF " << operation << " failed: " << std::strerror(errno)
              << "; exiting to keep the log consistent\n";
    std::abort();
}

void write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t written = ::write(fd, data.data(), data.size());
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail_stop("write");
        }
        data.remove_prefix(static_cast<std::size_t>(written));
    }
}

// A newly created file is only durable once its directory entry is too.
void sync_parent_directory(const std::string& path) {
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    const UniqueFd dir(::open(parent.c_str(), O_RDONLY));
    if (dir) {
        ::fsync(dir.get());
    }
}

UniqueFd open_for_append(const std::string& path) {
    const bool existed = std::filesystem::exists(path);
    UniqueFd fd(::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644));
    if (!fd) {
        throw std::system_error(errno, std::generic_category(), "open " + path);
    }
    if (!existed) {
        sync_parent_directory(path);
    }
    return fd;
}

}  // namespace

AofWriter::AofWriter(const std::string& path, FsyncPolicy policy)
    : fd_(open_for_append(path)), policy_(policy) {
    if (policy_ == FsyncPolicy::EverySecond) {
        syncer_ = std::thread([this] { sync_periodically(); });
    }
}

AofWriter::~AofWriter() {
    if (syncer_.joinable()) {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        stop_requested_.notify_one();
        syncer_.join();
    }
    if (unsynced_) {
        sync_to_disk(fd_.get());
    }
}

void AofWriter::append(const Command& command) {
    record_.clear();
    append_command(record_, command);
    write_all(fd_.get(), record_);

    if (policy_ == FsyncPolicy::Always) {
        if (sync_to_disk(fd_.get()) < 0) {
            fail_stop("fsync");
        }
    } else {
        unsynced_.store(true, std::memory_order_release);
    }
}

void AofWriter::sync_periodically() {
    std::unique_lock lock(mutex_);
    while (!stop_requested_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_; })) {
        // fsync runs concurrently with appends on purpose: the kernel serialises them, and
        // clients never wait for the disk under this policy.
        if (unsynced_.exchange(false, std::memory_order_acq_rel) && sync_to_disk(fd_.get()) < 0) {
            fail_stop("fsync");
        }
    }
}

}  // namespace miniredis
