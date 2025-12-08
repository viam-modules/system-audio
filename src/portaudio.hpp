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

}  // namespace portaudio
}  // namespace audio
