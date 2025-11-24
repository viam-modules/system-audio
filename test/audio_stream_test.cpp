#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <viam/sdk/common/instance.hpp>
#include "audio_stream.hpp"

using namespace microphone;
using namespace viam::sdk;

class AudioStreamTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};

class AudioStreamContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a basic audio context for testing
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
        samples_per_chunk_ = 4410;
        context_ = std::make_unique<AudioStreamContext>(info, samples_per_chunk_);
    }

    void TearDown() override {
        context_.reset();
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

    std::unique_ptr<AudioStreamContext> context_;
    int samples_per_chunk_;
};

TEST_F(AudioStreamContextTest, WriteAndReadSamples) {
    // Write some test samples to circular buffer
    std::vector<int16_t> test_samples = {100, 200, 300, 400, 500};

    for (auto sample : test_samples) {
        context_->write_sample(sample);
    }
    EXPECT_EQ(context_->get_write_position(), test_samples.size());

    std::vector<int16_t> read_buffer(test_samples.size());
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(read_buffer.data(), test_samples.size(), read_pos);

    EXPECT_EQ(samples_read, test_samples.size());
    EXPECT_EQ(read_pos, test_samples.size());  // Position should have advanced
    EXPECT_EQ(read_buffer, test_samples);
}

TEST_F(AudioStreamContextTest, MultipleReadersIndependent) {
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


TEST_F(AudioStreamContextTest, ReadPartialSamples) {
    // Write 100 samples
    const int num_samples = 100;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // Read only 50 samples
    std::vector<int16_t> buffer(50);
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(buffer.data(), 50, read_pos);

    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);  // Position advanced to 50

    // Read remaining 50 (continue from position 50)
    samples_read = context_->read_samples(buffer.data(), 50, read_pos);
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 100);  // Position now at 100
}

TEST_F(AudioStreamContextTest, ConcurrentWriteAndRead) {
    std::atomic<bool> stop{false};
    std::atomic<int> read_total{0};

    const int total_samples = 1000;

    // Producer thread (simulates RT audio callback)
    std::thread producer([&]() {
        for (int i = 0; i < total_samples; i++) {
            context_->write_sample(static_cast<int16_t>(i));
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Consumer thread (simulates get_audio)
    std::thread consumer([&]() {
        std::vector<int16_t> buffer(100);
        uint64_t my_read_pos = 0;

        // Keep reading until stopped AND all samples consumed
        while (!stop.load() || my_read_pos < context_->get_write_position()) {
            uint64_t write_pos = context_->get_write_position();
            uint64_t available = write_pos - my_read_pos;

            if (available > 0) {
                int to_read = std::min(available, static_cast<uint64_t>(100));
                int samples_read = context_->read_samples(buffer.data(), to_read, my_read_pos);
                read_total += samples_read;
            } else {
                // No samples available yet, wait a bit
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    producer.join();
    stop = true;
    consumer.join();

    // All samples should have been read
    EXPECT_EQ(read_total.load(), total_samples);
}


TEST_F(AudioStreamContextTest, ReadMoreThanAvailable) {
    // Write only 50 samples
    const int num_samples = 50;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // Try to read 100 samples
    std::vector<int16_t> buffer(100);
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(buffer.data(), 100, read_pos);

    // Should only get the 50 available samples
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);
}

TEST_F(AudioStreamContextTest, ReadSampleNotYetWritten) {
     // Write only 50 samples
    const int num_samples = 50;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // trying to read from a position that hasn't been written yet
    std::vector<int16_t> buffer(100);
    uint64_t read_pos = 100;
    int samples_read = context_->read_samples(buffer.data(), 100, read_pos);

    // should get 0 samples
    EXPECT_EQ(samples_read, 0);
    EXPECT_EQ(read_pos, 100);
}


TEST_F(AudioStreamContextTest, CalculateSampleTimestamp) {
    // Set up the baseline time
    context_->first_sample_adc_time = 1000.0;
    context_->stream_start_time = std::chrono::steady_clock::now();
    context_->first_callback_captured.store(true);
    context_->total_samples_written.store(0);

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


 class AudioCallbackTest : public ::testing::Test {
  protected:
      void SetUp() override {
          // Create test audio info
          test_info = viam::sdk::audio_info{
              .codec = viam::sdk::audio_codecs::PCM_16,
              .sample_rate_hz = 44100,
              .num_channels = 1
          };

          samples_per_chunk = 100;

          // Create ring buffer context (10 second buffer)
          ctx = std::make_unique<microphone::AudioStreamContext>(
              test_info,
              samples_per_chunk,
              10
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
              samples.size() / ctx->info.num_channels,  // framesPerBuffer
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
      std::vector<int16_t> samples = {100, 200, 300, 400, 500};

      int result = call_callback(samples);

      EXPECT_EQ(result, paContinue);

      EXPECT_EQ(ctx->get_write_position(), samples.size());

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
    ::testing::AddGlobalTestEnvironment(new AudioStreamTestEnvironment);
    return RUN_ALL_TESTS();
}
