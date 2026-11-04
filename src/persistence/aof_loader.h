#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "protocol/command.h"

namespace miniredis {

struct AofLoadResult {
    std::size_t commands = 0;
    std::size_t truncated_bytes = 0;
};

// Replays every record in order through `apply`. A missing file is an empty log. An incomplete
// final record is what a crash mid-write leaves behind, so it is cut from the file; any other
// malformed data means real corruption and throws std::runtime_error.
[[nodiscard]] AofLoadResult load_aof(const std::string& path,
                                     const std::function<void(const Command&)>& apply);

}  // namespace miniredis
