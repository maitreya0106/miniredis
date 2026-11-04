#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>

#include "server/command_dispatcher.h"
#include "server/thread_pool.h"
#include "util/unique_fd.h"

namespace miniredis {

struct ServerOptions {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 6380;
    std::size_t threads = 16;
};

// Each client occupies one pool worker for its whole lifetime, so at most `threads` clients are
// served concurrently; further connections wait in the pool queue until a worker frees up.
class Server {
public:
    Server(const ServerOptions& options, CommandDispatcher& dispatcher);

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;
    ~Server() = default;

    std::uint16_t port() const noexcept { return port_; }

    // Accepts clients until stop() is called, then waits for every connection to finish.
    void run();

    // Safe to call from any thread, any number of times.
    void stop();

private:
    void serve(UniqueFd client);
    bool track(int fd);
    void untrack(int fd);

    CommandDispatcher& dispatcher_;
    UniqueFd listener_;
    std::uint16_t port_;
    UniqueFd wake_reader_;
    UniqueFd wake_writer_;

    std::mutex clients_mutex_;
    std::unordered_set<int> clients_;
    bool stopping_ = false;
    std::atomic<bool> stop_requested_{false};

    ThreadPool pool_;
};

}  // namespace miniredis
