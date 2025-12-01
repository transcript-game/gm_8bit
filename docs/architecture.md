# Architecture

## System Architecture

gm_8bit uses a hook-based architecture to intercept voice data at the broadcast stage, manipulate it, and re-inject it into the engine's normal flow.

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      Source Engine Layer                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────┐          ┌──────────────────┐                     │
│  │  Client  │ ────────>│ Voice Reception  │                     │
│  │  Voice   │          │    Handler       │                     │
│  └──────────┘          └──────────────────┘                     │
│                                 │                               │
│                                 ▼                               │
│                    ┌─────────────────────────┐                  │
│                    │ SV_BroadcastVoiceData   │◄─── Hooked       │
│                    └─────────────────────────┘                  │
│                                 │                               │
└─────────────────────────────────┼───────────────────────────────┘
                                  │
                    ┌─────────────▼────────────┐
                    │   gm_8bit Module Layer   │
                    ├──────────────────────────┤
                    │                          │
                    │  ┌────────────────────┐  │
                    │  │   Hook Handler     │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │  Steam Voice       │  │
                    │  │  Decompressor      │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │  Opus Frame        │  │
                    │  │  Decoder           │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │  Audio Effects     │  │
                    │  │  Engine            │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │  Opus Frame        │  │
                    │  │  Encoder           │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │            ▼             │
                    │  ┌────────────────────┐  │
                    │  │  Steam Voice       │  │
                    │  │  Compressor        │  │
                    │  └─────────┬──────────┘  │
                    │            │             │
                    │     ┌──────┴──────┐      │
                    │     ▼             ▼      │
                    │  ┌────────┐  ┌────────┐  │
                    │  │Network │  │ Engine │  │
                    │  │ Relay  │  │Callback│  │
                    │  └────────┘  └────────┘  │
                    │                          │
                    └──────────────────────────┘
                              │         │
                              ▼         ▼
                        UDP Socket   Clients
```

## Core Components

### 1. Hook Handler

**File**: `source/main.cpp` (lines 88-130)

**Purpose**: Intercepts the `SV_BroadcastVoiceData` function to gain control over voice packets before they're sent to clients.

**Key Functions**:

-   `hook_BroadcastVoiceData()`: Main interception point

**Platform-Specific Details**:

**Windows x86**:

```cpp
Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50\x83\x78\x30\x00...")
```

**Windows x86-64**:

```cpp
Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC...")
```

**Linux**:

```cpp
Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx")
```

**Symbol Resolution**:

1. Uses `SymbolFinder` from garrysmod_common
2. Searches engine binary for signature patterns
3. Falls back to symbol name on Linux
4. Creates detour hook with Detouring library

**Hook Flow**:

```cpp
void hook_BroadcastVoiceData(IClient* cl, uint nBytes, char* data, int64 xuid) {
    // 1. Check if player has effects enabled
    // 2. Optional: relay original packet via UDP
    // 3. If afflicted: decompress, process, recompress
    // 4. Call original function or modified version
}
```

### 2. Steam Voice Handler

**File**: `source/steam_voice.h`

**Purpose**: Handles Steam's proprietary voice packet format, extracting and packaging Opus frames.

**Functions**:

#### DecompressIntoBuffer()

Extracts Opus frames from Steam voice packets and decodes to PCM.

```cpp
int DecompressIntoBuffer(
    IVoiceCodec* codec,
    char* data,           // Steam voice packet
    int nBytes,           // Packet size
    char* dest,           // Output PCM buffer
    int destSize          // Max output size
)
```

**Process**:

1. Skip SteamID (8 bytes) and CRC32 (4 bytes)
2. Parse operation codes:
    - `OP_CODEC_OPUSPLC (6)`: Begin Opus stream
    - `OP_SAMPLERATE (11)`: Set sample rate (should be 24000)
    - `OP_SILENCE (0)`: Silence frame
3. Extract Opus frame data
4. Call codec's `Decompress()` method
5. Return total PCM bytes

**Packet Structure**:

```
Offset | Size | Field
-------|------|------------------
0      | 8    | SteamID64
8      | 4    | CRC32 checksum
12     | 1    | Operation code
13     | var  | Payload (depends on opcode)
```

#### CompressIntoBuffer()

Packages PCM data into Steam voice format with Opus encoding.

```cpp
int CompressIntoBuffer(
    IVoiceCodec* codec,
    char* samples,        // PCM input
    int nSamples,         // Sample count
    char* dest,           // Output packet buffer
    int destSize,         // Max output size
    uint64_t steamID,     // Player's SteamID64
    bool bFinal           // Last frame?
)
```

**Process**:

1. Write SteamID64 (8 bytes)
2. Write placeholder CRC32 (4 bytes) - set to 0
3. Write `OP_CODEC_OPUSPLC` operation code
4. Write `OP_SAMPLERATE` + value (24000)
5. Call codec's `Compress()` method
6. Return total packet bytes

### 3. Opus Frame Codec

**File**: `source/opus_framedecoder.h`

**Class**: `Opus_FrameDecoder`

**Purpose**: Implements the `IVoiceCodec` interface using Opus codec with custom frame packaging.

**Constants**:

```cpp
#define SAMPLERATE_GMOD_OPUS 24000  // Hz
#define FRAME_SIZE_GMOD 480         // Samples (20ms at 24kHz)
#define OP_SILENCE 0
#define OP_CODEC_OPUSPLC 6
#define OP_SAMPLERATE 11
```

**Members**:

```cpp
OpusEncoder* enc;      // Opus encoder instance
OpusDecoder* dec;      // Opus decoder instance
uint16_t m_encodeSeq;  // Encoder sequence number
uint16_t m_seq;        // Decoder sequence number
```

**Buffer Protection Macros**:

```cpp
#define CHK_BUF_ACCESS(var, start, end, type) \
    if (start + sizeof(type) > end) return -1; \
    type var = *(type*)start; \
    start += sizeof(type);

