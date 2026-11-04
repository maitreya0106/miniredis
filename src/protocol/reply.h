#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace miniredis {

struct SimpleString {
    std::string value;
};

struct ErrorReply {
    std::string message;
};

struct BulkString {
    std::string value;
};

struct NilReply {};

struct ArrayReply {
    std::vector<BulkString> items;
};

using Reply =
    std::variant<SimpleString, ErrorReply, std::int64_t, BulkString, NilReply, ArrayReply>;

inline Reply ok() {
    return SimpleString{"OK"};
}
inline Reply error(std::string message) {
    return ErrorReply{std::move(message)};
}

// Replies use RESP framing even for inline requests, as Redis does; simple types stay one line.
void append_reply(std::string& out, const Reply& reply);

}  // namespace miniredis
