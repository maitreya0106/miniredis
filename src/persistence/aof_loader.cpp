#include "persistence/aof_loader.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>

#include "protocol/resp.h"

namespace miniredis {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open AOF " + path);
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

AofLoadResult load_aof(const std::string& path, const std::function<void(const Command&)>& apply) {
    AofLoadResult result;
    if (!std::filesystem::exists(path)) {
        return result;
    }

    const std::string contents = read_file(path);
    const std::string_view data(contents);
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ParseResult parsed = parse_resp(data.substr(offset));
        if (parsed.status == ParseStatus::Incomplete) {
            result.truncated_bytes = data.size() - offset;
            std::filesystem::resize_file(path, offset);
            break;
        }
        if (parsed.status == ParseStatus::Error) {
            throw std::runtime_error("AOF " + path + " is corrupt at byte " +
                                     std::to_string(offset) + ": " + parsed.error);
        }
        apply(parsed.command);
        offset += parsed.consumed;
        ++result.commands;
    }
    return result;
}

}  // namespace miniredis
