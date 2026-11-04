#pragma once

#include <cstddef>
#include <string_view>

#include "protocol/parse_result.h"

namespace miniredis {

inline constexpr std::size_t kMaxInlineRequestSize = std::size_t{64} * 1024;

// Parses one newline-terminated request. Arguments are split on whitespace, and quoting
// follows redis-cli: "double quotes" support escapes such as \n and \x41, 'single' are literal.
ParseResult parse_inline(std::string_view input);

}  // namespace miniredis
