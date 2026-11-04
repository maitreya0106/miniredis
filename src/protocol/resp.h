#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "protocol/command.h"
#include "protocol/parse_result.h"

namespace miniredis {

inline constexpr std::size_t kMaxBulkLength = std::size_t{512} * 1024 * 1024;
inline constexpr std::size_t kMaxArrayLength = std::size_t{1024} * 1024;

// Parses one request in RESP form: an array of bulk strings, "*2\r\n$3\r\nGET\r\n$1\r\nk\r\n".
// Lengths are bounded so a hostile header cannot make the server reserve gigabytes.
[[nodiscard]] ParseResult parse_resp(std::string_view input);

// Encodes a command the same way, which is also the AOF's on-disk record format.
void append_command(std::string& out, const Command& command);

}  // namespace miniredis
