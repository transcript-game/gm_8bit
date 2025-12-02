# Lua API Reference

gm_8bit exposes a minimal Lua API through the `eightbit` global table. The module now focuses solely on relaying Steam voice packets over UDP; there are no audio effects or codec controls.

**Namespace**: `eightbit`  
**Available In**: Server-side Lua only

## Functions

### eightbit.EnableBroadcast

Enable or disable UDP relay of intercepted voice packets. Broadcasting defaults to `true` when the module loads.

```lua
eightbit.EnableBroadcast(boolean enabled)
```

- `enabled`: `true` to forward packets, `false` to pause forwarding.

Example:

```lua
-- Pause relaying temporarily
eightbit.EnableBroadcast(false)

-- Resume relaying
eightbit.EnableBroadcast(true)
```

### eightbit.SetBroadcastIP

Set the destination IPv4 address for the UDP stream.

```lua
eightbit.SetBroadcastIP(string ip)
```

- `ip`: Target IPv4 address (default `"127.0.0.1"`).

### eightbit.SetBroadcastPort

Set the destination UDP port for the relay.

```lua
eightbit.SetBroadcastPort(number port)
```

- `port`: UDP port number (default `4000`).

## Packet Format

Relayed packets mirror the Steam voice packets the server receives. gm_8bit overwrites the leading SteamID64 with the authoritative server-side value and sends the packet unchanged to the configured IP/port.
