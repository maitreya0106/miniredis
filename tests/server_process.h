#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "util/unique_fd.h"

namespace miniredis::testing {

// Runs the real miniredis binary as a child process on a free port.
class ServerProcess {
public:
    explicit ServerProcess(const std::vector<std::string>& extra_args);
    ~ServerProcess();

    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;
    ServerProcess(ServerProcess&&) = delete;
    ServerProcess& operator=(ServerProcess&&) = delete;

    std::uint16_t port() const { return port_; }

    // SIGKILL: no destructors, no final fsync, exactly like a crash.
    void kill_hard();

    // Everything the server printed before announcing its port.
    const std::string& startup_output() const { return output_; }

private:
    void wait_for_port();

    pid_t pid_ = -1;
    UniqueFd stdout_;
    std::uint16_t port_ = 0;
    std::string output_;
};

}  // namespace miniredis::testing
