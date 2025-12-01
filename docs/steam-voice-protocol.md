# Steam Voice Protocol

## Overview

Steam uses a proprietary voice packet format to transmit Opus-encoded audio over the Source Engine network protocol. This format wraps Opus frames with metadata including player identification, checksums, and operation codes.

## Packet Structure

### Complete Packet Layout

```
Offset | Size    | Field                | Description
-------|---------|----------------------|----------------------------------
0      | 8       | SteamID64           | Unique player identifier
8      | 4       | CRC32               | Packet checksum (usually 0)
12     | 1       | Operation Code      | Instruction type
13     | varies  | Payload             | Operation-specific data
...    | varies  | More Operations     | Additional opcodes and payloads
end-4  | 4       | (CRC appended)      | Optional trailing checksum
```

### Minimum Packet Size

```cpp
#define STEAM_PCKT_SZ (sizeof(uint64_t) + sizeof(CRC32_t))  // 12 bytes
```

Any packet smaller than 12 bytes is invalid and discarded.

## Operation Codes

### OP_SILENCE (0)

**Purpose**: Indicates a period of silence in the audio stream

**Payload Structure**:

```
┌──────────────┬─────────────────┐
│ Opcode: 0x00 │ Samples: uint16 │
└──────────────┴─────────────────┘
```

**Usage**:

-   Sent when player is not speaking but connection is active
-   Receiver should insert silence samples
-   Helps maintain stream timing

**Implementation**:

```cpp
case OP_SILENCE: {
    if (curRead + sizeof(uint16_t) > maxRead)
        return -1;
    uint16_t silenceSamples = *(uint16_t*)curRead;
    curRead += sizeof(uint16_t);
    // Skip: gm_8bit doesn't insert silence
    break;
}
```

### OP_SAMPLERATE (11)

**Purpose**: Declares the sample rate for the audio stream

**Payload Structure**:

```
┌──────────────┬────────────────────┐
│ Opcode: 0x0B │ SampleRate: uint16 │
└──────────────┴────────────────────┘
```

**Expected Value**: 24000 (24kHz)

**Usage**:

-   Sent at the beginning of a voice stream
-   Should match decoder initialization
-   Always 24000 for Garry's Mod

**Implementation**:

```cpp
case OP_SAMPLERATE: {
    if (curRead + sizeof(uint16_t) > maxRead)
        return -1;
    uint16_t sampleRate = *(uint16_t*)curRead;
    // Expected: sampleRate == 24000
    curRead += sizeof(uint16_t);
    break;
}
```

### OP_CODEC_OPUSPLC (6)

**Purpose**: Contains Opus-encoded audio frames

**Payload Structure**:

```
┌──────────────┬─────────────────┬──────────────────────┐
│ Opcode: 0x06 │ Length: uint16  │ Opus Frame Data      │
└──────────────┴─────────────────┴──────────────────────┘
                                  └─── Length bytes ────┘
```

**Frame Data Format**:

-   Custom Opus frame packaging (see Opus Integration docs)
-   Contains multiple 480-sample frames
-   Includes sequence numbers for packet loss detection

**Implementation**:

```cpp
case OP_CODEC_OPUSPLC: {
    if (curRead + sizeof(uint16_t) > maxRead)
        return -1;

    uint16_t frameDataLen = *(uint16_t*)curRead;
    curRead += sizeof(uint16_t);

    if (curRead + frameDataLen > maxRead)
        return -1;

    int decompressedSamples = codec->Decompress(
        curRead, frameDataLen,
        curWrite, maxWrite - curWrite
    );

    if (decompressedSamples <= 0)
        return -1;

    curWrite += decompressedSamples * 2;  // 16-bit samples
    curRead += frameDataLen;
    break;
}
```

## Decompression Process

### Function Signature

```cpp
int DecompressIntoBuffer(
    IVoiceCodec* codec,           // Codec instance for decoding
    const char* compressedData,   // Input Steam voice packet
    int compressedLen,            // Packet length
    char* decompressedOut,        // Output PCM buffer
    int maxDecompressed           // Maximum output size
)
```

**Returns**: Number of PCM bytes written, or -1 on error

### Algorithm

