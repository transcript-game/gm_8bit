# Opus Codec Integration

## Overview

gm_8bit uses the Opus audio codec (version 1.3+) to compress and decompress voice data. Opus is a lossy codec designed for real-time voice transmission with low latency and high quality.

## Opus Codec Basics

### What is Opus?

-   **Type**: Lossy audio codec
-   **Designed for**: VoIP, video conferencing, low-latency streaming
-   **Bitrate**: 6-510 kbps
-   **Latency**: 5-66.5ms algorithmic delay
-   **License**: BSD (royalty-free)

### Why Opus?

1. **Low Latency**: Critical for real-time voice communication
2. **High Quality**: Better than older codecs (Speex, G.711) at same bitrate
3. **Robust**: Handles packet loss gracefully with PLC
4. **Efficient**: Good quality at low bitrates (saves bandwidth)
5. **Standardized**: IETF RFC 6716

## Configuration

### Sample Rate

```cpp
#define SAMPLERATE_GMOD_OPUS 24000  // 24 kHz
```

**Why 24kHz?**

-   Balance between quality and bandwidth
-   Captures human voice frequency range (20-8000 Hz)
-   Lower than music quality (48kHz) but sufficient for speech
-   Garry's Mod standard

**Supported Opus Rates**: 8000, 12000, 16000, 24000, 48000 Hz

### Frame Size

```cpp
#define FRAME_SIZE_GMOD 480  // Samples
```

**Frame Duration**: 480 samples ÷ 24000 Hz = 20ms

**Why 20ms?**

-   Standard frame duration for VoIP
-   Good balance of latency and compression efficiency
-   Lower latency: 10ms (240 samples)
-   Higher efficiency: 40ms (960 samples), 60ms (1440 samples)

**Supported Opus Frames**: 2.5ms, 5ms, 10ms, 20ms, 40ms, 60ms

### Channels

**Configuration**: Mono (1 channel)

```cpp
opus_encoder_create(SAMPLERATE_GMOD_OPUS, 1, ...)  // channels = 1
opus_decoder_create(SAMPLERATE_GMOD_OPUS, 1, ...)  // channels = 1
```

**Why Mono?**

-   Source Engine voice is mono only
-   Stereo provides no benefit for voice chat
-   Saves ~50% bandwidth vs stereo

### Application Mode

```cpp
opus_encoder_create(..., OPUS_APPLICATION_VOIP, ...)
```

**Available Modes**:

-   `OPUS_APPLICATION_VOIP`: Optimized for voice
-   `OPUS_APPLICATION_AUDIO`: Optimized for music
-   `OPUS_APPLICATION_RESTRICTED_LOWDELAY`: Lowest latency

**VOIP Mode Characteristics**:

-   Emphasizes speech intelligibility
-   Better performance at low bitrates
-   More aggressive noise suppression
-   Optimizes for stationary speaker

## Opus Frame Packaging

### Custom Frame Format

gm_8bit uses a custom format to package Opus frames with sequence numbers for packet loss detection.

**Frame Structure**:

```
For each 480-sample chunk:
┌──────────────┬────────────────┬──────────────────┐
│ Length       │ Sequence       │ Opus Data        │
│ (uint16_t)   │ (uint16_t)     │ (variable)       │
└──────────────┴────────────────┴──────────────────┘

End-of-stream marker:
┌──────────────┐
│ 0xFFFF       │
└──────────────┘
```

**Why Custom Format?**

-   Opus library only handles single frames
-   Need to package multiple frames in one packet
-   Sequence numbers enable packet loss detection
-   End marker enables state reset

### Alternative: Ogg Opus

Standard Opus files use Ogg container:

```
[Ogg Page Header][Opus ID Header][Ogg Page][Opus Frames]...
```

**Not Used Because**:

-   Overhead of Ogg framing (unnecessary for streaming)
-   Real-time streaming doesn't need file format
-   Custom format is simpler and more efficient
-   Already have Steam Voice wrapping

## Encoder Implementation

### Initialization