#define CHK_BUF_WRITE(start, end, type, val) \
    if (start + sizeof(type) > end) return -1; \
    *(type*)start = val; \
    start += sizeof(type);
```

#### Compress() Method

**Frame Structure**:

```
Per frame:
┌──────────────┬────────────┬──────────────┐
│ Sequence     │ Frame Size │ Opus Data    │
│ (uint16_t)   │ (uint16_t) │ (variable)   │
└──────────────┴────────────┴──────────────┘

Final marker:
┌──────────────┐
│ 0xFFFF       │
└──────────────┘
```

**Algorithm**:

```cpp
1. For each 480-sample frame:
   a. Write sequence number (m_encodeSeq++)
   b. Encode frame with opus_encode()
   c. Write frame length
   d. Write encoded data
2. If bFinal:
   a. Write 0xFFFF marker
   b. Reset encoder state
   c. Reset sequence to 0
```

#### Decompress() Method

**Packet Loss Handling**:

```cpp
if (seq < m_seq) {
    // Out-of-order packet, reset decoder
    opus_decoder_ctl(dec, OPUS_RESET_STATE);
} else if (seq > m_seq) {
    // Lost packets detected
    uint32_t lostFrames = min(seq - m_seq, 10);
    for (i = 0; i < lostFrames; i++) {
        // Generate PLC frames
        opus_decode(dec, NULL, 0, pcm, frameSize, 0);
    }
}
```

**Algorithm**:

```cpp
1. While data available:
   a. Read sequence number
   b. Check for packet loss
   c. Generate PLC frames if needed
   d. Read frame length
   e. Decode frame with opus_decode()
   f. Append to output buffer
   g. Update m_seq = seq + 1
