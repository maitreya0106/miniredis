#include "server/config.h"

#include <gtest/gtest.h>

namespace miniredis {
namespace {

TEST(ConfigTest, Defaults) {
    const Config config = parse_config({});
    EXPECT_EQ(config.server.bind_address, "127.0.0.1");
    EXPECT_EQ(config.server.port, 6380);
    EXPECT_EQ(config.server.threads, 16U);
    EXPECT_EQ(config.max_keys, 0U);
}

TEST(ConfigTest, ParsesMaxKeys) {
    EXPECT_EQ(parse_config({"--maxkeys", "1000"}).max_keys, 1000U);
}

TEST(ConfigTest, ParsesServerFlags) {
    const Config config = parse_config({"--bind", "0.0.0.0", "--port", "7000", "--threads", "4"});
    EXPECT_EQ(config.server.bind_address, "0.0.0.0");
    EXPECT_EQ(config.server.port, 7000);
    EXPECT_EQ(config.server.threads, 4U);
}

TEST(ConfigTest, RejectsBadInput) {
    EXPECT_THROW(parse_config({"--port"}), std::invalid_argument);
    EXPECT_THROW(parse_config({"--port", "70000"}), std::invalid_argument);
    EXPECT_THROW(parse_config({"--port", "12ab"}), std::invalid_argument);
    EXPECT_THROW(parse_config({"--threads", "0"}), std::invalid_argument);
    EXPECT_THROW(parse_config({"--verbose", "yes"}), std::invalid_argument);
}

}  // namespace
}  // namespace miniredis
