# Rust Migration Guide

## Overview

This guide provides detailed instructions for recreating gm_8bit in Rust, covering architecture decisions, library choices, implementation patterns, and potential improvements.

## Why Rust?

### Advantages Over C++

**Memory Safety**:

-   No buffer overflows (compile-time checks)
-   No use-after-free bugs
-   No data races (with `Send`/`Sync`)

**Modern Tooling**:

-   `cargo`: Build system, package manager, test runner
-   `rustfmt`: Automatic code formatting
-   `clippy`: Linting and best practices
-   `rustdoc`: Documentation generation

**Ecosystem**:

-   `crates.io`: Central package repository
-   Modern async runtime (Tokio)
-   Excellent networking libraries
-   Cross-platform support

**Error Handling**:

-   `Result<T, E>` type for explicit errors
-   `?` operator for error propagation
-   No silent failures or undefined behavior

### Trade-offs

**Learning Curve**: Borrow checker, lifetimes, ownership
**Compile Times**: Slower than C++ (improving)
**Binary Size**: Larger than optimized C++ (can be mitigated)
**FFI Overhead**: Interfacing with C libraries requires `unsafe`

## Project Structure

### Recommended Layout

```
gm_8bit_rs/
├── Cargo.toml                 # Package manifest
├── build.rs                   # Build script (optional)
├── src/
│   ├── lib.rs                 # Library entry point
│   ├── hook.rs                # Function hooking
│   ├── steam_voice.rs         # Steam voice protocol
│   ├── opus.rs                # Opus codec wrapper
│   ├── effects/
│   │   ├── mod.rs             # Effects module
│   │   ├── bitcrush.rs        # Bitcrush effect
│   │   └── desample.rs        # Desample effect
│   ├── network.rs             # UDP relay
│   ├── state.rs               # Global state management
│   └── lua_api.rs             # Lua bindings
├── tests/
│   ├── integration_test.rs
│   └── fixtures/
│       └── test_packets.bin
└── benches/
    └── effect_bench.rs
```

### Cargo.toml

```toml
[package]
name = "gm_8bit"
version = "2.0.0"
edition = "2021"
authors = ["Your Name <you@example.com>"]
license = "LGPL-2.1"

[lib]
crate-type = ["cdylib"]  # Dynamic library for Garry's Mod
name = "gmsv_eightbit"   # Output name

[dependencies]
# Opus codec
audiopus = "0.3"
# or: opus = "0.3"  (lower-level FFI)

# Function hooking
retour = { version = "0.3", features = ["static-detour"] }

# Error handling
thiserror = "1.0"
anyhow = "1.0"

# Networking
socket2 = "0.5"  # Low-level sockets

# Lua bindings
mlua = { version = "0.9", features = ["lua54", "vendored"] }

# Logging (optional)
log = "0.4"
env_logger = "0.11"

# Synchronization
parking_lot = "0.12"  # Faster than std::sync

# Utilities
lazy_static = "1.4"
bitflags = "2.4"

[dev-dependencies]
criterion = "0.5"  # Benchmarking
proptest = "1.4"   # Property testing

[profile.release]
opt-level = 3
lto = true          # Link-time optimization
codegen-units = 1   # Better optimization
strip = true        # Remove symbols
panic = "abort"     # Smaller binary
```

## Core Components

### 1. Global State

**File**: `src/state.rs`

