#include "server/connection.h"

#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <string_view>

#include "protocol/request_parser.h"
#include "server/net.h"

namespace miniredis {

void Connection::serve() {
    std::array<char, std::size_t{16} * 1024> chunk{};
    for (;;) {
        const ssize_t received = ::recv(socket_, chunk.data(), chunk.size(), 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            return;
        }
        input_.append(chunk.data(), static_cast<std::size_t>(received));

        const bool keep_open = process_input();
        // Pipelined requests get their replies in a single write.
        if (!output_.empty() && !send_all(socket_, output_)) {
            return;
        }
        output_.clear();
        if (!keep_open) {
            return;
        }
    }
}

bool Connection::process_input() {
    std::size_t offset = 0;
    bool keep_open = true;
    while (offset < input_.size()) {
        ParseResult result = parse_request(std::string_view(input_).substr(offset));
        if (result.status == ParseStatus::Incomplete) {
            break;
        }
        if (result.status == ParseStatus::Error) {
            append_reply(output_, error(result.error));
            keep_open = false;
            break;
        }
        offset += result.consumed;
        if (!result.command.empty()) {
            append_reply(output_, dispatcher_.execute(result.command));
        }
    }
    input_.erase(0, offset);
    return keep_open;
}

}  // namespace miniredis
