#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "protocol/resp.h"
#include "server_process.h"
#include "temp_dir.h"
#include "test_client.h"

namespace miniredis {
namespace {

using namespace std::chrono_literals;
using testing::ServerProcess;
using testing::TempDir;
using testing::TestClient;

std::string key(int i) {
    return "key:" + std::to_string(i);
}

std::string value(int i) {
    return "value-" + std::to_string(i) + "-" + std::string(32, 'x');
}

class CrashTest : public ::testing::TestWithParam<const char*> {};

// A client writes as fast as it can while the server is SIGKILLed underneath it. Every write the
// client saw acknowledged must still be there after a restart.
TEST_P(CrashTest, AcknowledgedWritesSurviveKill9) {
    const TempDir dir;
    const std::vector<std::string> args{"--aof", dir.file("appendonly.aof"), "--fsync", GetParam()};

    std::atomic<int> acknowledged{0};
    {
        ServerProcess server(args);
        std::thread writer([&] {
            try {
                TestClient client(server.port());
                for (int i = 0;; ++i) {
                    if (client.request("SET " + key(i) + " " + value(i)) != "+OK\r\n") {
                        return;
                    }
                    acknowledged = i + 1;
                }
            } catch (const std::exception&) {
                // The connection dies with the server; that is the point.
            }
        });

        const auto give_up = std::chrono::steady_clock::now() + 20s;
        while (acknowledged < 500 && std::chrono::steady_clock::now() < give_up) {
            std::this_thread::sleep_for(1ms);
        }
        server.kill_hard();
        writer.join();
    }
    ASSERT_GE(acknowledged, 500) << "writer made too little progress before the kill";

    const ServerProcess restarted(args);
    TestClient client(restarted.port());
    for (int i = 0; i < acknowledged; ++i) {
        const std::string expected = value(i);
        ASSERT_EQ(client.request("GET " + key(i)),
                  "$" + std::to_string(expected.size()) + "\r\n" + expected + "\r\n")
            << "acknowledged write #" << i << " was lost";
    }
}

// SIGKILL leaves written-but-unsynced data in the page cache, so it survives even with
// "everysec"; fsync is what protects against an OS crash or power loss, which a test cannot cause.
INSTANTIATE_TEST_SUITE_P(FsyncPolicies, CrashTest, ::testing::Values("always", "everysec"));

TEST(CrashRecoveryTest, StartsAfterTruncatingATornRecord) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    std::string contents;
    append_command(contents, {"SET", "survivor", "yes"});
    std::string torn;
    append_command(torn, {"SET", "torn", "no"});
    contents += torn.substr(0, torn.size() / 2);
    std::ofstream(path, std::ios::binary) << contents;

    const ServerProcess server({"--aof", path});
    EXPECT_NE(server.startup_output().find("Truncated"), std::string::npos);
    TestClient client(server.port());
    EXPECT_EQ(client.request("GET survivor"), "$3\r\nyes\r\n");
    EXPECT_EQ(client.request("GET torn"), "$-1\r\n");
}

}  // namespace
}  // namespace miniredis
