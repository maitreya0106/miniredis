#pragma once

#include <string>
#include <vector>

#include "server/server.h"

namespace miniredis {

struct Config {
    ServerOptions server;
};

// Throws std::invalid_argument with a user-facing message on bad input.
Config parse_config(const std::vector<std::string>& args);

std::string usage();

}  // namespace miniredis
