#include <cstring>
#include <stdexcept>
#include "mp3_encoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace microphone {

// Helper function to de-interleave audio samples
// Input: interleaved samples [L0, R0, L1, R1, ...]
// Output: planar format - plane[0]: [L0, L1, ...], plane[1]: [R0, R1, ...]
static void deinterleave_samples(const int16_t* interleaved,
                                  AVFrame* frame,
                                  int frame_size,
                                  int num_channels) {
    for (int ch = 0; ch < num_channels; ch++) {
        int16_t* plane = reinterpret_cast<int16_t*>(frame->data[ch]);
        for (int i = 0; i < frame_size; i++) {
            plane[i] = interleaved[i * num_channels + ch];
        }
    }
}

void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels) {
    ctx.sample_rate = sample_rate;
    ctx.num_channels = num_channels;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!codec) {
        throw std::runtime_error("MP3 encoder not found");
    }

    CleanupPtr<avcodec_context_cleanup> ffmpeg_ctx(avcodec_alloc_context3(codec));
    if (!ffmpeg_ctx) {
        throw std::runtime_error("Could not allocate MP3 encoder context");
    }
    ctx.ffmpeg_ctx = std::move(ffmpeg_ctx);

    // Configure encoder
    ctx.ffmpeg_ctx->sample_rate = sample_rate;
    av_channel_layout_default(&ctx.ffmpeg_ctx->ch_layout, num_channels);
    // describes the format you're feeding into the encoder
    // encoder explicitly supports planar 16-bit
    ctx.ffmpeg_ctx->sample_fmt = AV_SAMPLE_FMT_S16P;
    ctx.ffmpeg_ctx->bit_rate = 192000;     // target mp3 bit rate
    ctx.ffmpeg_ctx->frame_size = 1152;  // Standard MP3 frame size

    if (avcodec_open2(ctx.ffmpeg_ctx.get(), codec, nullptr) < 0) {
        throw std::runtime_error("Could not open MP3 encoder");
    }

    // Allocate reusable frame
    CleanupPtr<avframe_cleanup> frame(av_frame_alloc());
    if (!frame) {
        throw std::runtime_error("Could not allocate MP3 frame");
    }
    ctx.frame = std::move(frame);

    ctx.frame->nb_samples = ctx.ffmpeg_ctx->frame_size;
    ctx.frame->format = ctx.ffmpeg_ctx->sample_fmt;
    av_channel_layout_copy(&ctx.frame->ch_layout, &ctx.ffmpeg_ctx->ch_layout);

    if (av_frame_get_buffer(ctx.frame.get(), 0) < 0) {
        throw std::runtime_error("Could not allocate MP3 frame buffer");
    }

    VIAM_SDK_LOG(info) << "MP3 encoder initialized: " << sample_rate
                       << "Hz, " << num_channels << " channels, "
                       << ctx.ffmpeg_ctx->frame_size << " samples";
}

void encode_mp3_samples(MP3EncoderContext& ctx,
                        const int16_t* samples,
                        int sample_count,
                        std::vector<uint8_t>& output_data) {
    if (!ctx.ffmpeg_ctx || !ctx.frame) {
        throw std::runtime_error("MP3 encoder not initialized");
    }

    ctx.buffer.insert(ctx.buffer.end(), samples, samples + sample_count);

    size_t samples_per_frame = static_cast<size_t>(ctx.ffmpeg_ctx->frame_size * ctx.num_channels);

    // Encode as many complete frames as we have buffered
    while (ctx.buffer.size() >= samples_per_frame) {
        // De-interleave samples into planar format
        deinterleave_samples(ctx.buffer.data(), ctx.frame.get(),
                           ctx.ffmpeg_ctx->frame_size, ctx.num_channels);

        // Send frame to encoder
        int ret = avcodec_send_frame(ctx.ffmpeg_ctx.get(), ctx.frame.get());
        if (ret < 0) {
            throw std::runtime_error("Error sending frame to MP3 encoder");
        }

        // Remove encoded samples from buffer
        ctx.buffer.erase(ctx.buffer.begin(), ctx.buffer.begin() + samples_per_frame);

        // Receive encoded packets (may get multiple or none due to encoder buffering)
        CleanupPtr<avpacket_cleanup> pkt(av_packet_alloc());
        if (!pkt) {
            throw std::runtime_error("Could not allocate packet");
        }

        while ((ret = avcodec_receive_packet(ctx.ffmpeg_ctx.get(), pkt.get())) == 0) {
            output_data.insert(output_data.end(), pkt->data, pkt->data + pkt->size);
            av_packet_unref(pkt.get());
        }

        // AVERROR(EAGAIN) is expected when encoder needs more frames
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            VIAM_SDK_LOG(warn) << "MP3 encoder error: " << ret;
        }
    }
}

int flush_mp3_encoder(MP3EncoderContext& ctx) {
    if (!ctx.ffmpeg_ctx) {
        return 0;
    }

    // Flush encoder by sending NULL frame
    avcodec_send_frame(ctx.ffmpeg_ctx.get(), nullptr);

    // Drain and count remaining packets
    CleanupPtr<avpacket_cleanup> pkt(av_packet_alloc());
    if (!pkt) {
        return 0;
    }

    int flushed_packets = 0;
    while (avcodec_receive_packet(ctx.ffmpeg_ctx.get(), pkt.get()) == 0) {
        flushed_packets++;
        av_packet_unref(pkt.get());
    }

    if (flushed_packets > 0) {
        VIAM_SDK_LOG(info) << "MP3 encoder flushed " << flushed_packets << " remaining packets";
    }

    if (!ctx.buffer.empty()) {
        VIAM_SDK_LOG(info) << "Discarded " << ctx.buffer.size() / ctx.num_channels
                           << " unbuffered samples at end of stream";
    }

    return flushed_packets;
}

void cleanup_mp3_encoder(MP3EncoderContext& ctx) {
    // Reset smart pointers
    ctx.frame.reset();
    ctx.ffmpeg_ctx.reset();

    ctx.buffer.clear();
    ctx.sample_rate = 0;
    ctx.num_channels = 0;
}

} // namespace microphone
