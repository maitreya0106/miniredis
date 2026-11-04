#include "server/server.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "test_client.h"

namespace miniredis {
namespace {

using namespace std::chrono_literals;
using testing::TestClient;

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ServerOptions options;
        options.port = 0;
        options.threads = 4;
        server_ = std::make_unique<Server>(options, dispatcher_);
        running_ = std::async(std::launch::async, [this] { server_->run(); });
    }

    void TearDown() override {
        server_->stop();
        ASSERT_EQ(running_.wait_for(5s), std::future_status::ready) << "server did not stop";
        running_.get();
    }

    std::uint16_t port() const { return server_->port(); }

    Store store_;
    CommandDispatcher dispatcher_{store_};
    std::unique_ptr<Server> server_;
    std::future<void> running_;
};

TEST_F(ServerTest, ServesBasicCommands) {
    TestClient client(port());
    EXPECT_EQ(client.request("PING"), "+PONG\r\n");
    EXPECT_EQ(client.request("SET greeting \"hello world\""), "+OK\r\n");
    EXPECT_EQ(client.request("GET greeting"), "$11\r\nhello world\r\n");
    EXPECT_EQ(client.request("EXISTS greeting"), ":1\r\n");
    EXPECT_EQ(client.request("DEL greeting"), ":1\r\n");
    EXPECT_EQ(client.request("GET greeting"), "$-1\r\n");
}

TEST_F(ServerTest, SpeaksRespAndInlineOnTheSameConnection) {
    TestClient client(port());
    client.send("*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$10\r\nhello\r\nyou\r\n");
    EXPECT_EQ(client.read_reply(), "+OK\r\n");
    EXPECT_EQ(client.request("GET key"), "$10\r\nhello\r\nyou\r\n");
    client.send("*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n");
    EXPECT_EQ(client.read_reply(), "$10\r\nhello\r\nyou\r\n");
}

TEST_F(ServerTest, ReassemblesRespSplitAcrossPackets) {
    TestClient client(port());
    const std::string request = "*3\r\n$3\r\nSET\r\n$1\r\nk\r\n$5\r\nvalue\r\n";
    for (const char byte : request) {
        client.send(std::string(1, byte));
    }
    EXPECT_EQ(client.read_reply(), "+OK\r\n");
}

TEST_F(ServerTest, ClosesConnectionOnMalformedResp) {
    TestClient client(port());
    client.send("*1\r\n+PING\r\n");
    EXPECT_EQ(client.read_reply(), "-ERR Protocol error: expected '$', got '+'\r\n");
    EXPECT_TRUE(client.closed_by_peer());
}

TEST_F(ServerTest, AnswersPipelinedRequestsInOrder) {
    TestClient client(port());
    client.send("SET a 1\r\nSET b 2\r\nGET a\r\nGET b\r\n");
    EXPECT_EQ(client.read_reply(), "+OK\r\n");
    EXPECT_EQ(client.read_reply(), "+OK\r\n");
    EXPECT_EQ(client.read_reply(), "$1\r\n1\r\n");
    EXPECT_EQ(client.read_reply(), "$1\r\n2\r\n");
}

TEST_F(ServerTest, ReassemblesRequestsSplitAcrossPackets) {
    TestClient client(port());
    client.send("SE");
    std::this_thread::sleep_for(20ms);
    client.send("T k v\r");
    std::this_thread::sleep_for(20ms);
    client.send("\nGET k\r\n");
    EXPECT_EQ(client.read_reply(), "+OK\r\n");
    EXPECT_EQ(client.read_reply(), "$1\r\nv\r\n");
}

TEST_F(ServerTest, ReportsErrorsWithoutClosing) {
    TestClient client(port());
    EXPECT_EQ(client.request("NOPE"), "-ERR unknown command 'NOPE'\r\n");
    EXPECT_EQ(client.request("PING"), "+PONG\r\n");
}

TEST_F(ServerTest, ClosesConnectionOnProtocolError) {
    TestClient client(port());
    EXPECT_EQ(client.request("SET k \"unterminated"),
              "-ERR Protocol error: unbalanced quotes in request\r\n");
    EXPECT_TRUE(client.closed_by_peer());
}

TEST_F(ServerTest, ServesClientsConcurrently) {
    constexpr int kClients = 4;
    std::vector<std::future<void>> clients;
    for (int c = 0; c < kClients; ++c) {
        clients.push_back(std::async(std::launch::async, [this, c] {
            TestClient client(port());
            for (int i = 0; i < 200; ++i) {
                const std::string key = "k" + std::to_string(c) + "_" + std::to_string(i);
                ASSERT_EQ(client.request("SET " + key + " " + key), "+OK\r\n");
            }
        }));
    }
    for (auto& client : clients) {
        client.get();
    }
    EXPECT_EQ(store_.size(), static_cast<std::size_t>(kClients * 200));
}

TEST_F(ServerTest, StopDisconnectsIdleClients) {
    TestClient idle(port());
    EXPECT_EQ(idle.request("PING"), "+PONG\r\n");
    server_->stop();
    EXPECT_TRUE(idle.closed_by_peer());
}

}  // namespace
}  // namespace miniredis
