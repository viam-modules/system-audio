#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "test_utils.hpp"
#include "watchdog.hpp"

namespace {

// Minimal context type that satisfies the watchdog's only requirement on ContextT:
// a `std::atomic<uint64_t> last_callback_time_ns` member. Lets us test the watchdog
// without pulling in the full AudioBuffer / stream context machinery.
struct FakeContext {
    std::atomic<uint64_t> last_callback_time_ns{0};
};

// Helper: build a context whose callback last fired `ms_ago` milliseconds in the past.
std::shared_ptr<FakeContext> make_context_stale_by(uint64_t ms_ago) {
    auto ctx = std::make_shared<FakeContext>();
    test_utils::mark_callback_stale(ctx, ms_ago);
    return ctx;
}

}  // namespace

// 1. Restart fires when the callback is stale and attempts are below the budget.
TEST(StallWatchdog, RestartFiresWhenStale) {
    const auto ctx = make_context_stale_by(5000);  // 5s past threshold
    std::atomic<int> restart_calls{0};

    audio::utils::StallWatchdog<FakeContext> wd(
        [ctx]() { return ctx; },
        []() { return 0; },
        [&restart_calls](const std::shared_ptr<FakeContext>&) { restart_calls.fetch_add(1); },
        "[wd_test_1]");
    wd.start();

    test_utils::wait_one_poll();
    wd.stop();

    EXPECT_GE(restart_calls.load(), 1) << "restart_fn should have been called at least once";
}

// Once attempts hit MAX, the backoff gate prevents repeated calls within
//    BACKOFF_INTERVAL. The very first poll past MAX still fires (the watchdog needs
//    that to discover when the device returns), but subsequent polls within the backoff
//    window are skipped.
TEST(StallWatchdog, BackoffGateLimitsRestartsWhenAttemptsExhausted) {
    const auto ctx = make_context_stale_by(5000);
    std::atomic<int> restart_calls{0};

    audio::utils::StallWatchdog<FakeContext> wd(
        [ctx]() { return ctx; },
        []() { return audio::utils::MAX_RESTART_ATTEMPTS; },  // already at the cap
        [&restart_calls](const std::shared_ptr<FakeContext>&) { restart_calls.fetch_add(1); },
        "[wd_test_2]");
    wd.start();

    // Wait long enough for several poll cycles (200ms each). With attempts pinned at MAX
    // and BACKOFF_INTERVAL = 5000, only the very first retry should fire — every poll
    // for the next 5 seconds should hit the backoff gate and skip.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    wd.stop();

    EXPECT_EQ(restart_calls.load(), 1)
        << "expected exactly one backoff retry inside the BACKOFF_INTERVAL window; "
        << "subsequent polls should have been gated";
}

//   No restart when last_callback_time_ns == 0 (stream just opened, callback hasn't
//    fired yet). The watchdog should treat this as "give it more time", not as stalled.
TEST(StallWatchdog, NoRestartWhenCallbackNeverFired) {
    const auto ctx = std::make_shared<FakeContext>();  // last_callback_time_ns stays at 0
    std::atomic<int> restart_calls{0};

    audio::utils::StallWatchdog<FakeContext> wd(
        [ctx]() { return ctx; },
        []() { return 0; },
        [&restart_calls](const std::shared_ptr<FakeContext>&) { restart_calls.fetch_add(1); },
        "[wd_test_4]");
    wd.start();

    test_utils::wait_one_poll();
    wd.stop();

    EXPECT_EQ(restart_calls.load(), 0);
}

// No restart when the callback fired recently (well within threshold).
TEST(StallWatchdog, NoRestartWhenCallbackRecent) {
    const auto ctx = make_context_stale_by(100);  // 100ms old, threshold is 2000ms
    std::atomic<int> restart_calls{0};

    audio::utils::StallWatchdog<FakeContext> wd(
        [ctx]() { return ctx; },
        []() { return 0; },
        [&restart_calls](const std::shared_ptr<FakeContext>&) { restart_calls.fetch_add(1); },
        "[wd_test_5]");
    wd.start();

    test_utils::wait_one_poll();
    wd.stop();

    EXPECT_EQ(restart_calls.load(), 0);
}

// No restart when get_context returns nullptr (no active stream).
TEST(StallWatchdog, NoRestartWhenContextIsNull) {
    std::atomic<int> restart_calls{0};

    audio::utils::StallWatchdog<FakeContext> wd(
        []() { return std::shared_ptr<FakeContext>{}; },  // always null
        []() { return 0; },
        [&restart_calls](const std::shared_ptr<FakeContext>&) { restart_calls.fetch_add(1); },
        "[wd_test_6]");
    wd.start();

    test_utils::wait_one_poll();
    wd.stop();

    EXPECT_EQ(restart_calls.load(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