```cpp
1. Initialize read/write pointers:
   curRead = compressedData + sizeof(uint64_t)  // Skip SteamID
   maxRead = compressedData + compressedLen - sizeof(uint32_t)  // Skip trailing CRC
   curWrite = decompressedOut
   maxWrite = decompressedOut + maxDecompressed

2. While data remains:
   a. Read opcode (1 byte)
   b. Switch on opcode:
      - OP_SILENCE: Skip silence samples count
      - OP_SAMPLERATE: Skip sample rate value
      - OP_CODEC_OPUSPLC: Decompress Opus frames
      - Unknown: Return error (-1)
   c. Validate buffer bounds at each step

3. Return: curWrite - decompressedOut (total bytes)
```

### Error Conditions

**Buffer Overflow**:

```cpp
if (curRead + sizeof(type) > maxRead)
    return -1;  // Would read past end
```

**Invalid Opcode**:

```cpp
default:
    return -1;  // Unknown operation
```

**Decompression Failure**:

```cpp
if (decompressedSamples <= 0)
    return -1;  // Codec error
```

### Memory Safety

All buffer accesses validated:

```cpp
// Before reading any value
if (curRead + size > maxRead) return -1;

// Before writing any value
if (curWrite + size > maxWrite) return -1;
```

No possibility of buffer overflow if checks pass.

## Compression Process

### Function Signature

```cpp
int CompressIntoBuffer(
    uint64_t steamid,          // Player's SteamID64
    IVoiceCodec* codec,        // Codec instance for encoding
    const char* inputData,     // Input PCM samples
    int inputLen,              // Number of bytes
    char* compressedOut,       // Output packet buffer
    int maxCompressed,         // Maximum output size
    int sampleRate             // Sample rate (24000)
)
```

**Returns**: Number of packet bytes written, or -1 on error

### Algorithm

```cpp
1. Write SteamID64:
   *(uint64_t*)curWrite = steamid
   curWrite += 8

2. Write OP_SAMPLERATE:
   *curWrite = OP_SAMPLERATE (0x0B)
   curWrite += 1
   *(uint16_t*)curWrite = sampleRate (24000)
   curWrite += 2

3. Write OP_CODEC_OPUSPLC:
   *curWrite = OP_CODEC_OPUSPLC (0x06)
   curWrite += 1

   // Reserve space for length
   uint16_t* lengthPtr = (uint16_t*)curWrite
   curWrite += 2

   // Compress audio
   int compressedBytes = codec->Compress(
       inputData, inputLen / 2,  // Convert bytes to samples
       curWrite, maxWrite - curWrite,
       true  // bFinal
   )

   if (compressedBytes <= 0) return -1

   // Write actual length
   *lengthPtr = (uint16_t)compressedBytes
   curWrite += compressedBytes

4. Write trailing CRC32:
   *(uint32_t*)curWrite = 0  // Placeholder
   curWrite += 4

5. Return: curWrite - compressedOut
```

### Packet Layout Example

For a typical voice packet:

```
Offset | Hex Value        | Description
-------|------------------|------------------------------------------
0      | 01 00 10 01 00 00 00 00 | SteamID64 (little-endian)
8      | 00 00 00 00      | CRC32 (placeholder)
12     | 0B               | OP_SAMPLERATE opcode
13     | C0 5D            | Sample rate: 24000 (0x5DC0)
15     | 06               | OP_CODEC_OPUSPLC opcode
16     | 3A 01            | Frame data length: 314 bytes (0x013A)
18     | 00 00 2F 00 ... | Opus frame data (314 bytes)
332    | 00 00 00 00      | Trailing CRC32 (placeholder)
```

**Total Size**: 336 bytes (typical for ~1 second of speech)

## SteamID64 Extraction

### From IClient Structure

The SteamID64 in the packet header may be client-controlled and untrustworthy. The server extracts the real SteamID64 from the `IClient` structure.

**Platform-Specific Offsets**:

```cpp
#if defined ARCHITECTURE_X86
    uint64_t id64 = *(uint64_t*)((char*)cl + 181);
#else  // x86-64
    uint64_t id64 = *(uint64_t*)((char*)cl + 189);
#endif
```

**Why Different Offsets?**

-   Different struct padding on x86 vs x86-64
-   Different pointer sizes (4 vs 8 bytes)
-   Different alignment requirements

**Security**:

-   Always use server-side SteamID64
-   Never trust client-provided SteamID
-   Prevents player impersonation

### Writing Corrected SteamID64

