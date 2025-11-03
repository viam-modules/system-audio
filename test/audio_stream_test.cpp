#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <viam/sdk/common/instance.hpp>
#include "microphone.hpp"
#include "test_utils.hpp"

using namespace audio;
using namespace viam::sdk;


class InputStreamTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};


class InputStreamContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a basic input stream context for testing
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
        samples_per_chunk_ = 4410;
        context_ = std::make_unique<InputStreamContext>(info, samples_per_chunk_);
    }

    void TearDown() override {
        test_utils::ClearAudioBuffer(*context_);
    }

    // Helper to create a test chunk
    AudioIn::audio_chunk CreateTestChunk(int sequence, int64_t start_ns = 0) {
        AudioIn::audio_chunk chunk;
        chunk.start_timestamp_ns = std::chrono::nanoseconds(start_ns);
        chunk.end_timestamp_ns = std::chrono::nanoseconds(start_ns + 100000000); // +100ms
        chunk.info = context_->info;

        // Create some dummy audio data (100 samples of int16)
        chunk.audio_data.resize(100 * sizeof(int16_t));
        int16_t* samples = reinterpret_cast<int16_t*>(chunk.audio_data.data());
        for (int i = 0; i < 100; i++) {
            samples[i] = static_cast<int16_t>(i * 100 + sequence);
        }

        return chunk;
    }

    std::unique_ptr<audio::InputStreamContext> context_;
    int samples_per_chunk_;
};


TEST_F(InputStreamContextTest, MultipleReadersIndependent) {
    // Write samples to circular buffer
    const int num_samples = 100;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    EXPECT_EQ(context_->get_write_position(), num_samples);

    // Reader 1: Reads all samples
    std::vector<int16_t> buffer1(num_samples);
    uint64_t read_pos1 = 0;
    int samples_read1 = context_->read_samples(buffer1.data(), num_samples, read_pos1);
    EXPECT_EQ(samples_read1, num_samples);
    EXPECT_EQ(read_pos1, num_samples);

    // Reader 2: Can also read the same samples (independent position)
    std::vector<int16_t> buffer2(num_samples);
    uint64_t read_pos2 = 0;
    int samples_read2 = context_->read_samples(buffer2.data(), num_samples, read_pos2);
    EXPECT_EQ(samples_read2, num_samples);
    EXPECT_EQ(read_pos2, num_samples);

    // Both readers got the same data
    EXPECT_EQ(buffer1, buffer2);
}



TEST_F(InputStreamContextTest, CalculateSampleTimestamp) {
    // Set up the baseline time
    context_->first_sample_adc_time = 1000.0;
    context_->stream_start_time = std::chrono::system_clock::now();
    context_->first_callback_captured.store(true);
    test_utils::ClearAudioBuffer(*context_);

    auto baseline_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        context_->stream_start_time.time_since_epoch()
    ).count();

    // Test timestamp for sample 0
    auto timestamp1 = context_->calculate_sample_timestamp(0);
    EXPECT_EQ(timestamp1.count(), baseline_ns);

    // Test timestamp for sample at 1 second (44100 samples at 44.1kHz)
    auto timestamp2 = context_->calculate_sample_timestamp(44100);
    EXPECT_NEAR(timestamp2.count(), baseline_ns + 1'000'000'000, 1000);  // ~1 second

    // Test timestamp for sample at 0.5 seconds (22050 samples)
    auto timestamp3 =  context_->calculate_sample_timestamp(22050);
    EXPECT_NEAR(timestamp3.count(), baseline_ns + 500'000'000, 1000);  // ~0.5 seconds
}



// OutputStreamContext tests
class OutputStreamContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};
        context_ = std::make_unique<OutputStreamContext>(info, 30);  // 30 second buffer
    }

    void TearDown() override {
        context_.reset();
    }

    std::unique_ptr<audio::OutputStreamContext> context_;
};

TEST_F(OutputStreamContextTest, PlaybackPositionInitializedToZero) {
    EXPECT_EQ(context_->playback_position.load(), 0);
}

TEST_F(OutputStreamContextTest, WriteAndReadWithPlaybackPosition) {
    const int num_samples = 500;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }
    std::vector<int16_t> buffer(num_samples);
    uint64_t playback_pos = context_->playback_position.load();
    EXPECT_EQ(playback_pos, 0);

    int samples_read = context_->read_samples(buffer.data(), num_samples, playback_pos);
    EXPECT_EQ(samples_read, num_samples);

    context_->playback_position.store(playback_pos);
    EXPECT_EQ(context_->playback_position.load(), num_samples);
}