```rust
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

pub struct EightbitState {
    // Effect parameters
    pub crush_factor: i32,
    pub gain_factor: f32,
    pub desample_rate: usize,

    // Network relay
    pub broadcast_enabled: bool,
    pub broadcast_ip: String,
    pub broadcast_port: u16,

    // Per-player state
    pub afflicted_players: HashMap<i32, PlayerState>,
}

pub struct PlayerState {
    pub codec: OpusCodec,
    pub effect: EffectType,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EffectType {
    None = 0,
    BitCrush = 1,
    Desample = 2,
}

impl Default for EightbitState {
    fn default() -> Self {
        Self {
            crush_factor: 350,
            gain_factor: 1.2,
            desample_rate: 2,
            broadcast_enabled: false,
            broadcast_ip: "127.0.0.1".to_string(),
            broadcast_port: 4000,
            afflicted_players: HashMap::new(),
        }
    }
}

lazy_static::lazy_static! {
    pub static ref GLOBAL_STATE: Arc<RwLock<EightbitState>> =
        Arc::new(RwLock::new(EightbitState::default()));
}

// Thread-safe accessors
pub fn with_state<F, R>(f: F) -> R
where
    F: FnOnce(&EightbitState) -> R,
{
    let state = GLOBAL_STATE.read();
    f(&state)
}

pub fn with_state_mut<F, R>(f: F) -> R
where
    F: FnOnce(&mut EightbitState) -> R,
{
    let mut state = GLOBAL_STATE.write();
    f(&mut state)
}
```

**Improvements Over C++**:

-   Thread-safe by default (RwLock)
-   No raw pointers
-   Explicit ownership

### 2. Steam Voice Protocol

**File**: `src/steam_voice.rs`

```rust
use std::io::{self, Cursor, Read, Write};
use byteorder::{LittleEndian, ReadBytesExt, WriteBytesExt};
use thiserror::Error;

#[derive(Error, Debug)]
pub enum SteamVoiceError {
    #[error("Packet too small: {0} bytes (minimum 12)")]
    PacketTooSmall(usize),

    #[error("Invalid operation code: {0}")]
    InvalidOpcode(u8),

    #[error("Buffer overflow at offset {0}")]
    BufferOverflow(usize),

    #[error("IO error: {0}")]
    Io(#[from] io::Error),
}

type Result<T> = std::result::Result<T, SteamVoiceError>;

const OP_SILENCE: u8 = 0;
const OP_CODEC_OPUSPLC: u8 = 6;
const OP_SAMPLERATE: u8 = 11;

pub struct SteamVoicePacket {
    pub steam_id: u64,
    pub crc32: u32,
    pub operations: Vec<Operation>,
}

pub enum Operation {
    Silence { samples: u16 },
    SampleRate { rate: u16 },
    OpusData { data: Vec<u8> },
}

impl SteamVoicePacket {
    pub fn parse(data: &[u8]) -> Result<Self> {
        if data.len() < 12 {
            return Err(SteamVoiceError::PacketTooSmall(data.len()));
        }

        let mut cursor = Cursor::new(data);

        let steam_id = cursor.read_u64::<LittleEndian>()?;
        let crc32 = cursor.read_u32::<LittleEndian>()?;

        let mut operations = Vec::new();
        let end_pos = data.len() - 4; // Exclude trailing CRC

        while (cursor.position() as usize) < end_pos {
            let opcode = cursor.read_u8()?;

            let op = match opcode {
                OP_SILENCE => {
                    let samples = cursor.read_u16::<LittleEndian>()?;
                    Operation::Silence { samples }
                }
                OP_SAMPLERATE => {
                    let rate = cursor.read_u16::<LittleEndian>()?;
                    Operation::SampleRate { rate }
                }
                OP_CODEC_OPUSPLC => {
                    let len = cursor.read_u16::<LittleEndian>()? as usize;
                    let pos = cursor.position() as usize;

                    if pos + len > data.len() {
                        return Err(SteamVoiceError::BufferOverflow(pos));
                    }

                    let opus_data = data[pos..pos + len].to_vec();
                    cursor.set_position((pos + len) as u64);

                    Operation::OpusData { data: opus_data }
                }
                _ => return Err(SteamVoiceError::InvalidOpcode(opcode)),
            };

            operations.push(op);
        }

        Ok(SteamVoicePacket {
            steam_id,
            crc32,
            operations,
        })
    }

    pub fn serialize(&self, opus_data: &[u8]) -> Result<Vec<u8>> {
        let mut buffer = Vec::with_capacity(1024);

        buffer.write_u64::<LittleEndian>(self.steam_id)?;
        buffer.write_u32::<LittleEndian>(0)?; // CRC placeholder
        buffer.write_u8(OP_SAMPLERATE)?;
        buffer.write_u16::<LittleEndian>(24000)?;
        buffer.write_u8(OP_CODEC_OPUSPLC)?;
        buffer.write_u16::<LittleEndian>(opus_data.len() as u16)?;
        buffer.write_all(opus_data)?;
        buffer.write_u32::<LittleEndian>(0)?; // Trailing CRC

        Ok(buffer)
    }
}
```