```cpp
// Replace client-provided SteamID with server-authoritative one
*(uint64_t*)decompressedBuffer = id64;

// Copy rest of packet
memcpy(
    decompressedBuffer + sizeof(uint64_t),
    data + sizeof(uint64_t),
    nBytes - sizeof(uint64_t)
);
```

## CRC32 Checksum

### Current Implementation

**Reading**:

```cpp
// Skip trailing CRC32 when decompressing
maxRead = compressedData + compressedLen - sizeof(uint32_t);
```

**Writing**:

```cpp
// Write placeholder CRC32
*(uint32_t*)curWrite = 0;
curWrite += sizeof(uint32_t);
```

### Checksum Verification

**Status**: Not implemented

**Reasons**:

-   Source Engine may not verify checksums
-   gm_8bit processes packets server-side
-   No observed issues with zero checksums
-   Additional CPU overhead for minimal benefit

**For Production Use**:

If CRC32 verification becomes necessary:

```cpp
#include <checksum_crc.h>

// Calculate checksum over packet (excluding CRC field)
CRC32_t crc = CRC32_ProcessSingleBuffer(
    packet + sizeof(uint64_t),           // Start after SteamID
    packetLen - sizeof(uint64_t) - sizeof(CRC32_t)  // Exclude SteamID and CRC
);

// Write to packet
*(CRC32_t*)(packet + 8) = crc;
```

## Operation Code Sequence

### Typical Voice Packet

```
1. OP_SAMPLERATE (11)  → Declares 24000 Hz
2. OP_CODEC_OPUSPLC (6) → Contains Opus frames
```

### Extended Voice Packet

```
1. OP_SAMPLERATE (11)   → Declares 24000 Hz
2. OP_CODEC_OPUSPLC (6) → First chunk of frames
3. OP_SILENCE (0)       → Period of silence
4. OP_CODEC_OPUSPLC (6) → Second chunk of frames
```

### Stream Initialization

First packet in a voice stream typically includes:

```
1. OP_SAMPLERATE (11)   → Set decoder sample rate
2. OP_CODEC_OPUSPLC (6) → Initial audio frames
```

Subsequent packets may omit `OP_SAMPLERATE`.

## Protocol Variations

### Garry's Mod Specific

**Sample Rate**: Always 24000 Hz

-   Lower quality than 48kHz standard Opus
-   Bandwidth optimization for game servers
-   Adequate for voice chat clarity

**Channels**: Always mono (1 channel)

-   Source Engine voice is mono only
-   Stereo not supported in current implementation

**Frame Size**: 480 samples (20ms at 24kHz)

-   Standard Opus frame duration
-   Good balance of latency and quality

### Other Source Games

May use different:

-   Sample rates (8kHz, 12kHz, 16kHz, 24kHz)
-   Codecs (Speex, SILK, Opus)
-   Operation codes

gm_8bit is specifically tuned for Garry's Mod.

## Error Handling

### Malformed Packets

**Too Small**:

```cpp
if (nBytes < STEAM_PCKT_SZ) {
    return trampoline(...);  // Pass through unmodified
}
```

**Buffer Overrun**:

```cpp
if (curRead + size > maxRead) {
    return -1;  // Abort decompression
}
```

**Invalid Opcode**:

```cpp
default:
    return -1;  // Unknown operation
```

### Graceful Degradation

All errors result in:

```cpp
if (bytesDecompressed <= 0) {
    // Pass through original packet unchanged
    return detour_BroadcastVoiceData.GetTrampoline<...>()(
        cl, nBytes, data, xuid
    );
}
```

No server crashes, no disconnects, only that packet skipped.

## Reverse Engineering Notes

### Discovery Method

1. Binary analysis of Source Engine
2. Memory inspection during voice transmission
3. Packet capture and analysis
4. Cross-reference with Source SDK symbols
5. Validation against working implementations

### Undocumented Aspects

-   Exact CRC32 algorithm (if verified)
-   Additional operation codes (if any)
-   Protocol versioning (if exists)
-   Negotiation process (if any)

### Related Files

**Source SDK**:

-   `IClient` interface definition
-   Voice codec interfaces
-   Network message definitions

**gm_8bit**:

-   `source/steam_voice.h` - Main implementation
-   `source/ivoicecodec.h` - Codec interface
-   `templates/steam_voice.bt` - 010 Editor template

## 010 Editor Template

**File**: `templates/steam_voice.bt`

Binary template for visualizing Steam voice packets in a hex editor. Useful for debugging and reverse engineering.

