#include "store/store.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace miniredis {
namespace {

using namespace std::chrono_literals;

std::size_t del(Store& store, std::vector<std::string> keys) {
    return store.del(keys.begin(), keys.end());
}

std::size_t exists(Store& store, std::vector<std::string> keys) {
    return store.exists(keys.begin(), keys.end());
}

TEST(StoreTest, GetMissingKeyIsEmpty) {
    Store store;
    EXPECT_EQ(store.get("missing"), std::nullopt);
}

TEST(StoreTest, SetThenGet) {
    Store store;
    store.set("key", "value");
    EXPECT_EQ(store.get("key"), "value");
}

TEST(StoreTest, SetOverwrites) {
    Store store;
    store.set("key", "first");
    store.set("key", "second");
    EXPECT_EQ(store.get("key"), "second");
    EXPECT_EQ(store.size(), 1U);
}

TEST(StoreTest, DelCountsOnlyExistingKeys) {
    Store store;
    store.set("a", "1");
    store.set("b", "2");
    EXPECT_EQ(del(store, {"a", "b", "missing"}), 2U);
    EXPECT_EQ(store.size(), 0U);
}

TEST(StoreTest, ExistsCountsDuplicatesLikeRedis) {
    Store store;
    store.set("a", "1");
    EXPECT_EQ(exists(store, {"a", "a", "missing"}), 2U);
}

TEST(StoreTest, ConcurrentWritersDoNotLoseKeys) {
    Store store;
    constexpr int kThreads = 8;
    constexpr int kKeysPerThread = 2000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&store, t] {
            for (int i = 0; i < kKeysPerThread; ++i) {
                const std::string key = std::to_string(t) + ":" + std::to_string(i);
                store.set(key, key);
                EXPECT_EQ(store.get(key), key);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(store.size(), static_cast<std::size_t>(kThreads * kKeysPerThread));
}

class ExpiringStoreTest : public ::testing::Test {
protected:
    Store make_store(std::size_t max_keys = 0) {
        StoreOptions options;
        options.max_keys = max_keys;
        options.now = [this] { return TimePoint(std::chrono::milliseconds(now_ms_.load())); };
        return Store(options);
    }

    TimePoint now() const { return TimePoint(std::chrono::milliseconds(now_ms_.load())); }
    void advance(std::chrono::milliseconds by) { now_ms_ += by.count(); }

    std::atomic<std::int64_t> now_ms_{1'000'000};
};

TEST_F(ExpiringStoreTest, KeyExpiresLazilyOnAccess) {
    Store store = make_store();
    store.set("k", "v", now() + 100ms);
    advance(99ms);
    EXPECT_EQ(store.get("k"), "v");
    advance(1ms);
    EXPECT_EQ(store.get("k"), std::nullopt);
    EXPECT_EQ(store.size(), 0U);
}

TEST_F(ExpiringStoreTest, ExpiredKeysAreInvisibleToEveryOperation) {
    Store store = make_store();
    store.set("k", "v", now() + 10ms);
    advance(10ms);
    EXPECT_EQ(exists(store, {"k"}), 0U);
    EXPECT_EQ(del(store, {"k"}), 0U);
    EXPECT_FALSE(store.expire_at("k", now() + 1000ms));
    EXPECT_EQ(store.ttl("k").state, Ttl::State::Missing);
}

TEST_F(ExpiringStoreTest, TtlReportsAllThreeStates) {
    Store store = make_store();
    EXPECT_EQ(store.ttl("missing").state, Ttl::State::Missing);
    store.set("plain", "v");
    EXPECT_EQ(store.ttl("plain").state, Ttl::State::NoExpiry);
    store.set("volatile", "v", now() + 5000ms);
    advance(1000ms);
    const Ttl ttl = store.ttl("volatile");
    EXPECT_EQ(ttl.state, Ttl::State::Expiring);
    EXPECT_EQ(ttl.remaining, 4000ms);
}

TEST_F(ExpiringStoreTest, SetWithoutTtlClearsPreviousTtl) {
    Store store = make_store();
    store.set("k", "v1", now() + 10ms);
    store.set("k", "v2");
    advance(1h);
    EXPECT_EQ(store.get("k"), "v2");
}

TEST_F(ExpiringStoreTest, ExpireAtUpdatesDeadline) {
    Store store = make_store();
    store.set("k", "v");
    EXPECT_TRUE(store.expire_at("k", now() + 50ms));
    EXPECT_TRUE(store.expire_at("k", now() + 500ms));
    advance(100ms);
    EXPECT_EQ(store.get("k"), "v");
    advance(400ms);
    EXPECT_EQ(store.get("k"), std::nullopt);
}

TEST_F(ExpiringStoreTest, DeadlineInThePastDeletesImmediately) {
    Store store = make_store();
    store.set("a", "v");
    EXPECT_TRUE(store.expire_at("a", now()));
    EXPECT_EQ(store.size(), 0U);

    store.set("b", "old");
    store.set("b", "new", now() - 1ms);
    EXPECT_EQ(store.get("b"), std::nullopt);
}

TEST_F(ExpiringStoreTest, SweepReclaimsKeysThatAreNeverRead) {
    Store store = make_store();
    for (int i = 0; i < 1000; ++i) {
        store.set("volatile" + std::to_string(i), "v", now() + 10ms);
    }
    for (int i = 0; i < 10; ++i) {
        store.set("persistent" + std::to_string(i), "v");
    }
    advance(10ms);
    // Only keys with a TTL are sampled, and each cycle has a time budget, so a few cycles suffice.
    for (int cycle = 0; cycle < 100 && store.size() > 10; ++cycle) {
        store.sweep_expired();
    }
    EXPECT_EQ(store.size(), 10U);
}

TEST_F(ExpiringStoreTest, SweepLeavesLiveKeysAlone) {
    Store store = make_store();
    store.set("soon", "v", now() + 1h);
    store.set("never", "v");
    EXPECT_EQ(store.sweep_expired(), 0U);
    EXPECT_EQ(store.size(), 2U);
}

TEST_F(ExpiringStoreTest, EvictsLeastRecentlyUsedKey) {
    Store store = make_store(3);
    store.set("a", "1");
    store.set("b", "2");
    store.set("c", "3");
    ASSERT_EQ(store.get("a"), "1");  // a is now the most recent; b is the oldest
    store.set("d", "4");

    EXPECT_EQ(store.size(), 3U);
    EXPECT_EQ(store.get("b"), std::nullopt);
    EXPECT_EQ(store.get("a"), "1");
    EXPECT_EQ(store.get("c"), "3");
    EXPECT_EQ(store.get("d"), "4");
}

TEST_F(ExpiringStoreTest, OverwritingAtCapacityDoesNotEvict) {
    Store store = make_store(2);
    store.set("a", "1");
    store.set("b", "2");
    store.set("a", "updated");
    EXPECT_EQ(store.size(), 2U);
    EXPECT_EQ(store.get("b"), "2");
}

TEST_F(ExpiringStoreTest, EvictionKeepsVolatileIndexConsistent) {
    Store store = make_store(2);
    store.set("a", "1", now() + 10ms);
    store.set("b", "2", now() + 10ms);
    store.set("c", "3", now() + 10ms);  // evicts a, which had a TTL
    advance(10ms);
    for (int cycle = 0; cycle < 100 && store.size() > 0; ++cycle) {
        store.sweep_expired();
    }
    EXPECT_EQ(store.size(), 0U);
}

}  // namespace
}  // namespace miniredis
