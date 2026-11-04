#include "server/server.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <iostream>
#include <system_error>

#include "server/connection.h"
#include "server/net.h"

namespace miniredis {

namespace {

std::pair<UniqueFd, UniqueFd> make_pipe() {
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) < 0) {
        throw std::system_error(errno, std::generic_category(), "pipe");
    }
    return {UniqueFd(fds[0]), UniqueFd(fds[1])};
}

}  // namespace

Server::Server(const ServerOptions& options, CommandDispatcher& dispatcher)
    : dispatcher_(dispatcher),
      listener_(listen_tcp(options.bind_address, options.port)),
      port_(local_port(listener_)),
      pool_(options.threads) {
    auto [reader, writer] = make_pipe();
    wake_reader_ = std::move(reader);
    wake_writer_ = std::move(writer);
}

void Server::run() {
    // accept() cannot be interrupted portably from another thread, so the loop polls the
    // listener together with a self-pipe that stop() writes to.
    std::array<pollfd, 2> fds{{{listener_.get(), POLLIN, 0}, {wake_reader_.get(), POLLIN, 0}}};
    while (!stop_requested_) {
        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "poll");
        }
        if (fds[1].revents != 0) {
            break;
        }
        if ((fds[0].revents & POLLIN) != 0) {
            UniqueFd client = accept_client(listener_);
            if (client) {
                pool_.submit(
                    [this, client = std::move(client)]() mutable { serve(std::move(client)); });
            }
        }
    }
    stop();
    pool_.shutdown();
}

void Server::stop() {
    if (stop_requested_.exchange(true)) {
        return;
    }
    const char byte = 0;
    [[maybe_unused]] const ssize_t written = ::write(wake_writer_.get(), &byte, 1);

    // shutdown() wakes workers blocked in recv(); the fds are closed by their owners.
    std::lock_guard lock(clients_mutex_);
    stopping_ = true;
    for (int fd : clients_) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void Server::serve(UniqueFd client) {
    const int fd = client.get();
    if (!track(fd)) {
        return;
    }
    try {
        Connection(fd, dispatcher_).serve();
    } catch (const std::exception& e) {
        std::cerr << "connection error: " << e.what() << '\n';
    }
    untrack(fd);
}  // `client` closes the fd only after it is untracked.

bool Server::track(int fd) {
    std::lock_guard lock(clients_mutex_);
    if (stopping_) {
        return false;
    }
    clients_.insert(fd);
    return true;
}

void Server::untrack(int fd) {
    std::lock_guard lock(clients_mutex_);
    clients_.erase(fd);
}

}  // namespace miniredis
