#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <viam/sdk/common/instance.hpp>
#include "microphone.hpp"
#include "test_utils.hpp"

using namespace audio;
using namespace viam::sdk;
class AudioBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};
        buffer_ = std::make_unique<AudioBuffer>(info, 1);  // 1 second buffer
    }

    void TearDown() override {
        test_utils::ClearAudioBuffer(*buffer_);
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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