TEST_F(OutputStreamContextTest, PlaybackPositionTracksProgress) {
    const int total_samples = 1000;
    for (int i = 0; i < total_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    std::vector<int16_t> buffer(100);
    uint64_t playback_pos = context_->playback_position.load();

    for (int chunk = 0; chunk < 3; chunk++) {
        int samples_read = context_->read_samples(buffer.data(), 100, playback_pos);
        EXPECT_EQ(samples_read, 100);
        context_->playback_position.store(playback_pos);
    }

    EXPECT_EQ(context_->playback_position.load(), 300);
}

TEST_F(OutputStreamContextTest, MultipleReadersWithSharedPlaybackPosition) {
    const int num_samples = 200;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i * 10));
    }

    std::vector<int16_t> buffer1(100);
    uint64_t playback_pos = context_->playback_position.load();
    int samples_read1 = context_->read_samples(buffer1.data(), 100, playback_pos);
    context_->playback_position.store(playback_pos);

    EXPECT_EQ(samples_read1, 100);
    EXPECT_EQ(context_->playback_position.load(), 100);

    std::vector<int16_t> buffer2(100);
    playback_pos = context_->playback_position.load();
    int samples_read2 = context_->read_samples(buffer2.data(), 100, playback_pos);
    context_->playback_position.store(playback_pos);

    EXPECT_EQ(samples_read2, 100);
    EXPECT_EQ(context_->playback_position.load(), 200);

    EXPECT_EQ(buffer1[0], 0);
    EXPECT_EQ(buffer2[0], 1000);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnZeroNumChannels) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 0};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, 30);
    }, std::invalid_argument);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnNegativeNumChannels) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, -1};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, 30);
    }, std::invalid_argument);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnZeroSampleRate) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 0, 2};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, 30);
    }, std::invalid_argument);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnNegativeSampleRate) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, -48000, 2};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, 30);
    }, std::invalid_argument);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnZeroBufferDuration) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, 0);
    }, std::invalid_argument);
}

TEST_F(OutputStreamContextTest, OutputStreamContextThrowsOnNegativeBufferDuration) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};

    EXPECT_THROW({
        audio::OutputStreamContext ctx(info, -10);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnZeroNumChannels) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 0};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info,10);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnNegativeNumChannels) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, -1};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info, 10);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnZeroSampleRate) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 0, 2};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info, 10);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnNegativeSampleRate) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, -44100, 2};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info, 10);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnZeroBufferDuration) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 2};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info, 0);
    }, std::invalid_argument);
}

TEST_F(InputStreamContextTest, InputStreamContextThrowsOnNegativeBufferDuration) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 2};

    EXPECT_THROW({
        audio::InputStreamContext ctx(info, -5);
    }, std::invalid_argument);
}


 class AudioCallbackTest : public ::testing::Test {
  protected:
      void SetUp() override {
          // Create test audio info
          test_info = viam::sdk::audio_info{
              .codec = viam::sdk::audio_codecs::PCM_16,
              .sample_rate_hz = 44100,
              .num_channels = 1
          };

          // Small chunk size for testing (100 frames at 44.1kHz = ~2.3ms)
          samples_per_chunk = 100;

          // Create ring buffer context (10 second buffer)
          ctx = std::make_unique<microphone::AudioStreamContext>(
              test_info,
              samples_per_chunk,
              10  // 10 seconds buffer
          );

          // Create mock time info
          mock_time_info.inputBufferAdcTime = 0.0;
          mock_time_info.currentTime = 0.0;
          mock_time_info.outputBufferDacTime = 0.0;
      }

      std::vector<int16_t> create_test_samples(int count, int16_t value = 16383) {
          return std::vector<int16_t>(count, value);
      }

      int call_callback(const std::vector<int16_t>& samples) {
          return microphone::AudioCallback(
              samples.data(),      // inputBuffer
              nullptr,             // outputBuffer
              samples.size() / ctx->info.num_channels,  // framesPerBuffer (not total samples)
              &mock_time_info,     // timeInfo
              0,                   // statusFlags
              ctx.get()            // userData
          );
      }

      viam::sdk::audio_info test_info;
      int samples_per_chunk;
      std::unique_ptr<microphone::AudioStreamContext> ctx;
      PaStreamCallbackTimeInfo mock_time_info;
  };


  TEST_F(AudioCallbackTest, WritesSamplesToCircularBuffer) {
      // Create test samples
      std::vector<int16_t> samples = {100, 200, 300, 400, 500};

      // Call the callback
      int result = call_callback(samples);

      // Verify callback returned paContinue
      EXPECT_EQ(result, paContinue);

      // Verify samples were written to circular buffer
      EXPECT_EQ(ctx->get_write_position(), samples.size());

      // Read samples back and verify they match
      std::vector<int16_t> read_buffer(samples.size());
      uint64_t read_pos = 0;
      int samples_read = ctx->read_samples(read_buffer.data(), samples.size(), read_pos);

      EXPECT_EQ(samples_read, samples.size());
      EXPECT_EQ(read_buffer, samples);
  }

  TEST_F(AudioCallbackTest, TracksFirstCallbackTime) {
      std::vector<int16_t> samples = create_test_samples(100);
      EXPECT_FALSE(ctx->first_callback_captured.load());
      call_callback(samples);
      EXPECT_TRUE(ctx->first_callback_captured.load());
      EXPECT_EQ(ctx->first_sample_adc_time, mock_time_info.inputBufferAdcTime);
  }

  TEST_F(AudioCallbackTest, TracksSamplesWritten) {
      std::vector<int16_t> samples = create_test_samples(100);

      EXPECT_EQ(ctx->total_samples_written.load(), 0);
      call_callback(samples);
      EXPECT_EQ(ctx->total_samples_written.load(), 100);
      call_callback(samples);
      EXPECT_EQ(ctx->total_samples_written.load(), 200);
  }

  TEST_F(AudioCallbackTest, HandlesNullInputBuffer) {
      int result = microphone::AudioCallback(
          nullptr,           // null input buffer
          nullptr,
          100,
          &mock_time_info,
          0,
          ctx.get()
      );

      // Should return paContinue and not write anything
      EXPECT_EQ(result, paContinue);
      EXPECT_EQ(ctx->get_write_position(), 0);
  }

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