**Improvements**:

-   Zero-copy parsing with slices
-   Proper error handling
-   Safe buffer access (bounds checking)
-   No macros needed

### 3. Opus Codec Integration

**File**: `src/opus.rs`

```rust
use audiopus::coder::{Decoder, Encoder};
use audiopus::{Application, Channels, SampleRate};
use thiserror::Error;

const FRAME_SIZE: usize = 480;
const SAMPLE_RATE: u32 = 24000;

#[derive(Error, Debug)]
pub enum OpusError {
    #[error("Opus encode failed: {0}")]
    EncodeFailed(#[from] audiopus::Error),

    #[error("Invalid frame size: {0} (expected {FRAME_SIZE})")]
    InvalidFrameSize(usize),

    #[error("Sequence error: expected {expected}, got {actual}")]
    SequenceError { expected: u16, actual: u16 },
}

type Result<T> = std::result::Result<T, OpusError>;

pub struct OpusCodec {
    encoder: Encoder,
    decoder: Decoder,
    encode_seq: u16,
    decode_seq: u16,
    sample_buffer: Vec<i16>,
}

impl OpusCodec {
    pub fn new() -> Result<Self> {
        Ok(Self {
            encoder: Encoder::new(
                SampleRate::Hz24000,
                Channels::Mono,
                Application::Voip,
            )?,
            decoder: Decoder::new(SampleRate::Hz24000, Channels::Mono)?,
            encode_seq: 0,
            decode_seq: 0,
            sample_buffer: Vec::new(),
        })
    }

    pub fn compress(&mut self, samples: &[i16], final_frame: bool) -> Result<Vec<u8>> {
        // Add samples to buffer
        self.sample_buffer.extend_from_slice(samples);

        // Check if we have enough samples
        if self.sample_buffer.len() < FRAME_SIZE && !final_frame {
            return Ok(Vec::new()); // Not enough data yet
        }

        let mut output = Vec::new();
        let mut offset = 0;

        // Process complete frames
        while offset + FRAME_SIZE <= self.sample_buffer.len() {
            let frame = &self.sample_buffer[offset..offset + FRAME_SIZE];

            // Write sequence number
            output.extend_from_slice(&self.encode_seq.to_le_bytes());
            self.encode_seq = self.encode_seq.wrapping_add(1);

            // Encode frame
            let mut compressed = vec![0u8; 4000]; // Max Opus frame size
            let len = self.encoder.encode(frame, &mut compressed)?;

            // Write length and data
            output.extend_from_slice(&(len as u16).to_le_bytes());
            output.extend_from_slice(&compressed[..len]);

            offset += FRAME_SIZE;
        }

        // Keep remainder for next call
        self.sample_buffer.drain(..offset);

        // Handle final frame
        if final_frame && !self.sample_buffer.is_empty() {
            // Pad to frame size
            self.sample_buffer.resize(FRAME_SIZE, 0);

            let frame = &self.sample_buffer[..];
            output.extend_from_slice(&self.encode_seq.to_le_bytes());

            let mut compressed = vec![0u8; 4000];
            let len = self.encoder.encode(frame, &mut compressed)?;
            output.extend_from_slice(&(len as u16).to_le_bytes());
            output.extend_from_slice(&compressed[..len]);

            // Write end marker
            output.extend_from_slice(&0xFFFFu16.to_le_bytes());

            self.sample_buffer.clear();
            self.encode_seq = 0;
            self.encoder.reset_state()?;
        }

        Ok(output)
    }

    pub fn decompress(&mut self, data: &[u8]) -> Result<Vec<i16>> {
        use byteorder::{LittleEndian, ReadBytesExt};
        use std::io::Cursor;

        let mut cursor = Cursor::new(data);
        let mut output = Vec::new();

        while cursor.position() < data.len() as u64 - 2 {
            let len = cursor.read_u16::<LittleEndian>()?;

            // End marker
            if len == 0xFFFF {
                self.decoder.reset_state()?;
                self.decode_seq = 0;
                continue;
            }

            let seq = cursor.read_u16::<LittleEndian>()?;

            // Handle packet loss
            if seq < self.decode_seq {
                // Out of order - reset
                self.decoder.reset_state()?;
            } else if seq > self.decode_seq {
                // Generate PLC frames
                let lost = (seq - self.decode_seq).min(10);
                for _ in 0..lost {
                    let mut plc_frame = vec![0i16; FRAME_SIZE];
                    self.decoder.decode(None, &mut plc_frame, false)?;
                    output.extend_from_slice(&plc_frame);
                }
            }

            self.decode_seq = seq.wrapping_add(1);

            // Decode frame
            let pos = cursor.position() as usize;
            let frame_data = &data[pos..pos + len as usize];
            cursor.set_position((pos + len as usize) as u64);

            let mut decoded = vec![0i16; FRAME_SIZE];
            let samples = self.decoder.decode(Some(frame_data), &mut decoded, false)?;
            output.extend_from_slice(&decoded[..samples]);
        }

        Ok(output)
    }
}
```

