# Module audio

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


## Model viam:audio:discovery

This model is used to discover audio devices on your machine.
No configuration is needed, cxpand the test card or look at the discovery control card to obtain configurations for all connected audio devices.

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