2. Return total samples decoded
```

### 4. Audio Effects Engine

**File**: `source/audio_effects.h`

**Namespace**: `AudioEffects`

**Effect Types**:

```cpp
enum {
    EFF_NONE = 0,      // No effect (pass-through)
    EFF_BITCRUSH = 1,  // Bit depth reduction
    EFF_DESAMPLE = 2   // Sample rate reduction
};
```

#### BitCrush Effect

**Purpose**: Reduces bit depth to create lo-fi/retro sound quality.

**Algorithm**:

```cpp
void BitCrush(uint16_t* sampleBuffer, int samples, float quant, float gainFactor) {
    for (int i = 0; i < samples; i++) {
        float f = (float)sampleBuffer[i];
        f /= quant;              // Reduce precision
        sampleBuffer[i] = (uint16_t)f;
        sampleBuffer[i] *= quant;  // Restore amplitude
        sampleBuffer[i] *= gainFactor; // Compensate volume
    }
}
```

**Parameters**:

-   `quant`: Quantization divisor (default: 350)
    -   Higher = more distortion
    -   Range: 1-1000 recommended
-   `gainFactor`: Volume compensation (default: 1.2)
    -   Compensates for quieter output
    -   Range: 0.5-2.0 recommended

**Example Values**:

-   Light distortion: quant=500, gain=1.1
-   Medium distortion: quant=350, gain=1.2
-   Heavy distortion: quant=150, gain=1.5

#### Desample Effect

**Purpose**: Reduces effective sample rate by removing samples.

**Algorithm**:

```cpp
void Desample(uint16_t* inBuffer, int& samples, int desampleRate = 2) {
    int outIdx = 0;
    for (int i = 0; i < samples; i++) {
        if (i % desampleRate == 0) continue;  // Skip every nth
        tempBuf[outIdx] = inBuffer[i];
        outIdx++;
    }
    memcpy(inBuffer, tempBuf, outIdx * 2);
    samples = outIdx;  // Update count
}
```

**Parameters**:

-   `desampleRate`: Skip rate (default: 2)
    -   2 = Skip every other sample (~12kHz effective)
    -   3 = Skip every 3rd sample (~16kHz effective)
    -   4 = Skip every 4th sample (~18kHz effective)

**Frequency Calculation**:

```
Effective frequency = 24000 * (1 - 1/desampleRate)
desampleRate=2 → 12000 Hz
desampleRate=3 → 16000 Hz
desampleRate=4 → 18000 Hz
```

**Buffer Usage**:

-   Uses static `tempBuf[10*1024]` for temporary storage
-   Maximum ~5000 samples supported
-   In-place copy back to input buffer

### 5. State Management

**File**: `source/eightbit_state.h`

**Structure**: `EightbitState`

**Members**:

```cpp
struct EightbitState {
    // Effect parameters
    int crushFactor = 350;      // BitCrush quantization
    float gainFactor = 1.2;     // BitCrush gain
    int desampleRate = 2;       // Desample skip rate

    // Network relay
    bool broadcastPackets = false;  // Enable UDP relay
    uint16_t port = 4000;           // Destination port
    std::string ip = "127.0.0.1";   // Destination IP

    // Per-player state
    std::unordered_map<int, std::tuple<IVoiceCodec*, int>> afflictedPlayers;
    // Map: UserID → (Codec Instance, Effect Type)
};
```

**Global Instance**:

```cpp
EightbitState* g_eightbit = nullptr;  // Initialized in GMOD_MODULE_OPEN
```

**Player State Management**:

```cpp
// Enable effect for player
IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
codec->Init(5, 24000);
g_eightbit->afflictedPlayers[userid] = {codec, effect};

// Disable effect for player
IVoiceCodec* codec = std::get<0>(g_eightbit->afflictedPlayers[userid]);
delete codec;
g_eightbit->afflictedPlayers.erase(userid);
```

**Lookup Performance**:

-   `std::unordered_map` provides O(1) lookup
-   Critical because hook is called for every voice packet
-   Hundreds of times per second during active voice chat

### 6. Network Relay System

**Files**: `source/net.cpp`, `source/net.h`

**Class**: `Net`

**Purpose**: UDP relay of voice packets to external applications.

**Members**:

```cpp
class Net {
private:
#ifdef SYSTEM_WINDOWS
    SOCKET sock;
#else
    int sock;
#endif
public:
    Net();
    ~Net();
    void SendPacket(const char* ip, uint16_t port, char* data, int len);
};
```

**Windows Implementation**:

```cpp
Net::Net() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

Net::~Net() {
    closesocket(sock);
    WSACleanup();
}
```

**Linux Implementation**:

```cpp
Net::Net() {
    sock = socket(AF_INET, SOCK_DGRAM, 0);
}

