#include "store/store.h"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace miniredis {
namespace {

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

}  // namespace
}  // namespace miniredis
