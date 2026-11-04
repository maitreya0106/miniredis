#include "protocol/resp.h"

#include <algorithm>
#include <charconv>
#include <optional>

namespace miniredis {

namespace {

constexpr std::size_t kMaxHeaderLine = 64;

std::string protocol_error(const std::string& detail) {
    return "ERR Protocol error: " + detail;
}

class Reader {
public:
    explicit Reader(std::string_view input) : input_(input) {}

    std::size_t position() const { return pos_; }
    bool at_end() const { return pos_ >= input_.size(); }
    char peek() const { return input_[pos_]; }

    // Reads "<prefix><integer>\r\n". Returns nullopt with `incomplete_` set if more bytes are
    // needed, or with `incomplete_` clear if the line is malformed.
    std::optional<long long> header(char prefix) {
        incomplete_ = false;
        if (at_end()) {
            incomplete_ = true;
            return std::nullopt;
        }
        if (peek() != prefix) {
            return std::nullopt;
        }
        const std::size_t line_end = input_.find("\r\n", pos_);
        if (line_end == std::string_view::npos) {
            incomplete_ = input_.size() - pos_ <= kMaxHeaderLine;
            return std::nullopt;
        }
        long long value = 0;
        const char* first = input_.data() + pos_ + 1;
        const char* last = input_.data() + line_end;
        const auto [end, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || end != last || first == last) {
            return std::nullopt;
        }
        pos_ = line_end + 2;
        return value;
    }

    // Reads `length` payload bytes plus the trailing CRLF.
    std::optional<std::string> payload(std::size_t length) {
        incomplete_ = false;
        if (input_.size() - pos_ < length + 2) {
            incomplete_ = true;
            return std::nullopt;
        }
        if (input_.substr(pos_ + length, 2) != "\r\n") {
            return std::nullopt;
        }
        std::string value(input_.substr(pos_, length));
        pos_ += length + 2;
        return value;
    }

    bool incomplete() const { return incomplete_; }

private:
    std::string_view input_;
    std::size_t pos_ = 0;
    bool incomplete_ = false;
};

std::string unexpected_byte(char expected, char got) {
    return protocol_error(std::string("expected '") + expected + "', got '" + got + "'");
}

}  // namespace

ParseResult parse_resp(std::string_view input) {
    Reader reader(input);
    const auto count = reader.header('*');
    if (!count) {
        if (reader.incomplete()) {
            return ParseResult::incomplete();
        }
        if (!reader.at_end() && reader.peek() != '*') {
            return ParseResult::failure(unexpected_byte('*', reader.peek()));
        }
        return ParseResult::failure(protocol_error("invalid multibulk length"));
    }
    // Redis treats empty and null arrays as a no-op request.
    if (*count <= 0) {
        return ParseResult::complete(reader.position(), {});
    }
    if (static_cast<unsigned long long>(*count) > kMaxArrayLength) {
        return ParseResult::failure(protocol_error("invalid multibulk length"));
    }

    Command command;
    command.reserve(std::min<std::size_t>(static_cast<std::size_t>(*count), 1024));
    for (long long i = 0; i < *count; ++i) {
        const auto length = reader.header('$');
        if (!length) {
            if (reader.incomplete()) {
                return ParseResult::incomplete();
            }
            if (reader.peek() != '$') {
                return ParseResult::failure(unexpected_byte('$', reader.peek()));
            }
            return ParseResult::failure(protocol_error("invalid bulk length"));
        }
        if (*length < 0 || static_cast<unsigned long long>(*length) > kMaxBulkLength) {
            return ParseResult::failure(protocol_error("invalid bulk length"));
        }
        auto value = reader.payload(static_cast<std::size_t>(*length));
        if (!value) {
            if (reader.incomplete()) {
                return ParseResult::incomplete();
            }
            return ParseResult::failure(protocol_error("bulk string not terminated by CRLF"));
        }
        command.push_back(std::move(*value));
    }
    return ParseResult::complete(reader.position(), std::move(command));
}

void append_command(std::string& out, const Command& command) {
    out += '*';
    out += std::to_string(command.size());
    out += "\r\n";
    for (const auto& arg : command) {
        out += '$';
        out += std::to_string(arg.size());
        out += "\r\n";
        out += arg;
        out += "\r\n";
    }
}

}  // namespace miniredis
