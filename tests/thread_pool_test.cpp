#include "server/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace miniredis {
namespace {

using namespace std::chrono_literals;

TEST(ThreadPoolTest, RejectsZeroThreads) {
    EXPECT_THROW(ThreadPool(0), std::invalid_argument);
}

TEST(ThreadPoolTest, RunsEverySubmittedTask) {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) {
            pool.submit([&counter] { ++counter; });
        }
    }
    EXPECT_EQ(counter, 1000);
}

TEST(ThreadPoolTest, RunsTasksInParallelUpToThreadCount) {
    constexpr int kThreads = 4;
    ThreadPool pool(kThreads);
    std::mutex mutex;
    std::condition_variable all_arrived;
    int arrived = 0;

    std::vector<std::future<void>> futures;
    for (int i = 0; i < kThreads; ++i) {
        futures.push_back(pool.submit([&] {
            std::unique_lock lock(mutex);
            ++arrived;
            all_arrived.notify_all();
            // Only completes if all kThreads tasks are running at the same time.
            ASSERT_TRUE(all_arrived.wait_for(lock, 5s, [&] { return arrived == kThreads; }));
        }));
    }
    for (auto& future : futures) {
        future.get();
    }
}

TEST(ThreadPoolTest, ShutdownDrainsQueuedTasks) {
    std::atomic<int> counter{0};
    ThreadPool pool(1);
    for (int i = 0; i < 50; ++i) {
        pool.submit([&counter] {
            std::this_thread::sleep_for(1ms);
            ++counter;
        });
    }
    pool.shutdown();
    EXPECT_EQ(counter, 50);
}

TEST(ThreadPoolTest, SubmitAfterShutdownThrows) {
    ThreadPool pool(1);
    pool.shutdown();
    EXPECT_THROW(pool.submit([] {}), std::runtime_error);
}

TEST(ThreadPoolTest, ExceptionReachesFutureAndWorkerSurvives) {
    ThreadPool pool(1);
    auto failing = pool.submit([] { throw std::runtime_error("boom"); });
    EXPECT_THROW(failing.get(), std::runtime_error);

    auto next = pool.submit([] {});
    EXPECT_EQ(next.wait_for(5s), std::future_status::ready);
}

TEST(ThreadPoolTest, AcceptsMoveOnlyTasks) {
    ThreadPool pool(1);
    auto value = std::make_unique<int>(42);
    int seen = 0;
    pool.submit([value = std::move(value), &seen] { seen = *value; }).get();
    EXPECT_EQ(seen, 42);
}

}  // namespace
}  // namespace miniredis
