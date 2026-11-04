#include "store/expiry_sweeper.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace miniredis {
namespace {

using namespace std::chrono_literals;

TEST(ExpirySweeperTest, ReclaimsExpiredKeysInTheBackground) {
    std::atomic<std::int64_t> now_ms{1'000'000};
    StoreOptions options;
    options.now = [&now_ms] { return TimePoint(std::chrono::milliseconds(now_ms.load())); };
    Store store(options);

    for (int i = 0; i < 100; ++i) {
        store.set("k" + std::to_string(i), "v", store.now() + 10ms);
    }
    store.set("persistent", "v");

    const ExpirySweeper sweeper(store, 5ms);
    now_ms += 10;

    const auto give_up = std::chrono::steady_clock::now() + 5s;
    while (store.size() > 1 && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_EQ(store.size(), 1U);
}

TEST(ExpirySweeperTest, StopsPromptlyOnDestruction) {
    Store store;
    const auto start = std::chrono::steady_clock::now();
    {
        const ExpirySweeper sweeper(store, 1h);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

}  // namespace
}  // namespace miniredis
