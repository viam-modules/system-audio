#pragma once

#include <cstdlib>
#include "portaudio.h"

namespace audio {
namespace portaudio {

class PortAudioInterface {
   public:
    virtual PaError initialize() const = 0;
    virtual PaDeviceIndex getDefaultInputDevice() const = 0;
    virtual PaDeviceIndex getDefaultOutputDevice() const = 0;
    virtual const PaDeviceInfo* getDeviceInfo(PaDeviceIndex device) const = 0;
    virtual PaError openStream(PaStream** stream,
                               const PaStreamParameters* inputParameters,
                               const PaStreamParameters* outputParameters,
                               double sampleRate,
                               unsigned long framesPerBuffer,
                               PaStreamFlags streamFlags,
                               PaStreamCallback* streamCallback,
                               void* userData) const = 0;
    virtual PaError startStream(PaStream* stream) const = 0;
    virtual PaError terminate() const = 0;
    virtual PaError stopStream(PaStream* stream) const = 0;
    virtual PaError closeStream(PaStream* stream) const = 0;
    virtual PaDeviceIndex getDeviceCount() const = 0;
    virtual const PaStreamInfo* getStreamInfo(PaStream* stream) const = 0;
    virtual PaError isFormatSupported(const PaStreamParameters* inputParameters,
                                      const PaStreamParameters* outputParameters,
                                      double sampleRate) const = 0;
    virtual ~PortAudioInterface() = default;
};

// Wrapper around portAudio functions so they can be mocked.
class RealPortAudio : public PortAudioInterface {
   public:
    PaError initialize() const override {
        return Pa_Initialize();
    }

    PaDeviceIndex getDefaultInputDevice() const override {
        return Pa_GetDefaultInputDevice();
    }

    PaDeviceIndex getDefaultOutputDevice() const override {
        return Pa_GetDefaultOutputDevice();
    }

    const PaDeviceInfo* getDeviceInfo(PaDeviceIndex device) const override {
        return Pa_GetDeviceInfo(device);
    }

    PaError openStream(PaStream** stream,
                       const PaStreamParameters* inputParameters,
                       const PaStreamParameters* outputParameters,
                       double sampleRate,
                       unsigned long framesPerBuffer,
                       PaStreamFlags streamFlags,
                       PaStreamCallback* streamCallback,
                       void* userData) const override {
        return Pa_OpenStream(stream, inputParameters, outputParameters, sampleRate, framesPerBuffer, streamFlags, streamCallback, userData);
    }

    PaError startStream(PaStream* stream) const override {
        return Pa_StartStream(stream);
    }

    PaError terminate() const override {
        return Pa_Terminate();
    }

    PaError stopStream(PaStream* stream) const override {
        return Pa_StopStream(stream);
    }

    PaError closeStream(PaStream* stream) const override {
        return Pa_CloseStream(stream);
    }

    PaDeviceIndex getDeviceCount() const override {
        return Pa_GetDeviceCount();
    }

    const PaStreamInfo* getStreamInfo(PaStream* stream) const override {
        return Pa_GetStreamInfo(stream);
    }

    PaError isFormatSupported(const PaStreamParameters* inputParameters,
                              const PaStreamParameters* outputParameters,
                              double sampleRate) const override {
        return Pa_IsFormatSupported(inputParameters, outputParameters, sampleRate);
    }
};

static inline void startPortAudio(const audio::portaudio::PortAudioInterface* pa = nullptr) {
    // In production pa is nullptr and real_pa is used. For testing, pa is the mock pa
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.initialize();
    if (err != 0) {
        std::ostringstream buffer;
        buffer << "Failed to initialize PortAudio library: " << Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[startPortAudio] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    int numDevices = Pa_GetDeviceCount();
    VIAM_SDK_LOG(info) << "Available input devices:";

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            VIAM_SDK_LOG(error) << "failed to get device info for " << i + 1 << "th device";
            continue;
        }
        if (info->maxInputChannels > 0) {
            VIAM_SDK_LOG(info) << info->name << " default sample rate: " << info->defaultSampleRate
                               << " max input channels: " << info->maxInputChannels;
        }
    }

    VIAM_SDK_LOG(info) << "Available output devices:";

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info) {
            VIAM_SDK_LOG(error) << "failed to get device info for " << i + 1 << "th device";
            continue;
        }
        if (info->maxOutputChannels > 0) {
            VIAM_SDK_LOG(info) << info->name << " default sample rate: " << info->defaultSampleRate
                               << " max input channels: " << info->maxOutputChannels;
        }
    }
}

}  // namespace portaudio
}  // namespace audio
