# Project Overview

## Introduction

**gm_8bit** is a binary module for Garry's Mod that provides real-time voice manipulation capabilities. It intercepts Steam voice data on Source Engine servers, allowing for audio effects, voice relay, and recording functionality.

## Purpose

The module enables server administrators to:

-   Apply real-time audio effects to player voices (bitcrushing, frequency reduction)
-   Relay voice packets to external applications via UDP
-   Record and process voice communications
-   Create immersive audio experiences (radio effects, voice distortion, etc.)

## Key Features

### 1. Voice Interception

-   Hooks into Source Engine's `SV_BroadcastVoiceData` function
-   Intercepts voice packets before broadcast to clients
-   Non-destructive: can pass through unmodified if needed

### 2. Real-Time Processing

-   Decompresses Opus-encoded voice data
-   Applies configurable audio effects per-player
-   Recompresses and broadcasts modified audio

### 3. Audio Effects

-   **Bitcrusher**: Reduces bit depth for lo-fi/retro sound
-   **Desampler**: Reduces sample rate for frequency distortion
-   Configurable parameters via Lua

### 4. Network Relay

-   UDP broadcast of voice packets to external applications
-   Configurable IP and port
-   Useful for recording, analysis, or external processing

### 5. Lua Integration

-   Full control via Garry's Mod Lua API
-   Per-player effect management
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
│                      Garry's Mod Server                      │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  Client Voice Input → Source Engine → SV_BroadcastVoiceData │
│                                              ↓                │
│                                         [HOOK]                │
│                                              ↓                │
│                                    ┌─────────────────┐       │
│                                    │  gm_8bit Module │       │
│                                    ├─────────────────┤       │
│                                    │ 1. Decompress   │       │
│                                    │ 2. Apply Effect │       │
│                                    │ 3. Recompress   │       │
│                                    │ 4. Broadcast    │       │
│                                    └─────────────────┘       │
│                                         ↓         ↓           │
│                              Broadcast to    UDP Relay       │
│                                 Clients      (optional)       │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow

1. **Capture**: Player speaks, client sends Opus-compressed voice packet
2. **Intercept**: Hook captures packet before engine broadcast
3. **Decode**: Steam Voice format decompressed to Opus frames
4. **Extract**: Opus frames decoded to raw PCM samples (16-bit signed)
5. **Transform**: Audio effects applied to PCM data
6. **Encode**: Modified PCM encoded back to Opus frames
7. **Package**: Opus frames packaged in Steam Voice format
8. **Distribute**: Broadcast to all clients + optional UDP relay

## Performance Characteristics

### Latency

-   **Processing overhead**: ~2-5ms per voice packet
-   **Frame size**: 20ms (480 samples @ 24kHz)
-   **Total latency**: Minimal impact on voice quality

### CPU Usage

-   **Idle**: Negligible when no voice activity
-   **Active**: ~0.5-2% per speaking player (varies by effect)
-   **Scalability**: Linear with number of speaking players

### Memory

-   **Static buffers**: ~60KB (decompression, compression, effects)
-   **Per-player state**: ~40KB per afflicted player (Opus encoder/decoder)
-   **Typical usage**: <1MB for 10 players with effects

## Use Cases

### 1. Radio Effects

```lua
-- Make player sound like they're on a radio
eightbit.SetCrushFactor(350)
eightbit.SetGainFactor(1.2)
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
```

### 2. Voice Recording

```lua
-- Enable UDP relay for external recording
eightbit.SetBroadcastIP("127.0.0.1")
eightbit.SetBroadcastPort(4000)
eightbit.EnableBroadcast(true)
```

### 3. Damaged Equipment

```lua
-- Simulate damaged communication device
eightbit.SetDesampleRate(3)
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_DESAMPLE)
```

### 4. Proximity Effects

```lua
-- Apply effects based on distance from radio tower
hook.Add("Think", "VoiceEffects", function()
    for _, ply in ipairs(player.GetAll()) do
        local dist = ply:GetPos():Distance(radioTowerPos)
        if dist > 500 then
            eightbit.SetCrushFactor(math.floor(dist / 2))
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        else
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
        end
    end
end)
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

### 3. Effect Stacking

-   Only one effect per player at a time
-   Cannot chain effects (bitcrush + desample)
-   Would require effect pipeline redesign

### 4. Network Protocol

-   Relies on Source Engine voice protocol
-   No encryption/authentication for UDP relay
-   Steam Voice format is proprietary

## Security Considerations

### 1. Buffer Overflow Protection

-   All buffer operations checked with bounds macros
-   Fixed-size buffers prevent heap exhaustion
-   Safe fallback on malformed packets

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