```cpp
class Opus_FrameDecoder : public IVoiceCodec {
    OpusEncoder* enc = nullptr;
    uint16_t m_encodeSeq = 0;
    std::deque<uint16_t> sample_buf;  // Buffer for incomplete frames

    Opus_FrameDecoder() {
        int error = 0;
        enc = opus_encoder_create(
            SAMPLERATE_GMOD_OPUS,      // 24000 Hz
            1,                          // Mono
            OPUS_APPLICATION_VOIP,      // Voice optimized
            &error
        );
        if (error != OPUS_OK) {
            // Handle error
        }
    }
};
```

### Compression Process

**Function Signature**:

```cpp
int Compress(
    const char* pUncompressed,  // PCM samples (16-bit signed)
    int nSamples,               // Number of samples
    char* pCompressed,          // Output buffer
    int maxCompressedBytes,     // Max output size
    bool bFinal                 // Last frame?
)
```

**Algorithm**:

```cpp
1. Check if we have enough samples:
   if (sample_buf.size() + nSamples < 480 && !bFinal)
       // Buffer samples for later
       return 0;

2. Combine buffered + new samples:
   temp_buf = [sample_buf..., pUncompressed...]
   sample_buf.clear()

3. Handle remainder:
   remainder = (temp_buf.size()) % 480
   if (remainder > 0) {
       if (bFinal)
           // Pad with zeros to complete frame
           temp_buf.insert(..., 480 - remainder, 0)
       else
           // Save for next call
           sample_buf = temp_buf[-remainder:]
           temp_buf.erase(-remainder...)
   }

4. Encode each 480-sample frame:
   for each chunk of 480 samples:
       a. Write chunk length (uint16_t) - placeholder
       b. Write sequence number (m_encodeSeq++)
       c. Call opus_encode()
       d. Update chunk length with actual bytes
       e. Advance output pointer

5. If bFinal:
   a. Reset encoder state
   b. Reset sequence to 0
   c. Write 0xFFFF marker

6. Return: Total bytes written
```

**Sample Buffering**:

```cpp
std::deque<uint16_t> sample_buf;  // Persistent between calls
```

**Why Buffering?**

-   Input may not align with 480-sample boundaries
-   Buffer incomplete frames until we have enough
-   On `bFinal`, pad last frame with zeros

**Example**:

```
Call 1: 300 samples → buffer all (0 bytes output)
Call 2: 400 samples → 300+400=700 → encode 480, buffer 220 (compressed output)
Call 3: 500 samples → 220+500=720 → encode 480, buffer 240 (compressed output)
Call 4: 100 samples, bFinal → 240+100=340 → pad to 480, encode (compressed output)
```

### Opus Encode Call

```cpp
int bytes_written = opus_encode(
    enc,                                    // Encoder instance
    (opus_int16*)chunk,                     // PCM input (480 samples)
    FRAME_SIZE_GMOD,                        // Frame size (480)
    (unsigned char*)pCompressed,            // Output buffer
    std::min<uint64_t>(0x7FFF, maxBytes)   // Max output (cap at 32767)
);

if (bytes_written < 0) {
    // Error occurred
    return -1;
}
```

**Return Value**:

-   Positive: Number of bytes written
-   Negative: Error code (see Error Handling section)

**Typical Output Size**:

-   ~30-60 bytes per 480-sample frame
-   Varies based on voice content (silence = smaller)
-   VOIP mode targets ~24 kbps

## Decoder Implementation

### Initialization

```cpp
class Opus_FrameDecoder : public IVoiceCodec {
    OpusDecoder* dec = nullptr;
    uint16_t m_seq = 0;  // Expected sequence number

    Opus_FrameDecoder() {
        int error = 0;
        dec = opus_decoder_create(
            SAMPLERATE_GMOD_OPUS,  // 24000 Hz
            1,                      // Mono
            &error
        );
        if (error != OPUS_OK) {
            // Handle error
        }
    }
};
```

### Decompression Process

**Function Signature**:

```cpp
int Decompress(
    const char* pCompressed,    // Opus frame data
    int compressedBytes,        // Input size
    char* pUncompressed,        // Output PCM buffer
    int maxUncompressedBytes    // Max output size
)
```

**Algorithm**:

