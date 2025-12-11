#pragma once

#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "../src/audio_stream.hpp"
#include "../src/portaudio.hpp"
#include "portaudio.h"

namespace test_utils {

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
    MOCK_METHOD(PaError, closeStream, (PaStream* stream), (const, override));
    MOCK_METHOD(PaDeviceIndex, getDeviceCount, (), (const, override));
    MOCK_METHOD(const PaStreamInfo*, getStreamInfo, (PaStream* stream), (const, override));
    MOCK_METHOD(PaError, isFormatSupported, (const PaStreamParameters* inputParameters,
                                             const PaStreamParameters* outputParameters,
                                             double sampleRate), (const, override));
};

// Base test fixture with common PortAudio mock setup
class AudioTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        mock_pa_ = std::make_unique<::testing::NiceMock<test_utils::MockPortAudio>>();

        // Setup mock device info with common defaults
        mock_device_info_.defaultLowInputLatency = 0.01;
        mock_device_info_.defaultLowOutputLatency = 0.01;
        mock_device_info_.defaultSampleRate = 44100.0;
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
    buffer.total_samples_written.store(0, std::memory_order_relaxed);
    for (int i = 0; i < buffer.buffer_capacity; i++) {
        buffer.audio_buffer[i].store(0, std::memory_order_relaxed);
    }
}

} // namespace test_utils