Net::~Net() {
    close(sock);
}
```

**SendPacket()**:

```cpp
void Net::SendPacket(const char* ip, uint16_t port, char* data, int len) {
    sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    sendto(sock, data, len, 0, (sockaddr*)&dest, sizeof(dest));
}
```

**Packet Format**:

-   Identical to original Steam voice packet
-   Includes modified SteamID64 at offset 0
-   No additional headers or encryption

**Usage Pattern**:

```cpp
if (g_eightbit->broadcastPackets && nBytes > sizeof(uint64_t)) {
    // Extract real SteamID64 from IClient structure
    uint64_t id64 = *(uint64_t*)((char*)cl + offset);
    *(uint64_t*)buffer = id64;
    memcpy(buffer + 8, data + 8, nBytes - 8);
    net_handl->SendPacket(ip, port, buffer, nBytes);
}
```

## Data Flow Details

### Voice Packet Processing Pipeline

```
┌─────────────────────────────────────────────────────────────┐
│ Step 1: Hook Intercept                                       │
├─────────────────────────────────────────────────────────────┤
│ Input:  IClient*, nBytes, char* data, int64 xuid            │
│ Check:  userid in afflictedPlayers?                         │
│ Check:  nBytes >= STEAM_PCKT_SZ (12 bytes)?                 │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 2: Steam Voice Decompression                           │
├─────────────────────────────────────────────────────────────┤
│ Skip:   SteamID64 (8 bytes) + CRC32 (4 bytes)              │
│ Parse:  Operation codes (OP_CODEC_OPUSPLC, OP_SAMPLERATE)  │
│ Output: Opus frame data ready for decoding                  │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 3: Opus Frame Decoding                                 │
├─────────────────────────────────────────────────────────────┤
│ For each frame:                                             │
│   - Read sequence number (uint16_t)                         │
│   - Check for packet loss (generate PLC if needed)          │
│   - Read frame length (uint16_t)                            │
│   - opus_decode() to PCM samples                            │
│ Output: Raw PCM samples (16-bit signed, 24kHz, mono)        │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 4: Audio Effects Application                           │
├─────────────────────────────────────────────────────────────┤
│ If EFF_BITCRUSH:                                            │
│   - BitCrush(samples, count, crushFactor, gainFactor)       │
│ If EFF_DESAMPLE:                                            │
│   - Desample(samples, count, desampleRate)                  │
│ Output: Modified PCM samples                                │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 5: Opus Frame Encoding                                 │
├─────────────────────────────────────────────────────────────┤
│ Split into 480-sample frames:                               │
│   - Write sequence number (m_encodeSeq++)                   │
│   - opus_encode() to compressed data                        │
│   - Write frame length + data                               │
│ If final: Write 0xFFFF marker                               │
│ Output: Opus frame data                                     │
└─────────────────┬───────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Step 6: Steam Voice Compression                             │
├─────────────────────────────────────────────────────────────┤
│ Write: SteamID64 (8 bytes)                                  │
│ Write: CRC32 = 0 (4 bytes)                                  │
│ Write: OP_CODEC_OPUSPLC operation code                      │
│ Write: OP_SAMPLERATE + 24000                                │
│ Write: Opus frame data                                      │
│ Output: Complete Steam voice packet                         │
└─────────────────┬───────────────────────────────────────────┘
                  │
           ┌──────┴──────┐
           ▼             ▼
