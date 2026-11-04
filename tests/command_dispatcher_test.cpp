#include "server/command_dispatcher.h"

#include <gtest/gtest.h>

namespace miniredis {
namespace {

class CommandDispatcherTest : public ::testing::Test {
protected:
    std::string run(const Command& command) {
        std::string out;
        append_reply(out, dispatcher_.execute(command));
        return out;
    }

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

}  // namespace
}  // namespace miniredis
