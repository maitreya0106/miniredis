#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "util/unique_fd.h"

namespace miniredis {

// IPv4 only; `address` must be a dotted-quad literal such as 127.0.0.1 or 0.0.0.0.
[[nodiscard]] UniqueFd listen_tcp(const std::string& address, std::uint16_t port,
                                  int backlog = 511);
[[nodiscard]] UniqueFd connect_tcp(const std::string& address, std::uint16_t port);

// Returns an invalid fd on transient failures (e.g. the peer reset before we accepted).
[[nodiscard]] UniqueFd accept_client(const UniqueFd& listener);

std::uint16_t local_port(const UniqueFd& socket);

// Loops over partial writes. Returns false once the peer is gone; never raises SIGPIPE.
[[nodiscard]] bool send_all(int fd, std::string_view data);

}  // namespace miniredis