**Improvements**:

-   RAII (automatic cleanup)
-   `Result<T, E>` for errors
-   Safe slice handling
-   No manual memory management

### 4. Audio Effects

**File**: `src/effects/mod.rs`

```rust
pub mod bitcrush;
pub mod desample;

pub trait AudioEffect: Send + Sync {
    fn process(&mut self, samples: &mut Vec<i16>);
    fn name(&self) -> &str;
}

pub struct EffectChain {
    effects: Vec<Box<dyn AudioEffect>>,
}

impl EffectChain {
    pub fn new() -> Self {
        Self {
            effects: Vec::new(),
        }
    }

    pub fn add<E: AudioEffect + 'static>(&mut self, effect: E) {
        self.effects.push(Box::new(effect));
    }

    pub fn process(&mut self, samples: &mut Vec<i16>) {
        for effect in &mut self.effects {
            effect.process(samples);
        }
    }
}
```

**File**: `src/effects/bitcrush.rs`

```rust
use super::AudioEffect;

pub struct BitCrush {
    pub quant: f32,
    pub gain: f32,
}

impl BitCrush {
    pub fn new(quant: f32, gain: f32) -> Self {
        Self { quant, gain }
    }
}

impl AudioEffect for BitCrush {
    fn process(&mut self, samples: &mut Vec<i16>) {
        for sample in samples.iter_mut() {
            let mut f = *sample as f32;
            f /= self.quant;
            let quantized = f as i16;
            let scaled = (quantized as f32 * self.quant) as i16;
            *sample = (scaled as f32 * self.gain)
                .clamp(i16::MIN as f32, i16::MAX as f32) as i16;
        }
    }

    fn name(&self) -> &str {
        "BitCrush"
    }
}
```

**File**: `src/effects/desample.rs`

```rust
use super::AudioEffect;

pub struct Desample {
    pub rate: usize,
}

impl Desample {
    pub fn new(rate: usize) -> Self {
        Self { rate }
    }
}

impl AudioEffect for Desample {
    fn process(&mut self, samples: &mut Vec<i16>) {
        let mut out = Vec::with_capacity(samples.len());

        for (i, &sample) in samples.iter().enumerate() {
            if i % self.rate != 0 {
                out.push(sample);
            }
        }

        *samples = out;
    }

    fn name(&self) -> &str {
        "Desample"
    }
}
```

