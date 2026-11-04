#include "protocol/inline_parser.h"

#include <gtest/gtest.h>

#include <string>

namespace miniredis {
namespace {

Command parse_ok(std::string_view input) {
    ParseResult result = parse_inline(input);
    EXPECT_EQ(result.status, ParseStatus::Complete) << result.error;
    return result.command;
}

TEST(InlineParserTest, SplitsOnWhitespace) {
    EXPECT_EQ(parse_ok("SET  key \t value\r\n"), (Command{"SET", "key", "value"}));
}

TEST(InlineParserTest, AcceptsBareNewline) {
    EXPECT_EQ(parse_ok("PING\n"), Command{"PING"});
}

TEST(InlineParserTest, WaitsForNewline) {
    EXPECT_EQ(parse_inline("SET key val").status, ParseStatus::Incomplete);
}

TEST(InlineParserTest, ConsumesOnlyFirstRequest) {
    ParseResult result = parse_inline("PING\r\nGET a\r\n");
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_EQ(result.consumed, 6U);
    EXPECT_EQ(result.command, Command{"PING"});
}

TEST(InlineParserTest, EmptyLineYieldsNoArguments) {
    ParseResult result = parse_inline("\r\n");
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_TRUE(result.command.empty());
    EXPECT_EQ(result.consumed, 2U);
}

TEST(InlineParserTest, DoubleQuotesKeepSpacesAndUnescape) {
    EXPECT_EQ(parse_ok(R"(SET k "hello world\n\"x\" \x41")"
                       "\n"),
              (Command{"SET", "k", "hello world\n\"x\" A"}));
}

TEST(InlineParserTest, SingleQuotesAreLiteral) {
    EXPECT_EQ(parse_ok(R"(SET k 'a\nb \'c')"
                       "\n"),
              (Command{"SET", "k", "a\\nb 'c"}));
}

TEST(InlineParserTest, EmptyQuotedArgument) {
    EXPECT_EQ(parse_ok("SET k \"\"\n"), (Command{"SET", "k", ""}));
}

TEST(InlineParserTest, RejectsUnbalancedQuotes) {
    EXPECT_EQ(parse_inline("SET k \"oops\n").status, ParseStatus::Error);
}

TEST(InlineParserTest, RejectsTextGluedToClosingQuote) {
    EXPECT_EQ(parse_inline("SET k \"a\"b\n").status, ParseStatus::Error);
}

TEST(InlineParserTest, RejectsOversizedRequestWithoutNewline) {
    const std::string huge(kMaxInlineRequestSize + 1, 'x');
    EXPECT_EQ(parse_inline(huge).status, ParseStatus::Error);
}

}  // namespace
}  // namespace miniredis