```cpp
1. While data remains:
   a. Read frame length (uint16_t)

   b. If length == 0xFFFF:
      - Reset decoder state
      - Reset sequence to 0
      - Continue to next frame

   c. Read sequence number (uint16_t)

   d. Handle packet loss:
      if (seq < m_seq):
          // Out-of-order packet
          opus_decoder_ctl(dec, OPUS_RESET_STATE)
      else if (seq > m_seq):
          // Lost packets detected
          lostFrames = min(seq - m_seq, 10)
          for i in 0..lostFrames:
              // Generate PLC frame
              opus_decode(dec, NULL, 0, output, maxSamples, 0)
          m_seq = seq

   e. Decode actual frame:
      samples = opus_decode(dec, data, len, output, maxSamples, 0)

   f. Update state:
      m_seq = seq + 1
      Advance pointers

2. Return: Total samples decoded
```

### Packet Loss Concealment (PLC)

**What is PLC?**

-   Opus generates synthetic audio for lost packets
-   Uses previous frames to predict missing content
-   Maintains audio continuity during packet loss

**Triggering PLC**:

```cpp
int samples = opus_decode(
    dec,
    NULL,           // NULL = lost packet
    0,              // Length 0
    output,         // Output buffer
    maxSamples,     // Max samples
    0               // decode_fec = 0
);
```

**PLC Characteristics**:

-   Gradually fades to silence if loss continues
-   Maintains pitch and timbre from previous frames
-   Better than inserting silence (less jarring)
-   Limited effectiveness (10+ lost frames = noticeable)

**Limiting PLC Frames**:

```cpp
uint32_t lostFrames = std::min(seq - m_seq, 10);
```

**Why Limit to 10?**

-   Excessive PLC degrades quality
-   Large sequence jumps indicate major issues
-   Better to reset than generate 100+ fake frames
-   Prevents buffer overflow

### Sequence Number Handling

**Out-of-Order Packets**:

```cpp
if (seq < m_seq) {
    // Received old packet (network reordering)
    opus_decoder_ctl(dec, OPUS_RESET_STATE);
    // Decoder state is now inconsistent, reset it
}
```

**Why Reset?**

-   Opus decoder is stateful
-   Depends on previous frames for context
-   Out-of-order breaks assumptions
-   Reset to clean state

**Sequence Tracking**:

```cpp
uint16_t m_seq;  // Next expected sequence

// After decoding
m_seq = seq + 1;
```

**Sequence Overflow**:

-   `uint16_t` wraps at 65535 → 0
-   Comparison `seq > m_seq` handles this:
    -   65535 → 0 is not > (due to unsigned arithmetic)
    -   Works correctly across overflow boundary

**For Rust**: Use `Wrapping<u16>` to make overflow explicit:

```rust
use std::num::Wrapping;
let m_seq = Wrapping(0u16);
```

### Opus Decode Call

```cpp
int samples = opus_decode(
    dec,                            // Decoder instance
    (const unsigned char*)data,     // Compressed input (or NULL for PLC)
    len,                            // Input length (or 0 for PLC)
    (opus_int16*)output,            // PCM output buffer
    (maxBytes) / 2,                 // Max samples (bytes / 2)
    0                               // decode_fec = 0
);

if (samples < 0) {
    // Error occurred
    return -1;
}
```

**Return Value**:

-   Positive: Number of samples decoded
-   Negative: Error code

**decode_fec Parameter**:

-   0: Normal decoding
-   1: Use FEC (Forward Error Correction) if available
-   gm_8bit uses 0 (no FEC)

## Buffer Management

### Buffer Safety Macros

**Read Macro**:

```cpp
#define CHK_BUF_ACCESS(varName, start, end, type)  \
    if(start + sizeof(type) > end)                 \
        return -1;                                 \
    type varName = *(type*)start;                  \
    start += sizeof(type);
```

**Usage**:

```cpp
CHK_BUF_ACCESS(len, pCompressed, pEnd, uint16_t);
// Expands to:
// if (pCompressed + 2 > pEnd) return -1;
// uint16_t len = *(uint16_t*)pCompressed;
// pCompressed += 2;
```

**Write Macro**:

