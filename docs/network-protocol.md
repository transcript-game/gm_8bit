# Network Protocol

## UDP Relay System

### Overview

gm_8bit can relay voice packets to external applications via UDP. This enables voice recording, analysis, or external processing without modifying the game client.

### Protocol Design

**Transport**: UDP (User Datagram Protocol)
**Port**: Configurable (default: 4000)
**Destination**: Configurable IP address (default: 127.0.0.1)
**Encryption**: None
**Authentication**: None

### Packet Format

The relayed packet is identical to the Steam voice packet format:

```
Offset | Size | Field           | Description
-------|------|-----------------|----------------------------------
0      | 8    | SteamID64       | Server-verified player Steam ID
8      | 4    | CRC32           | Checksum (typically 0)
12     | 1    | Operation Code  | OP_SAMPLERATE or OP_CODEC_OPUSPLC
13     | var  | Payload         | Operation-specific data
...    | var  | More Operations | Additional opcodes
end    | 4    | Trailing CRC    | Final checksum (typically 0)
```

**Key Difference from Original**: The SteamID64 at offset 0 is replaced with the server-authoritative SteamID64 extracted from the `IClient` structure, preventing client spoofing.

### Relay Behavior

**When Enabled**:

1. Hook intercepts voice packet
2. Extracts real SteamID64 from server memory
3. Copies packet to buffer, replacing SteamID64
4. Sends via UDP to configured IP:Port
5. Continues normal processing (decompress, effects, etc.)

**When Disabled**:

-   No network overhead
-   Packets processed normally

### Configuration

**Enable/Disable**:

```lua
eightbit.EnableBroadcast(true)   -- Enable
eightbit.EnableBroadcast(false)  -- Disable
```

**Set Destination**:

```lua
eightbit.SetBroadcastIP("192.168.1.100")
eightbit.SetBroadcastPort(8080)
```

### Example Receiver (Python)

```python
import socket
import struct

UDP_IP = "127.0.0.1"
UDP_PORT = 4000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Listening on {UDP_IP}:{UDP_PORT}")

while True:
    data, addr = sock.recvfrom(2048)

    if len(data) < 12:
        print("Packet too small")
        continue

    # Parse header
    steamid, crc32 = struct.unpack('<QI', data[:12])

    print(f"SteamID: {steamid}, Size: {len(data)} bytes")

    # Write to file for later processing
    with open(f"voice_{steamid}_{int(time.time())}.bin", "wb") as f:
        f.write(data)
```

### Security Considerations

**No Encryption**: Packets sent in plaintext

-   **Implication**: Anyone on network can intercept voice data
-   **Mitigation**: Only relay to localhost or trusted networks

**No Authentication**: No verification of receiver

-   **Implication**: Anyone can listen on the port
-   **Mitigation**: Use firewall rules to restrict access

**SteamID Verification**: Server provides authentic SteamID

-   **Prevents**: Client impersonation
-   **Reliable**: Cannot be spoofed by malicious clients

### Firewall Configuration

**Windows**:

```powershell
# Allow outbound UDP on port 4000
New-NetFirewallRule -DisplayName "gm_8bit UDP Relay" -Direction Outbound -Protocol UDP -LocalPort 4000 -Action Allow
```

**Linux (iptables)**:

```bash
# Allow outbound UDP on port 4000
sudo iptables -A OUTPUT -p udp --dport 4000 -j ACCEPT
```

**Linux (ufw)**:

```bash
# Allow outbound to specific IP
sudo ufw allow out to 192.168.1.100 port 4000 proto udp
```

### Performance Impact

**CPU Overhead**: ~0.01-0.05ms per packet
**Network Bandwidth**: ~1-5 KB/s per speaking player
**Memory**: Negligible (uses existing buffer)

**Example**:

-   10 players speaking simultaneously
-   Average packet: 336 bytes
-   Frequency: ~50 packets/second per player
-   Total bandwidth: 336 _ 50 _ 10 = 168 KB/s (~1.3 Mbps)

### Use Cases

#### 1. Voice Recording

**Receiver**: Writes packets to disk
**Processing**: Decode Opus frames, convert to WAV/MP3
**Storage**: Time-stamped files per player

#### 2. Voice Analysis

**Receiver**: Real-time speech-to-text
**Processing**: Feed Opus frames to Whisper/DeepSpeech
**Output**: Text transcripts for chat logs

#### 3. External Effects

**Receiver**: Custom DSP pipeline
**Processing**: Advanced effects (reverb, pitch shift, noise reduction)
**Return**: Modified packets back to server (requires custom protocol)

#### 4. Monitoring

**Receiver**: Dashboard application
**Metrics**: Active speakers, audio levels, packet loss
**Alerts**: Unusual voice activity, potential exploits

### Packet Loss Handling

**UDP Characteristics**:

-   Unreliable delivery (packets may be lost)
-   No retransmission
-   Out-of-order delivery possible

**Receiver Responsibilities**:

1. Handle missing packets gracefully
2. Use sequence numbers for ordering
3. Implement buffering for jitter
4. Generate PLC for gaps (if decoding)

**Sequence Tracking**:

