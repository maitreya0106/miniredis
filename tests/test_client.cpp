#include "test_client.h"

#include <sys/socket.h>
#include <sys/time.h>

#include <array>
#include <cerrno>
#include <stdexcept>

#include "server/net.h"

namespace miniredis::testing {

TestClient::TestClient(std::uint16_t port) : socket_(connect_tcp("127.0.0.1", port)) {
    timeval timeout{5, 0};
    ::setsockopt(socket_.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

void TestClient::send(std::string_view data) {
    if (!send_all(socket_.get(), data)) {
        throw std::runtime_error("send failed");
    }
}

std::string TestClient::read_reply() {
    for (;;) {
        if (auto length = reply_length(0)) {
            std::string reply = buffer_.substr(0, *length);
            buffer_.erase(0, *length);
            return reply;
        }
        if (!fill()) {
            throw std::runtime_error("connection closed while waiting for a reply");
        }
    }
}

bool TestClient::closed_by_peer() {
    return buffer_.empty() && !fill();
}

bool TestClient::fill() {
    std::array<char, 4096> chunk{};
    ssize_t received = 0;
    do {
        received = ::recv(socket_.get(), chunk.data(), chunk.size(), 0);
    } while (received < 0 && errno == EINTR);
    if (received <= 0) {
        return false;
    }
    buffer_.append(chunk.data(), static_cast<std::size_t>(received));
    return true;
}

std::optional<std::size_t> TestClient::reply_length(std::size_t offset) const {
    const std::size_t line_end = buffer_.find("\r\n", offset);
    if (line_end == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t header_end = line_end + 2;
    const char type = buffer_[offset];
    if (type != '$' && type != '*') {
        return header_end - offset;
    }

    const long count = std::stol(buffer_.substr(offset + 1, line_end - offset - 1));
    if (count < 0) {
        return header_end - offset;
    }
    if (type == '$') {
        const std::size_t end = header_end + static_cast<std::size_t>(count) + 2;
        return end <= buffer_.size() ? std::optional(end - offset) : std::nullopt;
    }
    std::size_t position = header_end;
    for (long i = 0; i < count; ++i) {
        auto element = reply_length(position);
        if (!element) {
            return std::nullopt;
        }
        position += *element;
    }
    return position - offset;
}

}  // namespace miniredis::testing