**Improvements**:

-   Trait-based polymorphism
-   Easy to add new effects
-   Effect chaining built-in
-   Clipping prevention

### 5. Function Hooking

**File**: `src/hook.rs`

```rust
use retour::static_detour;
use std::ffi::c_void;

type SV_BroadcastVoiceData = unsafe extern "C" fn(
    *mut c_void,  // IClient*
    u32,          // nBytes
    *mut u8,      // data
    i64,          // xuid
);

static_detour! {
    static BroadcastVoiceHook: unsafe extern "C" fn(
        *mut c_void, u32, *mut u8, i64
    );
}

pub fn hook_broadcast_voice(target_addr: *mut c_void) -> Result<(), Box<dyn std::error::Error>> {
    unsafe {
        BroadcastVoiceHook
            .initialize(
                std::mem::transmute(target_addr),
                broadcast_voice_detour,
            )?
            .enable()?;
    }
    Ok(())
}

unsafe extern "C" fn broadcast_voice_detour(
    client: *mut c_void,
    nbytes: u32,
    data: *mut u8,
    xuid: i64,
) {
    // Convert raw pointer to slice
    let packet = std::slice::from_raw_parts(data, nbytes as usize);

    // Process packet
    match process_voice_packet(client, packet) {
        Ok(modified) => {
            // Call original with modified data
            // ...
        }
        Err(e) => {
            // Log error, call original
            eprintln!("Voice processing error: {}", e);
            BroadcastVoiceHook.call(client, nbytes, data, xuid);
        }
    }
}

fn process_voice_packet(
    client: *mut c_void,
    packet: &[u8],
) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    // Implementation
    Ok(packet.to_vec())
}
```

**Improvements**:

-   Type-safe function pointers
-   Safe slice conversion from raw pointers
-   Error handling in detour
-   `retour` crate handles platform differences

### 6. Lua API

**File**: `src/lua_api.rs`

```rust
use mlua::prelude::*;
use crate::state::{with_state_mut, EffectType};

pub fn register_lua_api(lua: &Lua) -> LuaResult<()> {
    let eightbit = lua.create_table()?;

    // Constants
    eightbit.set("EFF_NONE", EffectType::None as i32)?;
    eightbit.set("EFF_BITCRUSH", EffectType::BitCrush as i32)?;
    eightbit.set("EFF_DESAMPLE", EffectType::Desample as i32)?;

    // Functions
    eightbit.set("SetCrushFactor", lua.create_function(set_crush_factor)?)?;
    eightbit.set("GetCrushFactor", lua.create_function(get_crush_factor)?)?;
    eightbit.set("SetGainFactor", lua.create_function(set_gain_factor)?)?;
    eightbit.set("SetDesampleRate", lua.create_function(set_desample_rate)?)?;
    eightbit.set("EnableEffect", lua.create_function(enable_effect)?)?;
    eightbit.set("EnableBroadcast", lua.create_function(enable_broadcast)?)?;
    eightbit.set("SetBroadcastIP", lua.create_function(set_broadcast_ip)?)?;
    eightbit.set("SetBroadcastPort", lua.create_function(set_broadcast_port)?)?;

    lua.globals().set("eightbit", eightbit)?;

    Ok(())
}

fn set_crush_factor(_lua: &Lua, factor: i32) -> LuaResult<()> {
    with_state_mut(|state| {
        state.crush_factor = factor;
    });
    Ok(())
}

fn get_crush_factor(_lua: &Lua, _: ()) -> LuaResult<i32> {
    Ok(crate::state::with_state(|state| state.crush_factor))
}

fn set_gain_factor(_lua: &Lua, gain: f32) -> LuaResult<()> {
    with_state_mut(|state| {
        state.gain_factor = gain;
    });
    Ok(())
}

fn set_desample_rate(_lua: &Lua, rate: usize) -> LuaResult<()> {
    with_state_mut(|state| {
        state.desample_rate = rate;
    });
    Ok(())
}

fn enable_effect(_lua: &Lua, (userid, effect): (i32, i32)) -> LuaResult<()> {
    let effect_type = match effect {
        0 => EffectType::None,
        1 => EffectType::BitCrush,
        2 => EffectType::Desample,
        _ => return Err(LuaError::RuntimeError("Invalid effect type".to_string())),
    };

    with_state_mut(|state| {
        if effect_type == EffectType::None {
            state.afflicted_players.remove(&userid);
        } else {
            // Create or update player state
            state.afflicted_players
                .entry(userid)
                .and_modify(|ps| ps.effect = effect_type)
                .or_insert_with(|| PlayerState {
                    codec: OpusCodec::new().expect("Failed to create codec"),
                    effect: effect_type,
                });
        }
    });

    Ok(())
}

fn enable_broadcast(_lua: &Lua, enabled: bool) -> LuaResult<()> {
    with_state_mut(|state| {
        state.broadcast_enabled = enabled;
    });
    Ok(())
}

fn set_broadcast_ip(_lua: &Lua, ip: String) -> LuaResult<()> {
    with_state_mut(|state| {
        state.broadcast_ip = ip;
    });
    Ok(())
}

fn set_broadcast_port(_lua: &Lua, port: u16) -> LuaResult<()> {
    with_state_mut(|state| {
        state.broadcast_port = port;
    });
    Ok(())
}
```

