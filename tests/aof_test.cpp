#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "persistence/aof_loader.h"
#include "persistence/aof_writer.h"
#include "protocol/resp.h"
#include "server/command_dispatcher.h"
#include "store/store.h"
#include "temp_dir.h"

namespace miniredis {
namespace {

using namespace std::chrono_literals;
using testing::TempDir;

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream(path, std::ios::binary) << contents;
}

std::string encode(const std::vector<Command>& commands) {
    std::string out;
    for (const auto& command : commands) {
        append_command(out, command);
    }
    return out;
}

class RecordingLog final : public WriteLog {
public:
    void append(const Command& command) override { commands.push_back(command); }
    std::vector<Command> commands;
};

std::vector<Command> load_all(const std::string& path, AofLoadResult* result = nullptr) {
    std::vector<Command> replayed;
    const AofLoadResult loaded =
        load_aof(path, [&replayed](const Command& command) { replayed.push_back(command); });
    if (result != nullptr) {
        *result = loaded;
    }
    return replayed;
}

TEST(AofWriterTest, AppendsRespRecordsForBothPolicies) {
    for (const FsyncPolicy policy : {FsyncPolicy::Always, FsyncPolicy::EverySecond}) {
        const TempDir dir;
        const std::string path = dir.file("appendonly.aof");
        {
            AofWriter writer(path, policy);
            writer.append({"SET", "a", "1"});
            writer.append({"DEL", "a"});
        }
        EXPECT_EQ(read_file(path), encode({{"SET", "a", "1"}, {"DEL", "a"}}));
    }
}

TEST(AofWriterTest, AppendsToAnExistingFile) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    AofWriter(path, FsyncPolicy::Always).append({"SET", "a", "1"});
    AofWriter(path, FsyncPolicy::Always).append({"SET", "b", "2"});
    EXPECT_EQ(load_all(path).size(), 2U);
}

TEST(AofLoaderTest, MissingFileIsAnEmptyLog) {
    const TempDir dir;
    AofLoadResult result;
    EXPECT_TRUE(load_all(dir.file("nope.aof"), &result).empty());
    EXPECT_EQ(result.commands, 0U);
}

TEST(AofLoaderTest, ReplaysRecordsInOrder) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    const std::vector<Command> commands{{"SET", "a", "1"}, {"SET", "b", "2"}, {"DEL", "a"}};
    write_file(path, encode(commands));
    EXPECT_EQ(load_all(path), commands);
}

TEST(AofLoaderTest, TruncatesTornFinalRecord) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    const std::string intact = encode({{"SET", "a", "1"}, {"SET", "b", "2"}});
    const std::string torn = encode({{"SET", "c", "3"}}).substr(0, 10);
    write_file(path, intact + torn);

    AofLoadResult result;
    EXPECT_EQ(load_all(path, &result).size(), 2U);
    EXPECT_EQ(result.truncated_bytes, torn.size());
    EXPECT_EQ(read_file(path), intact);
}

TEST(AofLoaderTest, RejectsCorruptionBeforeTheEnd) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    write_file(path, encode({{"SET", "a", "1"}}) + "garbage\r\n" + encode({{"SET", "b", "2"}}));
    EXPECT_THROW(load_all(path), std::runtime_error);
}

class StoreWriteLogTest : public ::testing::Test {
protected:
    StoreWriteLogTest() { store_.set_write_log(&log_); }

    Store make_store(std::size_t max_keys) {
        StoreOptions options;
        options.max_keys = max_keys;
        options.now = [this] { return now_; };
        return Store(options);
    }

    TimePoint now_{std::chrono::milliseconds(1'000'000)};
    RecordingLog log_;
    Store store_ = make_store(0);
};

TEST_F(StoreWriteLogTest, LogsWritesWithAbsoluteDeadlines) {
    store_.set("a", "1");
    store_.set("b", "2", now_ + 5s);
    store_.expire_at("a", now_ + 1s);
    const std::vector<std::string> keys{"a", "missing"};
    store_.del(keys.begin(), keys.end());

    EXPECT_EQ(log_.commands, (std::vector<Command>{{"SET", "a", "1"},
                                                   {"SET", "b", "2", "PXAT", "1005000"},
                                                   {"PEXPIREAT", "a", "1001000"},
                                                   {"DEL", "a"}}));
}

TEST_F(StoreWriteLogTest, ReadsLogNothingUnlessAKeyExpires) {
    store_.set("a", "1", now_ + 1s);
    log_.commands.clear();
    store_.get("a");
    store_.ttl("a");
    EXPECT_TRUE(log_.commands.empty());

    now_ += 1s;
    store_.get("a");
    EXPECT_EQ(log_.commands, (std::vector<Command>{{"DEL", "a"}}));
}

TEST_F(StoreWriteLogTest, EvictionIsLoggedBeforeTheWriteThatCausedIt) {
    Store store = make_store(1);
    store.set_write_log(&log_);
    store.set("old", "1");
    store.set("new", "2");
    EXPECT_EQ(log_.commands,
              (std::vector<Command>{{"SET", "old", "1"}, {"DEL", "old"}, {"SET", "new", "2"}}));
}

TEST(AofRoundTripTest, ReplayRestoresValuesAndTtls) {
    const TempDir dir;
    const std::string path = dir.file("appendonly.aof");
    {
        Store store;
        CommandDispatcher dispatcher(store);
        AofWriter writer(path, FsyncPolicy::Always);
        store.set_write_log(&writer);
        dispatcher.execute({"SET", "plain", "v"});
        dispatcher.execute({"SET", "volatile", "v", "EX", "100"});
        dispatcher.execute({"SET", "gone", "v"});
        dispatcher.execute({"DEL", "gone"});
        dispatcher.execute({"SET", "later", "v"});
        dispatcher.execute({"EXPIRE", "later", "200"});
    }

    Store restored;
    CommandDispatcher dispatcher(restored);
    const AofLoadResult loaded =
        load_aof(path, [&dispatcher](const Command& command) { dispatcher.execute(command); });
    EXPECT_EQ(loaded.commands, 6U);
    EXPECT_EQ(restored.get("plain"), "v");
    EXPECT_EQ(restored.ttl("plain").state, Ttl::State::NoExpiry);
    EXPECT_EQ(restored.get("gone"), std::nullopt);
    const Ttl volatile_ttl = restored.ttl("volatile");
    ASSERT_EQ(volatile_ttl.state, Ttl::State::Expiring);
    EXPECT_GT(volatile_ttl.remaining, 90s);
    EXPECT_LE(volatile_ttl.remaining, 100s);
    EXPECT_GT(restored.ttl("later").remaining, 190s);
}

}  // namespace
}  // namespace miniredis