┌──────────────┐  ┌─────────────────┐
│ UDP Relay    │  │ Engine Callback │
│ (optional)   │  │                 │
├──────────────┤  ├─────────────────┤
│ SendPacket() │  │ Broadcast to    │
│              │  │ all clients     │
└──────────────┘  └─────────────────┘
```

### Memory Buffer Usage

**Global Buffers** (`source/main.cpp`):

```cpp
static char decompressedBuffer[20 * 1024];  // 20KB
static char recompressBuffer[20 * 1024];    // 20KB
```

**Effect Buffers** (`source/audio_effects.h`):

```cpp
static uint16_t tempBuf[10 * 1024];  // 20KB (10K samples * 2 bytes)
```

**Total Static Memory**: ~60KB

**Per-Player Codec State**:

-   OpusEncoder: ~20KB per instance
-   OpusDecoder: ~20KB per instance
-   Total: ~40KB per afflicted player

**Typical Memory Footprint**:

-   Base: 60KB (static buffers)
-   10 players with effects: 60KB + 400KB = 460KB
-   Well within acceptable limits for a binary module

## Thread Safety

**Current State**: Not thread-safe

**Implications**:

-   Hook called on engine's voice processing thread
-   Lua API called from server's main thread
-   No synchronization primitives used

**Safe Usage**:

-   Enable/disable effects during non-voice periods
-   Don't modify state during active voice chat
-   Source Engine is primarily single-threaded

**For Rust Port**:

-   Use `Arc<Mutex<EightbitState>>` for shared state
-   Consider `RwLock` for read-heavy operations
-   Lock only during state modifications, not processing

## Error Handling Strategy

**Philosophy**: Fail gracefully, never crash the server

**Fallback Pattern**:

```cpp
if (error_condition) {
    // Pass through to original function
    return detour_BroadcastVoiceData.GetTrampoline<...>()(...);
}
```

**Error Points**:

1. **Packet too small**: Pass through unmodified
2. **Decompression failure**: Pass through original
3. **Effect processing error**: Pass through unmodified
4. **Compression failure**: Pass through original
5. **Opus errors**: Reset state and continue

**No Logging**:

-   Silent failures to avoid console spam
-   Could add debug logging for development builds
-   Production: fail silently, maintain uptime

## Performance Optimizations

### 1. O(1) Player Lookup

```cpp
std::unordered_map<int, ...> afflictedPlayers;
```

-   Hash table for constant-time lookup
-   Critical for hook performance
-   Called for every voice packet

### 2. Static Buffers

-   Avoid heap allocations during processing
-   Fixed 60KB overhead
-   Pre-allocated at module load

### 3. Early Exit

```cpp
if (afflicted_players.find(uid) == afflicted_players.end()) {
    return trampoline(...);  // Immediate return
}
```

-   Unaffected players have minimal overhead
-   Only process when necessary

### 4. Minimal Copies

-   In-place effect processing where possible
-   Only one memcpy for desample effect
-   Bitcrush modifies in-place

### 5. Single-Pass Processing

-   One decode → effect → encode pipeline
-   No intermediate storage
-   Linear data flow

## Platform Differences

### Windows

-   MSVC compiler
-   WinSock2 for networking
-   COM initialization requirements
-   Different IClient offsets (x86: 181, x64: 189)

### Linux

-   GCC/Clang compiler
-   POSIX sockets
-   dlopen for third-party linking
-   Symbol-based hooking (no signature scanning)

### Architecture

-   x86: 32-bit pointers, calling conventions
-   x86-64: 64-bit pointers, different ABIs

## Third-Party Integration

**File**: `source/thirdparty.h`

**Purpose**: Optional integration with zsvoicechat module

**Function**:

```cpp
#ifdef THIRDPARTY_LINK
bool checkIfMuted(int slot);
#endif
```

**Usage**:

```cpp
if (checkIfMuted(cl->GetPlayerSlot() + 1)) {
    return trampoline(...);  // Skip processing for muted players
}
```

**Implementation**:

-   Linux only (uses dlopen)
-   Loads zsvoicechat.so if present
-   Resolves `shouldBeMuted` function
-   Graceful degradation if not found

## Extension Points

### Adding New Effects

1. Add enum to `AudioEffects` namespace
2. Implement effect function in `audio_effects.h`
3. Add case in `hook_BroadcastVoiceData`
4. Export constant to Lua

### Custom Voice Codecs

1. Implement `IVoiceCodec` interface
2. Replace `Opus_FrameDecoder` instantiation
3. Update `Init()` call with appropriate parameters

### Additional Relay Destinations

1. Modify `Net` class to support multiple sockets
2. Add list of destinations to `EightbitState`
3. Loop through destinations in broadcast code

## Future Architecture Considerations

### Multi-Effect Pipeline

```cpp
struct EffectChain {
    std::vector<std::function<void(uint16_t*, int&)>> effects;
};
```

### Async Processing

```cpp
// Move processing off voice thread
std::thread processingThread;
lockfree_queue<VoicePacket> workQueue;
```

### Recording System

```cpp
class VoiceRecorder {
    std::ofstream file;
    void WriteFrame(char* pcm, int samples);
};
```