**Improvements**:

-   Type-safe Lua bindings
-   Automatic type conversion
-   Error handling with `LuaResult`
-   Closures for concise code

### 7. Network Relay

**File**: `src/network.rs`

```rust
use socket2::{Domain, Socket, Type};
use std::net::SocketAddr;
use std::io;

pub struct UdpRelay {
    socket: Socket,
}

impl UdpRelay {
    pub fn new() -> io::Result<Self> {
        let socket = Socket::new(Domain::IPV4, Type::DGRAM, None)?;
        Ok(Self { socket })
    }

    pub fn send_to(&self, data: &[u8], addr: SocketAddr) -> io::Result<usize> {
        self.socket.send_to(data, &addr.into())
    }
}

// Thread-safe global instance
lazy_static::lazy_static! {
    static ref UDP_RELAY: UdpRelay = UdpRelay::new().expect("Failed to create UDP relay");
}

pub fn broadcast_packet(data: &[u8], ip: &str, port: u16) -> io::Result<()> {
    let addr: SocketAddr = format!("{}:{}", ip, port).parse()
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e))?;

    UDP_RELAY.send_to(data, addr)?;
    Ok(())
}
```

**Improvements**:

-   `socket2` for cross-platform sockets
-   Proper error propagation
-   Type-safe address parsing
-   RAII socket management

## Advanced Features

### 1. Async Processing

For non-blocking voice processing:

```rust
use tokio::sync::mpsc;
use std::sync::Arc;

struct VoicePacket {
    client_id: i32,
    data: Vec<u8>,
}

async fn voice_processor(mut rx: mpsc::Receiver<VoicePacket>) {
    while let Some(packet) = rx.recv().await {
        // Process in background
        tokio::task::spawn_blocking(move || {
            process_voice_packet(&packet.data);
        });
    }
}

// In hook
fn send_to_processor(packet: VoicePacket) {
    // Non-blocking send
    TX.try_send(packet).ok();
}
```

### 2. Configuration File

**Cargo.toml**:

```toml
[dependencies]
serde = { version = "1.0", features = ["derive"] }
toml = "0.8"
```

