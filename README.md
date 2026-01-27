# Module system-audio

This [Viam module](https://docs.viam.com/registry/) provides audio input and output capabilities using the [PortAudio](http://www.portaudio.com/) library. It can capture and play audio from microphones and speakers on your machine.

## Supported Platforms
- **Darwin ARM64**
- **Linux x64**
- **Linux ARM64**

## Model viam:audio:microphone

### Configuration
The following attribute template can be used to configure this model:

```json
{
  "device_name" : <DEVICE_NAME>,
  "sample_rate": <SAMPLE_RATE>,
  "num_channels": <NUM_CHANNELS>,
  "latency": <LATENCY>
}
```
#### Configuration Attributes

The following attributes are available for the `viam:audio:microphone` model:

| Name          | Type   | Inclusion | Description                |
|---------------|--------|-----------|----------------------------|
| `device_name` | string | **Optional** | The PortAudio device name to stream audio from. If not specified, the system default will be used. |
| `sample_rate` | int | **Optional** | The sample rate in Hz of the stream. If not specified, the device's default sample rate will be used. |
| `num_channels` | int | **Optional** | The number of audio channels to capture. Must not exceed the device's maximum input channels. Default: 1 |
| `latency` | int | **Optional** | Suggested input latency in milliseconds. This controls how much audio PortAudio buffers before making it available. Lower values (5-20ms) provide more responsive audio capture but use more CPU time. Higher values (50-100ms) are more stable but less responsive. If not specified, uses the device's default low latency setting (typically 10-20ms). |
| `historical_throttle_ms` | int | **Optional** | Delay in milliseconds between chunks when streaming historical audio data using the previous_timestamp parameter (default: 50ms). Gives clients adequate time to process buffered audio data. |

### Reconfiguration Behavior

The microphone component supports reconfiguration - you can change stream attributes without restarting the audio stream RPC calls. When you reconfigure:

- Active `get_audio()` calls will automatically transition to the new configuration
- There may be a brief gap in audio during the transition

#### Important Considerations

1. **Writing to fixed-format files (WAV, MP3, etc.)**
   - WAV files have a fixed header with sample rate and channel count
   - Changing `sample_rate` or `num_channels` mid-stream will corrupt the file
   - **Solution:** Stop recording, save the file, then reconfigure and start a new file

2. **During active audio encoding**
   - If you're encoding the streamed audio (e.g., to OPUS, AAC), changing `sample_rate` or `num_channels` will break the initialized encoder
   - **Solution:** Reinitialize the encoder when reconfigurations occur

**No client-side handling required:**
- When streaming audio chunks that are processed independently
- Changing `device_name` to switch microphones
- Adjusting `latency` for performance tuning
- Between `get_audio` RPC calls

**Clients should:**
- Monitor the `audio_info` field in each audio chunk
- Detect when `sample_rate` or `num_channels` changes
- Handle the transition appropriately

### macOS Microphone Permissions

**Important:** On macOS, do not run viam-server with `sudo` when using the microphone component. Running with `sudo` will result in silent audio (all zeros) due to macOS privacy restrictions that prevent root processes from accessing the microphone.

If you encounter permission issues:
1. Run viam-server without `sudo`
2. Grant microphone access to your Terminal application in **System Settings → Privacy & Security → Microphone**
3. Restart your terminal and try again


## Model viam:audio:speaker
### Configuration
The following attribute template can be used to configure this model:

```json
{
  "device_name" : <DEVICE_NAME>,
  "sample_rate": <SAMPLE_RATE>,
  "num_channels": <NUM_CHANNELS>,
  "latency": <LATENCY>
}
```

#### Configuration Attributes

The following attributes are available for the `viam:audio:speaker` model:

| Name          | Type   | Inclusion | Description                |
|---------------|--------|-----------|----------------------------|
| `device_name` | string | **Optional** | The PortAudio device name to play audio from. If not specified, the system default will be used. |
| `sample_rate` | int | **Optional** | The sample rate in Hz of the output stream. If not specified, the device's default sample rate will be used. |
| `num_channels` | int | **Optional** | The number of audio channels of the output stream. Must not exceed the device's maximum output channels. Default: 1 |
| `latency` | int | **Optional** | Suggested output latency in milliseconds. This controls how much audio PortAudio buffers before making it available. Lower values (5-20ms) provide faster audio output but use more CPU time. Higher values (50-100ms) are more stable but less responsive. If not specified, uses the device's default low latency setting (typically 10-20ms). |
| `volume` | int | **Optional** | Output volume as percentage (0-100). Supported on Linux devices only. On macOS, use the system volume controls (keyboard keys). |

## Model viam:audio:discovery

This model is used to discover audio devices on your machine.
No configuration is needed, expand the test card or look at the discovery control card to obtain configurations for all connected audio devices.


## Audio Format

All audio data uses **little-endian** byte order. The specific format depends on the codec requested:

**Supported codecs:**
- `PCM_16`: 16-bit signed integer PCM (range: -32768 to 32767)
- `PCM_32`: 32-bit signed integer PCM (range: -2147483648 to 2147483647)
- `PCM_32_FLOAT`: 32-bit floating point PCM (range: -1.0 to 1.0)
- `MP3`: MP3 compressed audio

**All audio data is in interleaved format** - multi-channel samples are stored sequentially:
- **Mono (1 channel)**: `[S0, S1, S2, ...]`
- **Stereo (2 channels)**: `[L0, R0, L1, R1, L2, R2, ...]` (left and right samples alternate)

- **Microphone (`get_audio`)**: Returns audio data in interleaved format
- **Speaker (`play`)**: Expects audio data in interleaved format


## Setup
```bash
canon make setup
```

## Build Module
```bash
canon make
```

## Build (Development)
```bash
canon make build
```
