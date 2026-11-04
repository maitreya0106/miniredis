#include "protocol/reply.h"

#include <type_traits>

namespace miniredis {

namespace {

void append_line(std::string& out, char prefix, const std::string& body) {
    out += prefix;
    out += body;
    out += "\r\n";
}

void append_bulk(std::string& out, const std::string& value) {
    append_line(out, '$', std::to_string(value.size()));
    out += value;
    out += "\r\n";
}

}  // namespace

void append_reply(std::string& out, const Reply& reply) {
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, SimpleString>) {
                append_line(out, '+', value.value);
            } else if constexpr (std::is_same_v<T, ErrorReply>) {
                append_line(out, '-', value.message);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                append_line(out, ':', std::to_string(value));
            } else if constexpr (std::is_same_v<T, BulkString>) {
                append_bulk(out, value.value);
            } else if constexpr (std::is_same_v<T, ArrayReply>) {
                append_line(out, '*', std::to_string(value.items.size()));
                for (const auto& item : value.items) {
                    append_bulk(out, item.value);
                }
            } else {
                out += "$-1\r\n";
            }
        },
        reply);
}

}  // namespace miniredis
