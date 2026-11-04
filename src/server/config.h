#pragma once

#include <string>
#include <vector>

#include "persistence/aof_writer.h"
#include "server/server.h"

namespace miniredis {

struct Config {
    ServerOptions server;
    std::size_t max_keys = 0;
    std::string aof_path;  // empty disables persistence
    FsyncPolicy fsync = FsyncPolicy::EverySecond;
};

// Throws std::invalid_argument with a user-facing message on bad input.
Config parse_config(const std::vector<std::string>& args);

std::string usage();

}  // namespace miniredis
