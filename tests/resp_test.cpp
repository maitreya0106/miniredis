#include "protocol/resp.h"

#include <gtest/gtest.h>

#include <string>

namespace miniredis {
namespace {

std::string encode(const Command& command) {
    std::string out;
    append_command(out, command);
    return out;
}

TEST(RespTest, EncodesCommandAsArrayOfBulkStrings) {
    EXPECT_EQ(encode({"SET", "k", "hello"}), "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nhello\r\n");
}

TEST(RespTest, ParsesWhatItEncodes) {
    const Command command{"SET", "key with spaces", std::string("bin\0\r\nary", 9), ""};
    const std::string wire = encode(command);
    const ParseResult result = parse_resp(wire);
    ASSERT_EQ(result.status, ParseStatus::Complete) << result.error;
    EXPECT_EQ(result.consumed, wire.size());
    EXPECT_EQ(result.command, command);
}

TEST(RespTest, EveryStrictPrefixIsIncomplete) {
    const std::string wire = encode({"SET", "k", "value"});
    for (std::size_t length = 0; length < wire.size(); ++length) {
        EXPECT_EQ(parse_resp(wire.substr(0, length)).status, ParseStatus::Incomplete)
            << "prefix length " << length;
    }
}

TEST(RespTest, ConsumesOnlyTheFirstRequest) {
    const std::string first = encode({"PING"});
    const ParseResult result = parse_resp(first + encode({"GET", "k"}));
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_EQ(result.consumed, first.size());
}

TEST(RespTest, EmptyArrayIsANoOp) {
    const ParseResult result = parse_resp("*0\r\n");
    ASSERT_EQ(result.status, ParseStatus::Complete);
    EXPECT_TRUE(result.command.empty());
}

TEST(RespTest, RejectsMalformedInput) {
    EXPECT_EQ(parse_resp("*x\r\n").status, ParseStatus::Error);
    EXPECT_EQ(parse_resp("*1\r\n+OK\r\n").status, ParseStatus::Error);
    EXPECT_EQ(parse_resp("*1\r\n$-1\r\n").status, ParseStatus::Error);
    EXPECT_EQ(parse_resp("*1\r\n$3\r\nabcXY").status, ParseStatus::Error);
    EXPECT_EQ(parse_resp("*99999999\r\n").status, ParseStatus::Error);
    EXPECT_EQ(parse_resp("*1\r\n$999999999999\r\n").status, ParseStatus::Error);
}

TEST(RespTest, RejectsEndlessHeaderLine) {
    EXPECT_EQ(parse_resp("*" + std::string(100, '1')).status, ParseStatus::Error);
}

}  // namespace
}  // namespace miniredis
