#include "server/command_dispatcher.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
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

Reply not_an_integer() {
    return error("ERR value is not an integer or out of range");
}

Reply invalid_expire(const std::string& command) {
    return error("ERR invalid expire time in '" + command + "' command");
}

std::optional<std::int64_t> parse_int(const std::string& text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// A non-positive amount means "already expired". Returns nullopt if the deadline would overflow.
std::optional<TimePoint> deadline_after(TimePoint now, std::int64_t amount, std::int64_t unit_ms) {
    if (amount <= 0) {
        return now;
    }
    const std::int64_t headroom =
        std::numeric_limits<std::int64_t>::max() - now.time_since_epoch().count();
    if (amount > headroom / unit_ms) {
        return std::nullopt;
    }
    return now + std::chrono::milliseconds(amount * unit_ms);
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

// An absolute deadline at or before the epoch is simply "already expired".
std::optional<TimePoint> deadline_at(std::int64_t amount, std::int64_t unit_ms) {
    if (amount <= 0) {
        return TimePoint{};
    }
    if (amount > std::numeric_limits<std::int64_t>::max() / unit_ms) {
        return std::nullopt;
    }
    return TimePoint(std::chrono::milliseconds(amount * unit_ms));
}

struct ExpiryOption {
    bool absolute;
    std::int64_t unit_ms;
};

std::optional<ExpiryOption> parse_expiry_option(const std::string& option) {
    if (option == "EX") {
        return ExpiryOption{false, 1000};
    }
    if (option == "PX") {
        return ExpiryOption{false, 1};
    }
    if (option == "EXAT") {
        return ExpiryOption{true, 1000};
    }
    if (option == "PXAT") {
        return ExpiryOption{true, 1};
    }
    return std::nullopt;
}

// SET key value [EX seconds | PX milliseconds | EXAT unix-seconds | PXAT unix-milliseconds]
Reply set(Store& store, const Command& command) {
    std::optional<TimePoint> deadline;
    for (std::size_t i = 3; i < command.size(); i += 2) {
        const auto option = parse_expiry_option(to_upper(command[i]));
        if (!option || deadline || i + 1 >= command.size()) {
            return error("ERR syntax error");
        }
        const auto amount = parse_int(command[i + 1]);
        if (!amount) {
            return not_an_integer();
        }
        if (*amount <= 0) {
            return invalid_expire("set");
        }
        deadline = option->absolute ? deadline_at(*amount, option->unit_ms)
                                    : deadline_after(store.now(), *amount, option->unit_ms);
        if (!deadline) {
            return invalid_expire("set");
        }
    }
    store.set(command[1], command[2], deadline);
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

Reply expire(Store& store, const Command& command) {
    const auto seconds = parse_int(command[2]);
    if (!seconds) {
        return not_an_integer();
    }
    const auto deadline = deadline_after(store.now(), *seconds, 1000);
    if (!deadline) {
        return invalid_expire("expire");
    }
    return integer(store.expire_at(command[1], *deadline) ? 1 : 0);
}

Reply pexpireat(Store& store, const Command& command) {
    const auto unix_ms = parse_int(command[2]);
    if (!unix_ms) {
        return not_an_integer();
    }
    const auto deadline = deadline_at(*unix_ms, 1);
    if (!deadline) {
        return invalid_expire("pexpireat");
    }
    return integer(store.expire_at(command[1], *deadline) ? 1 : 0);
}

Reply ttl(Store& store, const Command& command) {
    const Ttl ttl = store.ttl(command[1]);
    switch (ttl.state) {
        case Ttl::State::Missing:
            return std::int64_t{-2};
        case Ttl::State::NoExpiry:
            return std::int64_t{-1};
        case Ttl::State::Expiring:
            break;
    }
    // Rounded to the nearest second, as Redis does.
    return static_cast<std::int64_t>((ttl.remaining.count() + 500) / 1000);
}

const CommandSpec* find_command(const std::string& name) {
    static const std::unordered_map<std::string, CommandSpec> commands = {
        {"PING", {ping, -1}},     {"SET", {set, -3}},
        {"GET", {get, 2}},        {"DEL", {del, -2}},
        {"EXISTS", {exists, -2}}, {"EXPIRE", {expire, 3}},
        {"TTL", {ttl, 2}},        {"PEXPIREAT", {pexpireat, 3}},
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
