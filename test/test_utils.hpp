#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "../src/audio_stream.hpp"
#include "../src/device_id.hpp"
#include "../src/portaudio.hpp"
#include "../src/watchdog.hpp"
#include "portaudio.h"

namespace test_utils {

// Common test constants
constexpr int DEFAULT_DEVICE_SAMPLE_RATE = 44100;  // Device's native/default sample rate in tests

// Steady-clock "now" expressed as nanoseconds since the clock's epoch — same idiom as
// the production code uses for last_callback_time_ns. Useful for tests that need to
// fabricate stale timestamps.
inline uint64_t now_ns() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

// Sleeps long enough for the watchdog's poll thread to wake at least once and run its
// staleness check + restart_fn. Use this in tests that set last_callback_time_ns to a
// stale value and then need to assert on the watchdog's reaction.
inline void wait_one_poll() {
    std::this_thread::sleep_for(audio::utils::POLL_INTERVAL + std::chrono::milliseconds(150));
}

// Marks the given audio context's `last_callback_time_ns` to be `ms_ago` milliseconds
// in the past, so the watchdog will see it as stale on its next poll. Works with any
// type that exposes a std::atomic<uint64_t> last_callback_time_ns member — both the
// real AudioBuffer subclasses and the FakeContext used in watchdog_test.
template <typename Ctx>
inline void mark_callback_stale(const std::shared_ptr<Ctx>& ctx, uint64_t ms_ago = 5000) {
    const uint64_t now = now_ns();
    if (ms_ago * audio::NS_PER_MS <= now) {
        ctx->last_callback_time_ns.store(now - ms_ago * audio::NS_PER_MS);
    }
}

// Shared test environment for audio tests
// Manages the viam::sdk::Instance lifecycle for all tests
class AudioTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};

/**
 * Mock PortAudio implementation using gmock.
 * Can be used by both microphone_test and speaker_test.
 */
class MockPortAudio : public audio::portaudio::PortAudioInterface {
public:
    MOCK_METHOD(PaError, initialize, (), (const, override));
    MOCK_METHOD(PaDeviceIndex, getDefaultInputDevice, (), (const, override));
    MOCK_METHOD(PaDeviceIndex, getDefaultOutputDevice, (), (const, override));
    MOCK_METHOD(const PaDeviceInfo*, getDeviceInfo, (PaDeviceIndex device), (const, override));
    MOCK_METHOD(PaError, openStream, (PaStream** stream, const PaStreamParameters* inputParameters,
                                      const PaStreamParameters* outputParameters, double sampleRate,
                                      unsigned long framesPerBuffer, PaStreamFlags streamFlags,
                                      PaStreamCallback* streamCallback, void* userData), (const, override));
    MOCK_METHOD(PaError, startStream, (PaStream* stream), (const, override));
    MOCK_METHOD(PaError, terminate, (), (const, override));
    MOCK_METHOD(PaError, stopStream, (PaStream* stream), (const, override));
    MOCK_METHOD(PaError, abortStream, (PaStream* stream), (const, override));
    MOCK_METHOD(PaError, closeStream, (PaStream* stream), (const, override));
    MOCK_METHOD(PaDeviceIndex, getDeviceCount, (), (const, override));
    MOCK_METHOD(const PaStreamInfo*, getStreamInfo, (PaStream* stream), (const, override));
    MOCK_METHOD(PaError, isFormatSupported, (const PaStreamParameters* inputParameters,
                                             const PaStreamParameters* outputParameters,
                                             double sampleRate), (const, override));
};

/**
 * Mock DeviceIdResolver for exercising discovery without touching Core Audio
 * or sysfs. Tests program it with EXPECT_CALL / ON_CALL like any other mock.
 */
class MockDeviceIdResolver : public audio::device_id::DeviceIdResolver {
public:
    MOCK_METHOD(std::string, resolve, (PaDeviceIndex index, const PaDeviceInfo& info), (const, override));
};

// Base test fixture with common PortAudio mock setup
class AudioTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        mock_pa_ = std::make_unique<::testing::NiceMock<test_utils::MockPortAudio>>();

        // Setup mock device info with common defaults
        mock_device_info_.defaultLowInputLatency = 0.01;
        mock_device_info_.defaultLowOutputLatency = 0.01;
        mock_device_info_.defaultSampleRate = DEFAULT_DEVICE_SAMPLE_RATE;
        mock_device_info_.maxInputChannels = 2;
        mock_device_info_.maxOutputChannels = 2;
        mock_device_info_.name = testDeviceName;

        SetupDefaultPortAudioBehavior();
    }

    void TearDown() override {
        mock_pa_.reset();
    }

    void SetupDefaultPortAudioBehavior() {
        using ::testing::_;
        using ::testing::Return;

        // setting up mock default behaviors
        ON_CALL(*mock_pa_, getDefaultInputDevice())
            .WillByDefault(Return(0));
        ON_CALL(*mock_pa_, getDeviceInfo(_))
            .WillByDefault(Return(&mock_device_info_));
        ON_CALL(*mock_pa_, getDeviceCount())
            .WillByDefault(Return(1));
        ON_CALL(*mock_pa_, openStream(_, _, _, _, _, _, _, _))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, startStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, stopStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, abortStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, closeStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, getStreamInfo(_))
            .WillByDefault(Return(nullptr));
        ON_CALL(*mock_pa_, isFormatSupported(_, _, _))
            .WillByDefault(Return(paNoError));
    }

    // Common test device name used across all tests
    static constexpr const char* testDeviceName = "Test Device";

    std::unique_ptr<::testing::NiceMock<test_utils::MockPortAudio>> mock_pa_;
    PaDeviceInfo mock_device_info_;
};

// Helper function to clear an AudioBuffer - resets all samples and write position
inline void ClearAudioBuffer(audio::AudioBuffer& buffer) {
    buffer.total_samples_written.store(0);
    for (int i = 0; i < buffer.buffer_capacity; i++) {
        buffer.audio_buffer[i].store(0);
    }
}

} // namespace test_utils
