#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <viam/sdk/common/utils.hpp>

namespace audio {
namespace utils {

constexpr int MAX_RESTART_ATTEMPTS = 3;
constexpr std::chrono::milliseconds POLL_INTERVAL{200};
constexpr uint64_t STALL_THRESHOLD_MS = 2000;
// Once the attempts budget is exhausted, the watchdog stops attempting fast restarts and
// instead retries every BACKOFF_INTERVAL_MS. This supports hot-replug scenarios: the
// device may come back later (USB plug-in, driver recovery) and we want to resume
// recovery without forcing the user to reconfigure. Each backoff retry costs the same
// as a normal restart attempt — milliseconds — so the long interval is purely about
// not flooding logs while waiting.
constexpr uint64_t BACKOFF_INTERVAL_MS = 5000;

// Background watchdog that polls an audio component's `last_callback_time_ns` and
// triggers a restart when the callback has gone silent for too long. Used by both
// Speaker and Microphone — the component-specific bits are passed in as callbacks.
//
// ContextT is the concrete audio context type (InputStreamContext / OutputStreamContext).
// It must expose a `std::atomic<uint64_t> last_callback_time_ns` member — both subclasses
// inherit one from AudioBuffer.
//
// Lifecycle: construct → start() → (optional) stop() → destruct. start() and stop() are
// not thread-safe; call them from the owning component only. The destructor joins the
// poll thread.
template <typename ContextT>
class StallWatchdog {
   public:
    // Returns the currently active audio context, or nullptr if no stream is up.
    using GetContextFn = std::function<std::shared_ptr<ContextT>()>;

    // Returns the current consecutive-failed-restart count. Watchdog stops calling
    // restart_fn once this reaches MAX_RESTART_ATTEMPTS; the latch clears automatically
    // once the count drops back below max (e.g. after a reconfigure).
    using GetAttemptsFn = std::function<int()>;

    // Performs the restart. Argument is the same context get_context returned a moment
    // earlier — the component should bail if it has since been swapped (reconfigure /
    // peer restart).
    using RestartFn = std::function<void(const std::shared_ptr<ContextT>&)>;

    StallWatchdog(GetContextFn get_context, GetAttemptsFn get_attempts, RestartFn restart_fn, std::string log_prefix)
        : get_context_(std::move(get_context)),
          get_attempts_(std::move(get_attempts)),
          restart_fn_(std::move(restart_fn)),
          log_prefix_(std::move(log_prefix)) {}

    ~StallWatchdog() { stop(); }

    // Spins up the poll thread. Call after the owning component has finished
    // constructing its first stream so the first poll sees valid state.
    void start() {
        thread_ = std::thread([this] { loop(); });
    }

    // Signals stop and joins the poll thread. Safe to call multiple times.
    // The destructor calls this automatically; explicit stop() is for callers
    // that need to control teardown ordering.
    void stop() {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

   private:
    void loop() {
        while (!stop_.load()) {
            std::this_thread::sleep_for(POLL_INTERVAL);
            if (stop_.load()) {
                return;
            }

            const std::shared_ptr<ContextT> ctx = get_context_();
            if (!ctx) {
                continue;
            }

            const uint64_t last_cb = ctx->last_callback_time_ns.load();
            if (last_cb == 0) {
                // Callback hasn't fired yet — stream just opened, give it time.
                continue;
            }

            const uint64_t now_ns = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            if (now_ns <= last_cb) {
                continue;
            }
            const uint64_t stale_ms = (now_ns - last_cb) / audio::NS_PER_MS;
            if (stale_ms <= STALL_THRESHOLD_MS) {
                continue;
            }

            const int attempts = get_attempts_();
            if (attempts >= MAX_RESTART_ATTEMPTS) {
                // Budget exhausted — back off to slow retries instead of giving up
                // permanently, so hot-replug (device unplugged then plugged back in)
                // recovers automatically without a reconfigure.
                if (now_ns - last_attempt_ns_.load() < BACKOFF_INTERVAL_MS * audio::NS_PER_MS) {
                    if (!backoff_logged_.exchange(true)) {
                        VIAM_SDK_LOG(warn) << log_prefix_ << " Restart budget exhausted; backing off to "
                                           << BACKOFF_INTERVAL_MS / 1000 << "s retries until the device returns";
                    }
                    continue;
                }
                VIAM_SDK_LOG(info) << log_prefix_ << " Backoff retry (attempts=" << attempts << "); checking for device";
            } else {
                // Attempts is below max — clear the backoff latch so a future exhaustion
                // logs the "backing off" message again.
                backoff_logged_.store(false);
                VIAM_SDK_LOG(warn) << log_prefix_ << " Callback stale for " << stale_ms << "ms, attempting restart";
            }

            last_attempt_ns_.store(now_ns);
            try {
                restart_fn_(ctx);
            } catch (const std::exception& e) {
                VIAM_SDK_LOG(error) << log_prefix_ << " restart_fn threw: " << e.what();
            }
        }
    }

    GetContextFn get_context_;
    GetAttemptsFn get_attempts_;
    RestartFn restart_fn_;
    std::string log_prefix_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    // Latches once the watchdog has logged the "backing off" message after the attempts
    // budget was exhausted, so we don't spam the log every poll while waiting for backoff.
    // Cleared automatically once get_attempts drops back below MAX_RESTART_ATTEMPTS
    // (typically after a successful restart resets the counter).
    std::atomic<bool> backoff_logged_{false};
    // Wall-clock time of the most recent restart attempt, used to enforce the
    // BACKOFF_INTERVAL_MS gap once the attempts budget is exhausted. Initialized to 0
    // so the very first stale-detection always fires immediately.
    std::atomic<uint64_t> last_attempt_ns_{0};
};

}  // namespace utils
}  // namespace audio
