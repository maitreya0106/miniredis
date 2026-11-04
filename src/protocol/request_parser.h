#pragma once

#include <string_view>

#include "protocol/parse_result.h"

namespace miniredis {

// Chooses the format per request, as Redis does: RESP arrays start with '*', anything else is an
// inline command. So redis-cli and a human on netcat can use the same port.
[[nodiscard]] ParseResult parse_request(std::string_view input);

}  // namespace miniredis
