# Architecture

gm_8bit is a lightweight hook that mirrors Steam voice packets over UDP while leaving the Source Engine's normal broadcast untouched.

## Data Flow

1. **Intercept**: Detour `SV_BroadcastVoiceData` (engine symbol) on the server.
2. **Stamp**: Read the authoritative SteamID64 from `IClient` and overwrite the packet's leading SteamID64 field.
3. **Relay**: If broadcasting is enabled (default), send the packet via UDP to the configured IP/port.
4. **Forward**: Call the original `SV_BroadcastVoiceData` so clients still receive unmodified voice.

## Components

- **Hook Handler** (`source/main.cpp`): Performs stamping, relay decision, and trampoline call.
- **UDP Relay** (`source/net.cpp`): Manages a single IPv4 UDP socket and sends packets to the configured destination.
- **State** (`source/eightbit_state.h`): Holds broadcast toggle, destination IP, and port—no per-player or codec state.
- **Third-Party Mute (Linux optional)** (`source/thirdparty.h`): Skips relaying muted players if an external mute provider is loaded.

## Configuration Surface (Lua)

- `eightbit.EnableBroadcast(bool enabled)` — Defaults to `true`.
- `eightbit.SetBroadcastIP(string ip)` — Defaults to `127.0.0.1`.
- `eightbit.SetBroadcastPort(number port)` — Defaults to `4000`.

## Performance Notes

- Work per packet is bounded: a small memcpy for stamping plus one UDP send.
- No decoding, effects, or per-player codec allocations remain.
- Buffer size guard prevents relaying packets larger than the internal scratch buffer.
