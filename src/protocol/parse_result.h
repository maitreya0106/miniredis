#pragma once

#include <cstddef>
#include <string>

#include "protocol/command.h"

namespace miniredis {

enum class ParseStatus { Incomplete, Complete, Error };

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    std::size_t consumed = 0;
    Command command;
    std::string error;

    static ParseResult incomplete() { return {}; }
    static ParseResult complete(std::size_t consumed, Command command) {
        return {ParseStatus::Complete, consumed, std::move(command), {}};
    }
    static ParseResult failure(std::string error) {
        return {ParseStatus::Error, 0, {}, std::move(error)};
    }
};

}  // namespace miniredis
