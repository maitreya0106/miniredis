#include "server/config.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace miniredis {

namespace {

template <typename T>
T parse_number(std::string_view flag, std::string_view text, T min, T max) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < min || value > max) {
        throw std::invalid_argument(std::string(flag) + " expects a number between " +
                                    std::to_string(min) + " and " + std::to_string(max));
    }
    return value;
}

}  // namespace

std::string usage() {
    return "usage: miniredis [options]\n"
           "  --bind <ipv4>       address to listen on (default 127.0.0.1)\n"
           "  --port <n>          TCP port, 0 picks a free one (default 6380)\n"
           "  --threads <n>       worker threads = max concurrent clients (default 16)\n";
}

Config parse_config(const std::vector<std::string>& args) {
    Config config;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (i + 1 >= args.size()) {
            throw std::invalid_argument("missing value for " + flag);
        }
        const std::string& value = args[++i];

        if (flag == "--bind") {
            config.server.bind_address = value;
        } else if (flag == "--port") {
            config.server.port = parse_number<std::uint16_t>(flag, value, 0, 65535);
        } else if (flag == "--threads") {
            config.server.threads = parse_number<std::size_t>(flag, value, 1, 4096);
        } else {
            throw std::invalid_argument("unknown option " + flag);
        }
    }
    return config;
}

}  // namespace miniredis