```cpp
#define CHK_BUF_WRITE(start, end, type, val) \
    if(start + sizeof(type) > end)           \
        return -1;                           \
    *(type*)start = val;                     \
    start += sizeof(type);
```

**Usage**:

```cpp
CHK_BUF_WRITE(pCompressed, pCompressedEnd, uint16_t, m_encodeSeq++);
// Expands to:
// if (pCompressed + 2 > pCompressedEnd) return -1;
// *(uint16_t*)pCompressed = m_encodeSeq++;
// pCompressed += 2;
```

**Benefits**:

-   Prevents buffer overflows
-   DRY (Don't Repeat Yourself)
-   Compile-time abstraction (zero overhead)

**For Rust**: Use slices with bounds checking:

```rust
fn read_u16(cursor: &mut &[u8]) -> Result<u16, Error> {
    if cursor.len() < 2 {
        return Err(Error::UnexpectedEof);
    }
    let value = u16::from_le_bytes([cursor[0], cursor[1]]);
    *cursor = &cursor[2..];
    Ok(value)
}
```

## Encoder Control

### Reset State

```cpp
bool ResetState() {
    opus_decoder_ctl(dec, OPUS_RESET_STATE);
    opus_encoder_ctl(enc, OPUS_RESET_STATE);
    return true;
}
```

**When to Reset**:

-   End of voice stream (`bFinal = true`)
-   Sequence number error detected
-   Major packet loss (>10 frames)
-   Player disconnect/reconnect

**What it Does**:

-   Clears internal prediction state
-   Resets filters and buffers
-   Next frame treated as stream start
-   Prevents artifacts from state mismatch

### Other Control Options

**Bitrate Control**:

```cpp
opus_int32 bitrate = 24000;  // 24 kbps
opus_encoder_ctl(enc, OPUS_SET_BITRATE(bitrate));
```

**Complexity**:

```cpp
int complexity = 10;  // 0-10, higher = better quality, more CPU
opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(complexity));
```

**VBR (Variable Bitrate)**:

```cpp
opus_encoder_ctl(enc, OPUS_SET_VBR(1));  // Enable VBR
```

**DTX (Discontinuous Transmission)**:

```cpp
opus_encoder_ctl(enc, OPUS_SET_DTX(1));  // Enable DTX (silence detection)
```

**gm_8bit doesn't use these** - relies on Opus defaults for VOIP mode.

## Error Handling

### Opus Error Codes

From `opus_defines.h`:

```cpp
#define OPUS_OK                0    // Success
#define OPUS_BAD_ARG          -1    // Invalid argument
#define OPUS_BUFFER_TOO_SMALL -2    // Output buffer too small
#define OPUS_INTERNAL_ERROR   -3    // Internal codec error
#define OPUS_INVALID_PACKET   -4    // Corrupted data
#define OPUS_UNIMPLEMENTED    -5    // Feature not supported
#define OPUS_INVALID_STATE    -6    // Codec not initialized
#define OPUS_ALLOC_FAIL       -7    // Memory allocation failed
```

### Error Detection

**Encoder Errors**:

```cpp
int bytes = opus_encode(...);
if (bytes < 0) {
    switch (bytes) {
        case OPUS_BAD_ARG:         // Invalid frame size or buffer
        case OPUS_BUFFER_TOO_SMALL: // Output buffer insufficient
        case OPUS_INTERNAL_ERROR:   // Unexpected codec error
        // ...
    }
    return -1;  // Propagate error
}
```

**Decoder Errors**:

```cpp
int samples = opus_decode(...);
if (samples < 0) {
    switch (samples) {
        case OPUS_INVALID_PACKET:  // Corrupted data
        case OPUS_BUFFER_TOO_SMALL: // Output buffer too small
        case OPUS_INTERNAL_ERROR:   // Codec error
        // ...
    }
    return -1;  // Propagate error
}
```

### Graceful Degradation

**Current Strategy**: Silent failure

```cpp
if (bytes_written < 0) {
    return -1;  // Compress() returns -1
}
// Caller checks return value and falls back to original packet
```

**For Production**: Consider logging

```cpp
#ifdef DEBUG
    if (bytes < 0) {
        const char* error = opus_strerror(bytes);
        fprintf(stderr, "Opus encode failed: %s\n", error);
    }
#endif
```

## Memory Management

### Encoder/Decoder Lifecycle

**Creation**:

```cpp
Opus_FrameDecoder() {
    int error = 0;
    dec = opus_decoder_create(24000, 1, &error);
    enc = opus_encoder_create(24000, 1, OPUS_APPLICATION_VOIP, &error);

    if (error != OPUS_OK) {
        // Constructor failed - handle appropriately
    }
}
```

**Destruction**:

```cpp
~Opus_FrameDecoder() {
    opus_decoder_destroy(dec);
    opus_encoder_destroy(enc);
}
```

**Heap Allocation**:

-   `opus_*_create()` allocates on heap
-   ~20KB per encoder/decoder
-   Must call `opus_*_destroy()` to free

**Per-Player Codecs**:

```cpp
// Enable effect for player
IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
codec->Init(5, 24000);
afflictedPlayers[userid] = {codec, effect};

// Disable effect for player
IVoiceCodec* codec = std::get<0>(afflictedPlayers[userid]);
delete codec;  // Calls destructor, frees Opus instances
afflictedPlayers.erase(userid);
```

### Memory Footprint

**Per Codec Instance**:

-   OpusEncoder: ~20-23KB
-   OpusDecoder: ~18-21KB
-   Sample buffer: ~1-2KB (max 960 samples)
-   Total: ~40KB per player with effects

**10 Players with Effects**: 400KB
**100 Players**: Would be 4MB (not recommended)

## Performance Optimization

### Encoding Performance

**CPU Usage**:

-   ~0.1-0.5ms per 480-sample frame on modern CPU
-   VOIP mode is lighter than AUDIO mode
-   Complexity setting affects CPU (default ~5)

**Optimization**:

-   Opus uses SIMD (SSE, AVX on x86)
-   Compile with appropriate flags: `-msse4.1`, `-mavx`
-   Fixed-point vs floating-point (arch-dependent)

**Benchmarking**:

```cpp
#include <chrono>

auto start = std::chrono::high_resolution_clock::now();
opus_encode(...);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
printf("Encode took %lld μs\n", duration.count());
```

### Decoding Performance

**CPU Usage**:

-   ~0.1-0.3ms per frame (faster than encoding)
-   PLC generation is similar cost to normal decode
-   Decoder state is smaller than encoder

### Minimizing Allocations

**Static Buffers**:

```cpp
static char decompressedBuffer[20 * 1024];  // Pre-allocated
```

**Sample Buffer**:

```cpp
std::deque<uint16_t> sample_buf;  // Small, infrequent allocations
```

**For Rust**: Use arena allocators:

```rust
use bumpalo::Bump;

let arena = Bump::new();
let buffer = arena.alloc_slice_fill_copy(20 * 1024, 0u8);
```

## Opus Library Linking

### Windows

**32-bit**:

```lua
-- premake5.lua
links { "opus" }
libdirs { "opus/lib32" }
```

Files: `opus/lib32/opus.lib`

**64-bit**:

```lua
libdirs { "opus/lib64" }
```

Files: `opus/lib64/opus.lib`

### Linux

**System Library**:

```bash
sudo apt-get install libopus-dev  # Debian/Ubuntu
sudo yum install opus-devel       # RHEL/CentOS
```

**Linking**:

```lua
links { "opus" }
```

**Pkg-config**:

```bash
pkg-config --cflags --libs opus
# Output: -I/usr/include/opus -lopus
```

### Include Path

```lua
includedirs { "opus/include" }
```

Headers: `opus/include/opus.h`, `opus_defines.h`, `opus_types.h`

## Rust Opus Integration

### Crate Selection

**Option 1: opus (FFI)**

```toml
[dependencies]
opus = "0.3"
```

**Pros**: Direct binding to libopus
**Cons**: Requires system libopus

**Option 2: audiopus (Safe Wrapper)**

```toml
[dependencies]
audiopus = "0.3"
```

**Pros**: Safe Rust API, includes libopus
**Cons**: Slightly higher-level

### Example Usage

```rust
use audiopus::{Application, Channels, SampleRate};
use audiopus::coder::{Encoder, Decoder};

// Encoder
let mut encoder = Encoder::new(
    SampleRate::Hz24000,
    Channels::Mono,
    Application::Voip,
)?;

let output = encoder.encode(&pcm_samples, &mut compressed_buffer)?;

// Decoder
let mut decoder = Decoder::new(
    SampleRate::Hz24000,
    Channels::Mono,
)?;

let samples = decoder.decode(&compressed_data, &mut pcm_buffer, false)?;

// PLC
let plc_samples = decoder.decode(None, &mut pcm_buffer, false)?;
```

### Type Safety

```rust
struct OpusFrame {
    sequence: u16,
    data: Vec<u8>,
}

struct OpusFrameDecoder {
    encoder: Encoder,
    decoder: Decoder,
    encode_seq: u16,
    decode_seq: u16,
    sample_buffer: Vec<i16>,
}

impl OpusFrameDecoder {
    fn new() -> Result<Self, audiopus::Error> {
        Ok(Self {
            encoder: Encoder::new(
                SampleRate::Hz24000,
                Channels::Mono,
                Application::Voip,
            )?,
            decoder: Decoder::new(
                SampleRate::Hz24000,
                Channels::Mono,
            )?,
            encode_seq: 0,
            decode_seq: 0,
            sample_buffer: Vec::new(),
        })
    }

    fn compress(&mut self, samples: &[i16], final_frame: bool) -> Result<Vec<u8>, Error> {
        // Implementation
    }

    fn decompress(&mut self, data: &[u8]) -> Result<Vec<i16>, Error> {
        // Implementation
    }
}
```

## Testing Recommendations

### Unit Tests

**Encode/Decode Round-Trip**:

```cpp
// Generate test signal (sine wave)
int16_t samples[480];
for (int i = 0; i < 480; i++) {
    samples[i] = 10000 * sin(2 * M_PI * 440 * i / 24000);  // 440 Hz tone
}

// Encode
char compressed[100];
int len = codec.Compress((char*)samples, 480, compressed, 100, true);

// Decode
int16_t decoded[480];
int count = codec.Decompress(compressed, len, (char*)decoded, 960);

// Verify
assert(count == 480);
// Note: Lossy codec, won't match exactly
// Check RMS error < threshold
```

**Packet Loss Simulation**:

```cpp
// Encode frames 0, 1, 2, 3, 4
// Decode frames 0, 1, 4 (skip 2, 3)
// Verify PLC generates frames for 2, 3
```

**Sequence Wraparound**:

```cpp
m_encodeSeq = 65534;
// Encode 3 frames
// Sequences: 65534, 65535, 0
// Verify decoder handles correctly
```

### Integration Tests

**Real Voice Data**: Use captured PCM from microphone
**Long Streams**: Test thousands of frames
**Boundary Conditions**: 0 samples, max samples, bFinal transitions

### Fuzzing

```cpp
// Random compressed data
for (int i = 0; i < 10000; i++) {
    char random[1000];
    for (int j = 0; j < 1000; j++) {
        random[j] = rand() % 256;
    }

    char output[10000];
    codec.Decompress(random, 1000, output, 10000);
    // Should not crash
}
```

## Troubleshooting

### Common Issues

**Issue**: Decoder returns -4 (OPUS_INVALID_PACKET)
**Cause**: Corrupted compressed data
**Solution**: Verify frame packaging, check network transmission

**Issue**: Encoder returns -2 (OPUS_BUFFER_TOO_SMALL)
**Cause**: Output buffer insufficient
**Solution**: Increase buffer size (100-200 bytes per frame typical)

**Issue**: Audio quality degraded
**Cause**: Low bitrate or packet loss
**Solution**: Increase bitrate with `OPUS_SET_BITRATE`, improve network

**Issue**: Clicking/popping sounds
**Cause**: Sequence number errors, state reset mid-stream
**Solution**: Debug sequence tracking, verify packet ordering

**Issue**: Compile error: "opus.h not found"
**Cause**: Include path not set
**Solution**: Add `opus/include` to include directories

**Issue**: Link error: "undefined reference to opus_encode"
**Cause**: Opus library not linked
**Solution**: Add `opus` to linker flags, verify lib path