-   Opus frames include sequence numbers
-   Detect gaps: `if current_seq > last_seq + 1`
-   Generate PLC: `opus_decode(decoder, NULL, 0, ...)`

### Alternative Protocols

#### TCP (Not Recommended)

**Advantages**:

-   Reliable delivery
-   In-order packets
-   Error correction

**Disadvantages**:

-   Higher latency (retransmission delays)
-   Connection overhead
-   Not suitable for real-time voice

#### Unix Domain Sockets (Linux Only)

**Advantages**:

-   Lower overhead than IP sockets
-   Better security (file permissions)
-   Faster for local IPC

**Example**:

```rust
use std::os::unix::net::UnixDatagram;

let sock = UnixDatagram::bind("/tmp/gm_8bit.sock")?;
sock.send_to(data, "/tmp/voice_receiver.sock")?;
```

#### Shared Memory (Advanced)

**Advantages**:

-   Zero-copy IPC
-   Extremely low latency
-   High throughput

**Disadvantages**:

-   Complex synchronization
-   Platform-specific
-   Requires separate receiver process

## Implementation Details

### C++ Code

**File**: `source/net.cpp`

```cpp
void Net::SendPacket(const char* ip, uint16_t port, char* data, int len) {
    sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    sendto(sock, data, len, 0, (sockaddr*)&dest, sizeof(dest));
}
```

**Features**:

-   Blocking send (acceptable for small packets)
-   No error checking (fire-and-forget)
-   IPv4 only

### Rust Implementation

```rust
use socket2::{Domain, Socket, Type, SockAddr};
use std::net::SocketAddr;

pub struct UdpRelay {
    socket: Socket,
}

impl UdpRelay {
    pub fn new() -> io::Result<Self> {
        let socket = Socket::new(Domain::IPV4, Type::DGRAM, None)?;
        Ok(Self { socket })
    }

    pub fn send_to(&self, data: &[u8], addr: &str, port: u16) -> io::Result<()> {
        let sockaddr: SocketAddr = format!("{}:{}", addr, port)
            .parse()
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;

        self.socket.send_to(data, &sockaddr.into())?;
        Ok(())
    }
}
```

**Improvements**:

-   Error propagation
-   Type-safe addresses
-   Cross-platform (socket2 crate)

## Troubleshooting

### Packets Not Received

**Check**:

1. Receiver listening on correct port?

    ```bash
    # Linux
    netstat -ulnp | grep 4000

    # Windows
    netstat -an | findstr :4000
    ```

2. Firewall blocking?

    ```bash
    # Temporarily disable to test
    sudo ufw disable  # Linux
    ```

3. Correct IP address?

    ```lua
    print(eightbit.GetBroadcastIP())  -- Check current setting
    ```

4. Broadcast enabled?
    ```lua
    if not eightbit.IsBroadcastEnabled() then
        eightbit.EnableBroadcast(true)
    end
    ```

### High Packet Loss

**Causes**:

-   Network congestion
-   CPU overload on receiver
-   UDP buffer overflow

**Solutions**:

-   Increase receiver buffer size:
    ```python
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024*1024)  # 1MB
    ```
-   Use localhost for testing
-   Reduce voice quality (lower bitrate)

### Performance Degradation

**Symptoms**:

-   Server lag when broadcast enabled
-   High CPU usage

**Solutions**:

-   Check destination is reachable (avoid timeout)
-   Use localhost instead of remote IP
-   Disable broadcast when not needed
-   Profile with `perf` (Linux) or Performance Monitor (Windows)

## Advanced Topics

### Encrypted Relay

**Using TLS/DTLS**:

```rust
use rustls::ClientConfig;

// Configure TLS
let config = ClientConfig::builder()
    .with_safe_defaults()
    .with_root_certificates(root_store)
    .with_no_client_auth();

// Send encrypted
let mut conn = rustls::ClientConnection::new(config, server_name)?;
// ... encrypt and send data
```

**Trade-offs**:

-   Added security
-   Increased latency
-   CPU overhead

### Multi-Destination Relay

**Broadcast to Multiple Receivers**:

```lua
local receivers = {
    {ip = "192.168.1.100", port = 4000},
    {ip = "192.168.1.101", port = 4000},
    {ip = "10.0.0.50", port = 8080},
}

for _, recv in ipairs(receivers) do
    eightbit.SetBroadcastIP(recv.ip)
    eightbit.SetBroadcastPort(recv.port)
    -- Would require modification to support multiple destinations
end
```

**Implementation**:

```rust
struct UdpRelay {
    socket: Socket,
    destinations: Vec<SocketAddr>,
}

impl UdpRelay {
    pub fn broadcast(&self, data: &[u8]) -> io::Result<()> {
        for dest in &self.destinations {
            self.socket.send_to(data, &dest.into())?;
        }
        Ok(())
    }
}
```

### Compression

**Before Relay**:

```rust
use flate2::write::GzEncoder;
use flate2::Compression;

let mut encoder = GzEncoder::new(Vec::new(), Compression::fast());
encoder.write_all(data)?;
let compressed = encoder.finish()?;

// Send compressed data
relay.send_to(&compressed, addr)?;
```

**Trade-offs**:

-   Reduced bandwidth (~30-50% savings)
-   Increased CPU usage
-   Receiver must decompress
