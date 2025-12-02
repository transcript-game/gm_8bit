# Project Overview

## Introduction

**gm_8bit** is a binary module for Garry's Mod that intercepts Steam voice packets and relays them over UDP. It focuses exclusively on streaming captured voice to external consumers—no in-engine audio effects or Opus processing remain.

## Purpose

The module enables server administrators to:

-   Relay voice packets to external applications via UDP
-   Record and process voice communications outside the game server
-   Feed voice data into analytics, transcription, or streaming pipelines

## Key Features

### 1. Voice Interception

-   Hooks into Source Engine's `SV_BroadcastVoiceData` function
-   Intercepts voice packets before broadcast to clients
-   Non-destructive: always forwards packets to the game unchanged

### 2. Network Relay

-   UDP broadcast of voice packets to external applications
-   Configurable IP and port
-   Useful for recording, analysis, or external processing

### 3. Lua Integration

-   Full control via Garry's Mod Lua API
-   Runtime configuration changes

## Technical Stack

### Languages

-   **C++**: Core module implementation
-   **Lua**: Server-side scripting interface

### Libraries

-   **Opus Codec**: Audio compression/decompression (24kHz, mono)
-   **garrysmod_common**: SDK for binary modules
-   **Detouring**: Function hooking library

### Platforms

-   Windows (x86, x86-64)
-   Linux (x86-64)

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      Garry's Mod Server                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Client Voice Input → Source Engine → SV_BroadcastVoiceData │
│                                              ↓              │
│                                         [HOOK]              │
│                                              ↓              │
│                                    ┌─────────────────┐     │
│                                    │  gm_8bit Module │     │
│                                    ├─────────────────┤     │
│                                    │ 1. Stamp SteamID│     │
│                                    │ 2. Forward UDP  │     │
│                                    │ 3. Pass-through │     │
│                                    └─────────────────┘     │
│                                         ↓        ↓         │
│                              Broadcast to   UDP Relay      │
│                                 Clients     (default on)    │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Capture**: Player speaks, client sends Steam voice packet
2. **Intercept**: Hook captures packet before engine broadcast
3. **Annotate**: Module stamps packet with authoritative SteamID64
4. **Relay**: Packet forwarded via UDP to configured IP/port
5. **Broadcast**: Original packet continues through Source Engine

## Performance Characteristics

### Latency & Overhead

-   Relaying adds negligible CPU cost; packet contents are untouched
-   Voice broadcast path remains default Source Engine behavior

### CPU Usage

-   **Idle**: Negligible when no voice activity
-   **Active**: Primarily socket send overhead
-   **Scalability**: Linear with number of speaking players

### Memory

-   **Static buffers**: ~20KB for packet staging
-   **Per-player state**: None (no codec instances kept)
-   **Typical usage**: Well under 1MB

## Use Cases

### 1. Voice Recording

```lua
-- Enable UDP relay for external recording
eightbit.SetBroadcastIP("127.0.0.1")
eightbit.SetBroadcastPort(4000)
eightbit.EnableBroadcast(true)
```

## Limitations

### 1. Server-Side Only

-   Module runs on game server, not client
-   Cannot modify outgoing voice from clients
-   All processing happens at broadcast stage

### 2. Format Constraints

-   Fixed 24kHz sample rate (Garry's Mod limitation)
-   Mono only (single channel)
-   20ms frame size (480 samples)

### 3. Network Protocol

-   Relies on Source Engine voice protocol
-   No encryption/authentication for UDP relay
-   Steam Voice format is proprietary

## Security Considerations

### 1. Buffer Overflow Protection

-   Fixed-size buffers prevent heap exhaustion
-   Copies are bounded by incoming packet size

### 2. UDP Relay

-   No authentication on relay packets
-   Should only relay to localhost or trusted networks
-   Consider VPN/firewall rules for production

### 3. Malformed Packets

-   Invalid packets passed through unmodified
-   No crashes on corrupted voice data
-   Opus decoder handles errors gracefully

## Future Enhancements

### Potential Features

-   Effect chaining/pipeline
-   Configurable frame size (10ms, 40ms, 60ms)
-   Stereo support (if Source Engine adds support)
-   Voice activity detection (VAD) integration
-   Built-in recording to disk
-   Encryption for UDP relay
-   Multi-destination relay

### Rust Port Benefits

-   Memory safety guarantees
-   Fearless concurrency
-   Better error handling
-   Modern toolchain and dependencies
-   Cross-platform binary distribution via Cargo

## Resources

### Documentation

-   [Architecture Details](architecture.md)
-   [Lua API Reference](lua-api.md)
-   [Build Instructions](build-system.md)

### External References

-   [Opus Codec Documentation](https://opus-codec.org/docs/)
-   [Source SDK Documentation](https://developer.valvesoftware.com/wiki/SDK_Docs)
-   [garrysmod_common](https://github.com/danielga/garrysmod_common)

### Related Projects

-   [zsvoicechat](https://github.com/ZeqMacaw/zsvoicechat) - Alternative voice manipulation
-   [Opus](https://opus-codec.org/) - Reference codec implementation