**Code**:

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct Config {
    pub default_crush_factor: i32,
    pub default_gain_factor: f32,
    pub udp_relay: UdpRelayConfig,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct UdpRelayConfig {
    pub enabled: bool,
    pub ip: String,
    pub port: u16,
}

impl Config {
    pub fn load(path: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let content = std::fs::read_to_string(path)?;
        Ok(toml::from_str(&content)?)
    }
}
```

### 3. Logging

```rust
use log::{info, warn, error};

fn init_logging() {
    env_logger::Builder::from_default_env()
        .filter_level(log::LevelFilter::Info)
        .init();
}

// Usage
info!("gm_8bit initialized");
warn!("Packet processing took {}ms", duration);
error!("Failed to decode Opus frame: {}", e);
```

### 4. Metrics

```rust
use std::sync::atomic::{AtomicU64, Ordering};

pub struct Metrics {
    packets_processed: AtomicU64,
    errors: AtomicU64,
    total_latency_us: AtomicU64,
}

impl Metrics {
    pub fn record_packet(&self, latency_us: u64) {
        self.packets_processed.fetch_add(1, Ordering::Relaxed);
        self.total_latency_us.fetch_add(latency_us, Ordering::Relaxed);
    }

    pub fn record_error(&self) {
        self.errors.fetch_add(1, Ordering::Relaxed);
    }

    pub fn average_latency(&self) -> f64 {
        let total = self.total_latency_us.load(Ordering::Relaxed);
        let count = self.packets_processed.load(Ordering::Relaxed);

        if count == 0 {
            0.0
        } else {
            total as f64 / count as f64
        }
    }
}
```

## Testing

### Unit Tests

```rust
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bitcrush() {
        let mut effect = BitCrush::new(350.0, 1.2);
        let mut samples = vec![1000, 2000, 3000, 4000];
        effect.process(&mut samples);

        // Verify quantization occurred
        assert_ne!(samples, vec![1000, 2000, 3000, 4000]);
    }

    #[test]
    fn test_desample() {
        let mut effect = Desample::new(2);
        let mut samples = vec![100, 200, 300, 400, 500, 600];
        effect.process(&mut samples);

        // Should skip every other sample
        assert_eq!(samples.len(), 3);
    }

    #[test]
    fn test_opus_roundtrip() {
        let mut codec = OpusCodec::new().unwrap();
        let samples: Vec<i16> = (0..480).map(|i| (i * 100) as i16).collect();

        let compressed = codec.compress(&samples, true).unwrap();
        assert!(!compressed.is_empty());

        let decompressed = codec.decompress(&compressed).unwrap();
        assert_eq!(decompressed.len(), 480);
    }
}
```

### Integration Tests

```rust
// tests/integration_test.rs
use gm_8bit::*;

#[test]
fn test_full_pipeline() {
    let mut state = EightbitState::default();
    state.crush_factor = 350;

    // Simulate voice packet
    let packet = create_test_packet();
    let result = process_voice_packet(&state, &packet);

    assert!(result.is_ok());
}
```

### Benchmarks

```rust
// benches/effect_bench.rs
use criterion::{black_box, criterion_group, criterion_main, Criterion};
use gm_8bit::effects::*;

fn bitcrush_benchmark(c: &mut Criterion) {
    let mut effect = bitcrush::BitCrush::new(350.0, 1.2);
    let mut samples = vec![1000i16; 10000];

    c.bench_function("bitcrush 10k samples", |b| {
        b.iter(|| {
            effect.process(black_box(&mut samples));
        });
    });
}

criterion_group!(benches, bitcrush_benchmark);
criterion_main!(benches);
```

## Cross-Compilation

### For Windows (from Linux)

```bash
# Install cross compiler
sudo apt-get install mingw-w64

# Add Windows target
rustup target add x86_64-pc-windows-gnu
rustup target add i686-pc-windows-gnu

# Build
cargo build --release --target x86_64-pc-windows-gnu
cargo build --release --target i686-pc-windows-gnu
```

### For Linux (32-bit from 64-bit)

```bash
# Add 32-bit target
rustup target add i686-unknown-linux-gnu

# Install 32-bit libraries
sudo apt-get install gcc-multilib

# Build
cargo build --release --target i686-unknown-linux-gnu
```

## Deployment

### Build Script

**build.sh**:

```bash
#!/bin/bash
set -e

echo "Building gm_8bit for all platforms..."

