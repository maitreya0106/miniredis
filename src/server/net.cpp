#include "server/net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <cerrno>
#include <system_error>

namespace miniredis {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

[[noreturn]] void throw_errno(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

void set_option(int fd, int level, int name, int value) {
    if (::setsockopt(fd, level, name, &value, sizeof(value)) < 0) {
        throw_errno("setsockopt");
    }
}

// macOS has no MSG_NOSIGNAL, so the equivalent is set once per socket instead.
void disable_sigpipe([[maybe_unused]] int fd) {
#ifdef SO_NOSIGPIPE
    set_option(fd, SOL_SOCKET, SO_NOSIGPIPE, 1);
#endif
}

sockaddr_in make_address(const std::string& address, std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
        throw std::invalid_argument("not an IPv4 address: " + address);
    }
    return addr;
}

UniqueFd make_tcp_socket() {
    UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        throw_errno("socket");
    }
    return fd;
}

}  // namespace

UniqueFd listen_tcp(const std::string& address, std::uint16_t port, int backlog) {
    const sockaddr_in addr = make_address(address, port);
    UniqueFd fd = make_tcp_socket();
    set_option(fd.get(), SOL_SOCKET, SO_REUSEADDR, 1);
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw_errno("bind " + address + ":" + std::to_string(port));
    }
    if (::listen(fd.get(), backlog) < 0) {
        throw_errno("listen");
    }
    return fd;
}

UniqueFd connect_tcp(const std::string& address, std::uint16_t port) {
    const sockaddr_in addr = make_address(address, port);
    UniqueFd fd = make_tcp_socket();
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw_errno("connect " + address + ":" + std::to_string(port));
    }
    disable_sigpipe(fd.get());
    set_option(fd.get(), IPPROTO_TCP, TCP_NODELAY, 1);
    return fd;
}

UniqueFd accept_client(const UniqueFd& listener) {
    for (;;) {
        UniqueFd client(::accept(listener.get(), nullptr, nullptr));
        if (client) {
            disable_sigpipe(client.get());
            set_option(client.get(), IPPROTO_TCP, TCP_NODELAY, 1);
            return client;
        }
        if (errno != EINTR) {
            return UniqueFd{};
        }
    }
}

std::uint16_t local_port(const UniqueFd& socket) {
    sockaddr_in addr{};
    socklen_t length = sizeof(addr);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&addr), &length) < 0) {
        throw_errno("getsockname");
    }
    return ntohs(addr.sin_port);
}

bool send_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const ssize_t sent = ::send(fd, data.data(), data.size(), kSendFlags);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

}  // namespace miniredis
