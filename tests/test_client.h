#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "util/unique_fd.h"

namespace miniredis::testing {

// Blocking client that returns each reply as its raw RESP bytes, e.g. "$1\r\nv\r\n".
class TestClient {
public:
    explicit TestClient(std::uint16_t port);

    void send(std::string_view data);
    std::string read_reply();
    std::string request(std::string_view line) {
        send(std::string(line) + "\r\n");
        return read_reply();
    }

    // True once the server has closed the connection.
    bool closed_by_peer();

private:
    bool fill();
    std::optional<std::size_t> reply_length(std::size_t offset) const;

    UniqueFd socket_;
    std::string buffer_;
};

}  // namespace miniredis::testing
