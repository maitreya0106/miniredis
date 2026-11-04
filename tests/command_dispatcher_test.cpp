#include "server/command_dispatcher.h"

#include <gtest/gtest.h>

#include <chrono>

namespace miniredis {
namespace {

class CommandDispatcherTest : public ::testing::Test {
protected:
    CommandDispatcherTest()
        : store_(
              StoreOptions{0, [this] { return TimePoint(std::chrono::milliseconds(now_ms_)); }}) {}

    void advance(std::chrono::milliseconds by) { now_ms_ += by.count(); }

    std::string run(const Command& command) {
        std::string out;
        append_reply(out, dispatcher_.execute(command));
        return out;
    }

    std::int64_t now_ms_ = 1'000'000;
    Store store_;
    CommandDispatcher dispatcher_{store_};
};

TEST_F(CommandDispatcherTest, Ping) {
    EXPECT_EQ(run({"PING"}), "+PONG\r\n");
    EXPECT_EQ(run({"PING", "hi"}), "$2\r\nhi\r\n");
}

TEST_F(CommandDispatcherTest, SetGet) {
    EXPECT_EQ(run({"SET", "k", "v"}), "+OK\r\n");
    EXPECT_EQ(run({"GET", "k"}), "$1\r\nv\r\n");
    EXPECT_EQ(run({"GET", "missing"}), "$-1\r\n");
}

TEST_F(CommandDispatcherTest, CommandNamesAreCaseInsensitive) {
    EXPECT_EQ(run({"set", "k", "v"}), "+OK\r\n");
    EXPECT_EQ(run({"gEt", "k"}), "$1\r\nv\r\n");
}

TEST_F(CommandDispatcherTest, DelAndExistsReturnCounts) {
    run({"SET", "a", "1"});
    run({"SET", "b", "2"});
    EXPECT_EQ(run({"EXISTS", "a", "b", "c"}), ":2\r\n");
    EXPECT_EQ(run({"DEL", "a", "c"}), ":1\r\n");
    EXPECT_EQ(run({"EXISTS", "a"}), ":0\r\n");
}

TEST_F(CommandDispatcherTest, UnknownCommand) {
    EXPECT_EQ(run({"FLY", "away"}), "-ERR unknown command 'FLY'\r\n");
}

TEST_F(CommandDispatcherTest, WrongArity) {
    EXPECT_EQ(run({"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(run({"GET", "a", "b"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(run({"DEL"}), "-ERR wrong number of arguments for 'del' command\r\n");
    EXPECT_EQ(run({"PING", "a", "b"}), "-ERR wrong number of arguments for 'ping' command\r\n");
}

TEST_F(CommandDispatcherTest, SetWithExpiry) {
    EXPECT_EQ(run({"SET", "a", "v", "EX", "10"}), "+OK\r\n");
    EXPECT_EQ(run({"TTL", "a"}), ":10\r\n");
    EXPECT_EQ(run({"SET", "b", "v", "px", "1500"}), "+OK\r\n");
    EXPECT_EQ(run({"TTL", "b"}), ":2\r\n");
    advance(std::chrono::milliseconds(1500));
    EXPECT_EQ(run({"GET", "b"}), "$-1\r\n");
}

TEST_F(CommandDispatcherTest, SetRejectsBadOptions) {
    EXPECT_EQ(run({"SET", "a", "v", "EX"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "a", "v", "XX", "1"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "a", "v", "EX", "1", "PX", "1"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run({"SET", "a", "v", "EX", "ten"}),
              "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run({"SET", "a", "v", "EX", "0"}), "-ERR invalid expire time in 'set' command\r\n");
    EXPECT_EQ(run({"SET", "a", "v", "EX", "9223372036854775807"}),
              "-ERR invalid expire time in 'set' command\r\n");
    EXPECT_EQ(run({"EXISTS", "a"}), ":0\r\n");
}

TEST_F(CommandDispatcherTest, ExpireAndTtl) {
    EXPECT_EQ(run({"TTL", "missing"}), ":-2\r\n");
    EXPECT_EQ(run({"EXPIRE", "missing", "10"}), ":0\r\n");
    run({"SET", "k", "v"});
    EXPECT_EQ(run({"TTL", "k"}), ":-1\r\n");
    EXPECT_EQ(run({"EXPIRE", "k", "100"}), ":1\r\n");
    advance(std::chrono::seconds(40));
    EXPECT_EQ(run({"TTL", "k"}), ":60\r\n");
    EXPECT_EQ(run({"SET", "k", "v2"}), "+OK\r\n");
    EXPECT_EQ(run({"TTL", "k"}), ":-1\r\n");
}

TEST_F(CommandDispatcherTest, NonPositiveExpireDeletesKey) {
    run({"SET", "k", "v"});
    EXPECT_EQ(run({"EXPIRE", "k", "-5"}), ":1\r\n");
    EXPECT_EQ(run({"EXISTS", "k"}), ":0\r\n");
}

TEST_F(CommandDispatcherTest, ExpireRejectsBadNumbers) {
    run({"SET", "k", "v"});
    EXPECT_EQ(run({"EXPIRE", "k", "1.5"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run({"EXPIRE", "k", "9223372036854775807"}),
              "-ERR invalid expire time in 'expire' command\r\n");
    EXPECT_EQ(run({"TTL", "k"}), ":-1\r\n");
}

}  // namespace
}  // namespace miniredis
