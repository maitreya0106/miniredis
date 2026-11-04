#include "server_process.h"

#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <stdexcept>

extern char** environ;  // NOLINT(readability-redundant-declaration)

namespace miniredis::testing {

namespace {

constexpr int kStartupTimeoutMs = 10'000;

}  // namespace

ServerProcess::ServerProcess(const std::vector<std::string>& extra_args) {
    std::array<int, 2> pipe_fds{};
    if (::pipe(pipe_fds.data()) < 0) {
        throw std::runtime_error("pipe failed");
    }
    stdout_ = UniqueFd(pipe_fds[0]);
    const UniqueFd write_end(pipe_fds[1]);

    std::vector<std::string> args{MINIREDIS_BINARY, "--port", "0"};
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, write_end.get(), STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, stdout_.get());
    const int status =
        posix_spawn(&pid_, MINIREDIS_BINARY, &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) {
        throw std::runtime_error("posix_spawn failed");
    }
    wait_for_port();
}

ServerProcess::~ServerProcess() {
    if (pid_ > 0) {
        kill_hard();
    }
}

void ServerProcess::kill_hard() {
    ::kill(pid_, SIGKILL);
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
}

void ServerProcess::wait_for_port() {
    const std::string marker = "listening on ";
    for (;;) {
        const std::size_t found = output_.find(marker);
        if (found != std::string::npos) {
            const std::size_t line_end = output_.find('\n', found);
            if (line_end != std::string::npos) {
                const std::size_t colon = output_.rfind(':', line_end);
                port_ = static_cast<std::uint16_t>(
                    std::stoul(output_.substr(colon + 1, line_end - colon - 1)));
                return;
            }
        }

        pollfd readable{stdout_.get(), POLLIN, 0};
        if (::poll(&readable, 1, kStartupTimeoutMs) <= 0) {
            throw std::runtime_error("server did not start; output so far: " + output_);
        }
        std::array<char, 512> chunk{};
        const ssize_t received = ::read(stdout_.get(), chunk.data(), chunk.size());
        if (received <= 0) {
            throw std::runtime_error("server exited during startup; output: " + output_);
        }
        output_.append(chunk.data(), static_cast<std::size_t>(received));
    }
}

}  // namespace miniredis::testing
