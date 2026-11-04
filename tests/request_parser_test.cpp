#include "protocol/request_parser.h"

#include <gtest/gtest.h>

namespace miniredis {
namespace {

TEST(RequestParserTest, PicksRespForArrays) {
    const ParseResult result = parse_request("*1\r\n$4\r\nPING\r\n");
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_EQ(result.command, Command{"PING"});
    EXPECT_EQ(result.consumed, 14U);
}

TEST(RequestParserTest, PicksInlineForEverythingElse) {
    const ParseResult result = parse_request("PING\r\n");
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_EQ(result.command, Command{"PING"});
}

TEST(RequestParserTest, EmptyInputIsIncomplete) {
    EXPECT_EQ(parse_request("").status, ParseStatus::Incomplete);
}

}  // namespace
}  // namespace miniredis