**Usage**:

1. Open voice packet in 010 Editor
2. Load `steam_voice.bt` template
3. View parsed structure

## Rust Implementation Considerations

### Packet Parsing

```rust
struct SteamVoicePacket<'a> {
    steam_id: u64,
    crc: u32,
    operations: Vec<Operation<'a>>,
}

enum Operation<'a> {
    Silence { samples: u16 },
    SampleRate { rate: u16 },
    OpusData { data: &'a [u8] },
}

impl<'a> SteamVoicePacket<'a> {
    fn parse(data: &'a [u8]) -> Result<Self, ParseError> {
        let mut cursor = Cursor::new(data);

        let steam_id = cursor.read_u64::<LittleEndian>()?;
        let crc = cursor.read_u32::<LittleEndian>()?;

        let mut operations = Vec::new();
        while cursor.position() < (data.len() - 4) as u64 {
            let opcode = cursor.read_u8()?;
            let op = match opcode {
                0 => Operation::Silence {
                    samples: cursor.read_u16::<LittleEndian>()?,
                },
                11 => Operation::SampleRate {
                    rate: cursor.read_u16::<LittleEndian>()?,
                },
                6 => {
                    let len = cursor.read_u16::<LittleEndian>()? as usize;
                    let pos = cursor.position() as usize;
                    Operation::OpusData {
                        data: &data[pos..pos + len],
                    }
                },
                _ => return Err(ParseError::InvalidOpcode(opcode)),
            };
            operations.push(op);
        }

        Ok(SteamVoicePacket { steam_id, crc, operations })
    }
}
```

### Type Safety

```rust
#[derive(Debug, Clone, Copy)]
#[repr(u8)]
enum OpCode {
    Silence = 0,
    OpusPlc = 6,
    SampleRate = 11,
}

impl TryFrom<u8> for OpCode {
    type Error = ParseError;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(OpCode::Silence),
            6 => Ok(OpCode::OpusPlc),
            11 => Ok(OpCode::SampleRate),
            _ => Err(ParseError::InvalidOpcode(value)),
        }
    }
}
```

### Error Handling

```rust
use thiserror::Error;

#[derive(Error, Debug)]
enum ParseError {
    #[error("Packet too small: {0} bytes (minimum 12)")]
    TooSmall(usize),

    #[error("Invalid operation code: {0}")]
    InvalidOpcode(u8),

    #[error("Unexpected end of packet")]
    UnexpectedEof,

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
```

## Testing Recommendations

### Unit Tests

1. **Parse Valid Packets**: Known good packets should parse successfully
2. **Reject Invalid Opcodes**: Unknown operations should fail
3. **Buffer Overflow Protection**: Oversized lengths should be caught
4. **Minimum Size Validation**: <12 byte packets rejected

### Integration Tests

1. **Real Packet Capture**: Use Wireshark to capture actual packets
2. **Round-Trip**: Compress then decompress should match original
3. **Effect Processing**: Modified packets should still parse
4. **Corruption Handling**: Randomly corrupted packets shouldn't crash

### Fuzzing

```rust
#[cfg(test)]
mod fuzz {
    use super::*;

    #[test]
    fn fuzz_parse() {
        for _ in 0..10000 {
            let data: Vec<u8> = (0..rand::random::<u8>())
                .map(|_| rand::random())
                .collect();

            // Should never panic
            let _ = SteamVoicePacket::parse(&data);
        }
    }
}
```

## Performance Considerations

### Zero-Copy Parsing

Current C++ implementation copies data:

```cpp
memcpy(decompressedBuffer + sizeof(uint64_t),
       data + sizeof(uint64_t),
       nBytes - sizeof(uint64_t));
```

Rust can use slices for zero-copy:

```rust
let opus_data: &[u8] = &packet[18..332];  // No copy
```

### Stack vs Heap

**C++**: Fixed-size stack buffers

```cpp
static char decompressedBuffer[20 * 1024];
```

**Rust**: Consider arena allocation

```rust
let mut arena = bumpalo::Bump::new();
let buffer = arena.alloc_slice_fill_copy(20 * 1024, 0u8);
```

### Hot Path Optimization

Decompression is called for every voice packet (100-1000x per second):

-   Inline small functions
-   Avoid allocations
-   Minimize bounds checks (use `unsafe` with verification)
-   Profile with `perf` or `cargo flamegraph`