# Windows 64-bit
cargo build --release --target x86_64-pc-windows-gnu
cp target/x86_64-pc-windows-gnu/release/gmsv_eightbit.dll dist/gmsv_eightbit_win64.dll

# Windows 32-bit
cargo build --release --target i686-pc-windows-gnu
cp target/i686-pc-windows-gnu/release/gmsv_eightbit.dll dist/gmsv_eightbit_win32.dll

# Linux 64-bit
cargo build --release --target x86_64-unknown-linux-gnu
cp target/x86_64-unknown-linux-gnu/release/libgmsv_eightbit.so dist/gmsv_eightbit_linux64.so

# Linux 32-bit
cargo build --release --target i686-unknown-linux-gnu
cp target/i686-unknown-linux-gnu/release/libgmsv_eightbit.so dist/gmsv_eightbit_linux.so

echo "Build complete! Files in dist/"
```

## Migration Checklist

-   [ ] Set up Rust project structure
-   [ ] Implement Steam voice protocol parser
-   [ ] Integrate Opus codec (audiopus crate)
-   [ ] Port audio effects (bitcrush, desample)
-   [ ] Implement function hooking (retour crate)
-   [ ] Create Lua bindings (mlua crate)
-   [ ] Implement UDP relay (socket2 crate)
-   [ ] Add thread-safe state management
-   [ ] Write unit tests for all components
-   [ ] Write integration tests
-   [ ] Create benchmarks
-   [ ] Set up CI/CD (GitHub Actions)
-   [ ] Cross-compile for all platforms
-   [ ] Test on actual Garry's Mod server
-   [ ] Write updated documentation
-   [ ] Create migration guide for users

## Common Pitfalls

### 1. Unsafe Code

**Wrong**:

```rust
// Too much unsafe
unsafe {
    let ptr = data.as_ptr();
    let val = *ptr.offset(10); // Undefined if out of bounds!
}
```

**Right**:

```rust
// Minimize unsafe, validate bounds
let val = data.get(10).copied().ok_or(Error::IndexOutOfBounds)?;
```

### 2. Blocking in Async

**Wrong**:

```rust
async fn process() {
    std::thread::sleep(Duration::from_secs(1)); // Blocks entire runtime!
}
```

**Right**:

```rust
async fn process() {
    tokio::time::sleep(Duration::from_secs(1)).await; // Non-blocking
}
```

### 3. Arc Misuse

**Wrong**:

```rust
let state = Arc::new(Mutex::new(State::default()));
// Cloning Arc for every access (unnecessary)
```

**Right**:

```rust
lazy_static! {
    static ref STATE: Arc<RwLock<State>> = Arc::new(RwLock::new(State::default()));
}
// Single global instance
```

## Resources

### Documentation

-   [The Rust Book](https://doc.rust-lang.org/book/)
-   [Rust By Example](https://doc.rust-lang.org/rust-by-example/)
-   [audiopus crate](https://docs.rs/audiopus/)
-   [retour crate](https://docs.rs/retour/)
-   [mlua crate](https://docs.rs/mlua/)

### Tools

-   [cargo-edit](https://github.com/killercup/cargo-edit) - Manage dependencies
-   [cargo-watch](https://github.com/passcod/cargo-watch) - Auto-rebuild
-   [cargo-bloat](https://github.com/RazrFalcon/cargo-bloat) - Binary size analysis

### Community

-   [Rust Discord](https://discord.gg/rust-lang)
-   [r/rust](https://reddit.com/r/rust)
-   [Rust Users Forum](https://users.rust-lang.org/)

## Conclusion

Migrating gm_8bit to Rust provides:

-   **Memory safety** without garbage collection
-   **Fearless concurrency** with compile-time guarantees
-   **Modern tooling** with Cargo ecosystem
-   **Better error handling** with Result types
-   **Future-proof codebase** with active community

The migration is straightforward due to Rust's excellent FFI support and mature ecosystem for the required functionality (Opus, networking, Lua bindings).
