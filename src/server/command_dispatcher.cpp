#include "server/command_dispatcher.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace miniredis {

namespace {

using Handler = Reply (*)(Store&, const Command&);

// Arity follows the Redis convention: positive means exact, negative means "at least",
// and the count includes the command name itself.
struct CommandSpec {
    Handler handler;
    int arity;
};

std::string to_upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

Reply wrong_arity(const std::string& name) {
    return error("ERR wrong number of arguments for '" + to_lower(name) + "' command");
}

bool arity_matches(int arity, std::size_t arg_count) {
    const auto required = static_cast<std::size_t>(arity < 0 ? -arity : arity);
    return arity < 0 ? arg_count >= required : arg_count == required;
}

Reply integer(std::size_t value) {
    return static_cast<std::int64_t>(value);
}

Reply ping(Store& /*store*/, const Command& command) {
    if (command.size() > 2) {
        return wrong_arity(command[0]);
    }
    if (command.size() == 2) {
        return BulkString{command[1]};
    }
    return SimpleString{"PONG"};
}

Reply set(Store& store, const Command& command) {
    store.set(command[1], command[2]);
    return ok();
}

Reply get(Store& store, const Command& command) {
    auto value = store.get(command[1]);
    if (!value) {
        return NilReply{};
    }
    return BulkString{std::move(*value)};
}

Reply del(Store& store, const Command& command) {
    return integer(store.del(command.begin() + 1, command.end()));
}

Reply exists(Store& store, const Command& command) {
    return integer(store.exists(command.begin() + 1, command.end()));
}

const CommandSpec* find_command(const std::string& name) {
    static const std::unordered_map<std::string, CommandSpec> commands = {
        {"PING", {ping, -1}}, {"SET", {set, 3}},        {"GET", {get, 2}},
        {"DEL", {del, -2}},   {"EXISTS", {exists, -2}},
    };
    auto it = commands.find(to_upper(name));
    return it == commands.end() ? nullptr : &it->second;
}

}  // namespace

Reply CommandDispatcher::execute(const Command& command) {
    if (command.empty()) {
        return error("ERR empty command");
    }
    const CommandSpec* spec = find_command(command[0]);
    if (spec == nullptr) {
        return error("ERR unknown command '" + command[0] + "'");
    }
    if (!arity_matches(spec->arity, command.size())) {
        return wrong_arity(command[0]);
    }
    return spec->handler(store_, command);
}

}  // namespace miniredis
