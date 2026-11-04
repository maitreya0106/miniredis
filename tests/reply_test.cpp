#include "protocol/reply.h"

#include <gtest/gtest.h>

namespace miniredis {
namespace {

std::string encode(const Reply& reply) {
    std::string out;
    append_reply(out, reply);
    return out;
}

TEST(ReplyTest, EncodesEveryType) {
    EXPECT_EQ(encode(ok()), "+OK\r\n");
    EXPECT_EQ(encode(error("ERR bad")), "-ERR bad\r\n");
    EXPECT_EQ(encode(std::int64_t{-7}), ":-7\r\n");
    EXPECT_EQ(encode(BulkString{"hi"}), "$2\r\nhi\r\n");
    EXPECT_EQ(encode(BulkString{""}), "$0\r\n\r\n");
    EXPECT_EQ(encode(NilReply{}), "$-1\r\n");
}

TEST(ReplyTest, BulkStringIsBinarySafe) {
    EXPECT_EQ(encode(BulkString{std::string("a\r\n\0b", 5)}),
              std::string("$5\r\na\r\n\0b\r\n", 11));
}

}  // namespace
}  // namespace miniredis
