#include "protocol/inline_parser.h"

#include <cctype>
#include <optional>
#include <string>

namespace miniredis {

namespace {

bool is_space(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::optional<int> hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower >= 'a' && lower <= 'f') {
        return lower - 'a' + 10;
    }
    return std::nullopt;
}

char unescape(char c) {
    switch (c) {
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'b':
            return '\b';
        case 'a':
            return '\a';
        default:
            return c;
    }
}

class Tokenizer {
public:
    explicit Tokenizer(std::string_view line) : line_(line) {}

    std::optional<Command> split() {
        Command args;
        for (;;) {
            skip_spaces();
            if (at_end()) {
                return args;
            }
            std::optional<std::string> arg;
            if (peek() == '"') {
                arg = double_quoted();
            } else if (peek() == '\'') {
                arg = single_quoted();
            } else {
                arg = bare();
            }
            if (!arg) {
                return std::nullopt;
            }
            args.push_back(std::move(*arg));
        }
    }

private:
    bool at_end() const { return pos_ >= line_.size(); }
    char peek(std::size_t ahead = 0) const { return line_[pos_ + ahead]; }
    bool has(std::size_t count) const { return pos_ + count < line_.size(); }

    void skip_spaces() {
        while (!at_end() && is_space(peek())) {
            ++pos_;
        }
    }

    // A closing quote must be followed by whitespace or the end of the line.
    bool closes_cleanly() {
        ++pos_;
        return at_end() || is_space(peek());
    }

    std::string bare() {
        const std::size_t start = pos_;
        while (!at_end() && !is_space(peek())) {
            ++pos_;
        }
        return std::string(line_.substr(start, pos_ - start));
    }

    std::optional<std::string> double_quoted() {
        std::string arg;
        ++pos_;
        while (!at_end()) {
            const char c = peek();
            if (c == '"') {
                return closes_cleanly() ? std::optional(std::move(arg)) : std::nullopt;
            }
            if (c == '\\' && has(1)) {
                if (peek(1) == 'x' && has(3)) {
                    auto high = hex_value(peek(2));
                    auto low = hex_value(peek(3));
                    if (high && low) {
                        arg += static_cast<char>((*high << 4) | *low);
                        pos_ += 4;
                        continue;
                    }
                }
                arg += unescape(peek(1));
                pos_ += 2;
                continue;
            }
            arg += c;
            ++pos_;
        }
        return std::nullopt;
    }

    std::optional<std::string> single_quoted() {
        std::string arg;
        ++pos_;
        while (!at_end()) {
            const char c = peek();
            if (c == '\'') {
                return closes_cleanly() ? std::optional(std::move(arg)) : std::nullopt;
            }
            if (c == '\\' && has(1) && peek(1) == '\'') {
                arg += '\'';
                pos_ += 2;
                continue;
            }
            arg += c;
            ++pos_;
        }
        return std::nullopt;
    }

    std::string_view line_;
    std::size_t pos_ = 0;
};

}  // namespace

ParseResult parse_inline(std::string_view input) {
    const std::size_t newline = input.find('\n');
    if (newline == std::string_view::npos) {
        if (input.size() > kMaxInlineRequestSize) {
            return ParseResult::failure("ERR Protocol error: too big inline request");
        }
        return ParseResult::incomplete();
    }

    std::string_view line = input.substr(0, newline);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    auto args = Tokenizer(line).split();
    if (!args) {
        return ParseResult::failure("ERR Protocol error: unbalanced quotes in request");
    }
    return ParseResult::complete(newline + 1, std::move(*args));
}

}  // namespace miniredis
