#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <viam/sdk/common/instance.hpp>
#include "audio_stream.hpp"
#include "microphone.hpp"

using namespace audio;
using namespace viam::sdk;

class AudioBufferTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};


class AudioBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
        buffer_ = std::make_unique<AudioBuffer>(info, 1);  // 1 second buffer
    }

    void TearDown() override {
        buffer_.reset();
    }

    std::unique_ptr<AudioBuffer> buffer_;
};


TEST_F(AudioBufferTest, WriteAndReadSamples) {
    std::vector<int16_t> test_samples = {100, 200, 300, 400, 500};

    for (auto sample : test_samples) {
        buffer_->write_sample(sample);
    }
    EXPECT_EQ(buffer_->get_write_position(), test_samples.size());

    std::vector<int16_t> read_buffer(test_samples.size());
    uint64_t read_pos = 0;
    int samples_read = buffer_->read_samples(read_buffer.data(), test_samples.size(), read_pos);

    EXPECT_EQ(samples_read, test_samples.size());
    EXPECT_EQ(read_pos, test_samples.size());  // Position should have advanced
    EXPECT_EQ(read_buffer, test_samples);
}


TEST_F(AudioBufferTest, ReadPartialSamples) {
    // Write 100 samples
    const int num_samples = 100;
    for (int i = 0; i < num_samples; i++) {
        buffer_->write_sample(static_cast<int16_t>(i));
    }

    // Read only 50 samples
    std::vector<int16_t> buffer(50);
    uint64_t read_pos = 0;
    int samples_read = buffer_->read_samples(buffer.data(), 50, read_pos);

    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);  // Position advanced to 50

    // Read remaining 50 (continue from position 50)
    samples_read = buffer_->read_samples(buffer.data(), 50, read_pos);
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 100);  // Position now at 100
}


TEST_F(AudioBufferTest, ConcurrentWriteAndRead) {
    std::atomic<bool> stop{false};
    std::atomic<int> read_total{0};

    const int total_samples = 1000;

    // Producer thread (simulates RT audio callback)
    std::thread producer([&]() {
        for (int i = 0; i < total_samples; i++) {
            buffer_->write_sample(static_cast<int16_t>(i));
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Consumer thread (simulates get_audio)
    std::thread consumer([&]() {
        std::vector<int16_t> buffer(100);
        uint64_t my_read_pos = 0;

        // Keep reading until stopped AND all samples consumed
        while (!stop.load() || my_read_pos < buffer_->get_write_position()) {
            uint64_t write_pos = buffer_->get_write_position();
            uint64_t available = write_pos - my_read_pos;

            if (available > 0) {
                int to_read = std::min(available, static_cast<uint64_t>(100));
                int samples_read = buffer_->read_samples(buffer.data(), to_read, my_read_pos);
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

TEST_F(AudioBufferTest, ReadMoreThanAvailable) {
    // Write only 50 samples
    const int num_samples = 50;
    for (int i = 0; i < num_samples; i++) {
        buffer_->write_sample(static_cast<int16_t>(i));
    }

    // Try to read 100 samples
    std::vector<int16_t> buffer(100);
    uint64_t read_pos = 0;
    int samples_read = buffer_->read_samples(buffer.data(), 100, read_pos);

    // Should only get the 50 available samples
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);
}

TEST_F(AudioBufferTest, ReadSampleNotYetWritten) {
     // Write only 50 samples
    const int num_samples = 50;
    for (int i = 0; i < num_samples; i++) {
        buffer_->write_sample(static_cast<int16_t>(i));
    }

    // trying to read from a position that hasn't been written yet
    std::vector<int16_t> buffer(100);
    uint64_t read_pos = 100;
    int samples_read = buffer_->read_samples(buffer.data(), 100, read_pos);

    // should get 0 samples
    EXPECT_EQ(samples_read, 0);
    EXPECT_EQ(read_pos, 100);
}

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
    context_->reset();

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new AudioBufferTestEnvironment);
    return RUN_ALL_TESTS();
}
