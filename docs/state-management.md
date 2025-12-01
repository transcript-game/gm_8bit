# State Management

## Overview

gm_8bit maintains global state for effect parameters, network configuration, and per-player codec instances. Proper state management is critical for thread safety, memory efficiency, and correct behavior.

## Global State Structure

### C++ Implementation

**File**: `source/eightbit_state.h`

```cpp
struct EightbitState {
    // Effect parameters (global)
    int crushFactor = 350;
    float gainFactor = 1.2;
    int desampleRate = 2;

    // Network relay configuration
    bool broadcastPackets = false;
    uint16_t port = 4000;
    std::string ip = "127.0.0.1";

    // Per-player state
    std::unordered_map<int, std::tuple<IVoiceCodec*, int>> afflictedPlayers;
    // Map: UserID → (Codec Instance, Effect Type)
};
```

**Global Instance**:

```cpp
// source/main.cpp
EightbitState* g_eightbit = nullptr;

GMOD_MODULE_OPEN() {
    g_eightbit = new EightbitState();
    // ...
}

GMOD_MODULE_CLOSE() {
    delete g_eightbit;
    // ...
}
```

## State Lifetime

### Initialization

**When**: Module load (`GMOD_MODULE_OPEN`)
**Process**:

1. Allocate `EightbitState` on heap
2. Set default values (constructors)
3. Initialize network handler
4. Set up hook

**Code**:

```cpp
GMOD_MODULE_OPEN() {
    g_eightbit = new EightbitState();
    net_handl = new Net();

    // Hook setup
    detour_BroadcastVoiceData.Create(...);
    detour_BroadcastVoiceData.Enable();

    return 0;
}
```

### Cleanup

**When**: Module unload (`GMOD_MODULE_CLOSE`)
**Process**:

1. Disable hook
2. Delete all player codecs
3. Delete network handler
4. Delete global state

**Code**:

```cpp
GMOD_MODULE_CLOSE() {
    detour_BroadcastVoiceData.Disable();
    detour_BroadcastVoiceData.Destroy();

    // Free all codec instances
    for (auto& p : g_eightbit->afflictedPlayers) {
        IVoiceCodec* codec = std::get<0>(p.second);
        if (codec != nullptr) {
            delete codec;
        }
    }

    delete net_handl;
    delete g_eightbit;

    return 0;
}
```

## Per-Player State

### Structure

```cpp
std::unordered_map<int, std::tuple<IVoiceCodec*, int>> afflictedPlayers;
```

**Key**: UserID (from `Player:UserID()`)
**Value**: Tuple of:

1. `IVoiceCodec*`: Opus encoder/decoder instance
2. `int`: Effect type (EFF_NONE, EFF_BITCRUSH, EFF_DESAMPLE)

### Adding Player

**Trigger**: `eightbit.EnableEffect(userid, effect)` with effect != EFF_NONE

**Process**:

```cpp
if (eff != AudioEffects::EFF_NONE) {
    IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
    codec->Init(5, 24000);

    afflicted_players.insert(
        std::pair<int, std::tuple<IVoiceCodec*, int>>(
            id,
            std::tuple<IVoiceCodec*, int>(codec, eff)
        )
    );
}
```

**Memory Allocation**:

-   `new Opus_FrameDecoder()`: ~40KB per player
-   Opus internal state: ~20KB encoder + ~20KB decoder

### Updating Player

**Trigger**: `eightbit.EnableEffect(userid, new_effect)` when player already has effect

**Process**:

```cpp
if (afflicted_players.find(id) != afflicted_players.end()) {
    if (eff == AudioEffects::EFF_NONE) {
        // Remove player
        IVoiceCodec* codec = std::get<0>(afflicted_players.at(id));
        delete codec;
        afflicted_players.erase(id);
    } else {
        // Update effect type (keep codec)
        std::get<1>(afflicted_players.at(id)) = eff;
    }
}
```

**Optimization**: Codec instance reused when changing effect types

### Removing Player

**Trigger**:

-   `eightbit.EnableEffect(userid, EFF_NONE)`
-   Player disconnect (manual cleanup recommended)

**Process**:

```cpp
IVoiceCodec* codec = std::get<0>(afflicted_players.at(id));
delete codec;  // Calls destructor
afflicted_players.erase(id);
```

**Destructor** (`Opus_FrameDecoder`):

```cpp
~Opus_FrameDecoder() {
    opus_decoder_destroy(dec);
    opus_encoder_destroy(enc);
}
```

Frees Opus internal memory.

## Thread Safety

### Current Implementation

**Not Thread-Safe**: No synchronization primitives

**Access Pattern**:

-   **Hook**: Called from engine's voice thread
-   **Lua API**: Called from server's main thread

**Why It Works**:

-   Source Engine is primarily single-threaded
-   Voice processing and Lua execution don't overlap (usually)

**Potential Issues**:

-   Enabling effect during voice packet processing
-   Race condition: Reading while writing

### Rust Migration (Thread-Safe)

```rust
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

pub struct EightbitState {
    // Effect parameters
    pub crush_factor: AtomicI32,
    pub gain_factor: AtomicF32,  // Or use RwLock<f32>

    // Per-player state
    pub afflicted_players: HashMap<i32, PlayerState>,
}

lazy_static::lazy_static! {
    static ref GLOBAL_STATE: Arc<RwLock<EightbitState>> =
        Arc::new(RwLock::new(EightbitState::default()));
}

// Read access
pub fn with_state<F, R>(f: F) -> R
where
    F: FnOnce(&EightbitState) -> R,
{
    let state = GLOBAL_STATE.read();
    f(&state)
}

// Write access
pub fn with_state_mut<F, R>(f: F) -> R
where
    F: FnOnce(&mut EightbitState) -> R,
{
    let mut state = GLOBAL_STATE.write();
    f(&mut state)
}
```

**Usage**:

```rust
// Hook (read)
let crush_factor = with_state(|state| state.crush_factor);

// Lua API (write)
with_state_mut(|state| {
    state.crush_factor = 500;
});
```

**Benefits**:

-   Multiple readers simultaneously (RwLock)
-   Exclusive writer access
-   No data races (compile-time guarantee)

## Memory Management

### Current Footprint

**Static**:

-   `EightbitState` struct: ~48 bytes
-   Global buffers: ~60KB
-   Network handler: ~32 bytes

**Dynamic** (per player):

-   `IVoiceCodec*`: 8 bytes (pointer)
-   `Opus_FrameDecoder`: ~40KB
    -   OpusEncoder: ~20KB
    -   OpusDecoder: ~20KB
    -   Sample buffer: ~2KB

**Total for 10 players with effects**: ~400KB dynamic + 60KB static = ~460KB

### Memory Leaks Prevention

**Potential Leaks**:

1. Codec not deleted when player disconnects
2. Hook disabled while codecs still allocated
3. Module unloaded without cleanup

**Prevention**:

**1. Explicit Cleanup**:

```lua
hook.Add("PlayerDisconnected", "EightbitCleanup", function(ply)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
end)
```

**2. Module Close**:

```cpp
GMOD_MODULE_CLOSE() {
    // Delete all codecs before module unload
    for (auto& p : g_eightbit->afflictedPlayers) {
        IVoiceCodec* codec = std::get<0>(p.second);
        delete codec;
    }
    delete g_eightbit;
}
```

**3. RAII (Rust)**:

```rust
impl Drop for OpusCodec {
    fn drop(&mut self) {
        // Automatically frees Opus resources
    }
}
```

## State Synchronization

### Between Components

**Lua → Hook**:

```
Lua API sets state → Hook reads state next packet
```

No explicit synchronization needed (eventual consistency acceptable).

**Hook → Lua**:

```
Hook processes packet → Lua queries state later
```

Read-only access from Lua (GetCrushFactor).

### Eventual Consistency

**Example**:

```lua
eightbit.SetCrushFactor(500)
-- May take 1-2 packets (~20-40ms) to take effect
```

**Acceptable because**:

-   Audio effects tolerate brief inconsistency
-   Human ear smooths transitions
-   Alternative would require locks (performance cost)

## Configuration Persistence

### Current Implementation

**None**: State lost on server restart

**Recommended** (Lua):

```lua
-- Save on shutdown
hook.Add("ShutDown", "SaveEightbitState", function()
    local state = {
        crushFactor = eightbit.GetCrushFactor(),
        gainFactor = ...,  -- Need getter
    }
    file.Write("eightbit_config.json", util.TableToJSON(state))
end)

-- Load on startup
hook.Add("Initialize", "LoadEightbitState", function()
    if file.Exists("eightbit_config.json", "DATA") then
        local json = file.Read("eightbit_config.json", "DATA")
        local state = util.JSONToTable(json)

        eightbit.SetCrushFactor(state.crushFactor)
        eightbit.SetGainFactor(state.gainFactor)
    end
end)
```

### Rust Implementation

```rust
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct Config {
    pub crush_factor: i32,
    pub gain_factor: f32,
    pub desample_rate: usize,
}

impl Config {
    pub fn save(&self, path: &str) -> Result<(), Box<dyn Error>> {
        let json = serde_json::to_string_pretty(self)?;
        std::fs::write(path, json)?;
        Ok(())
    }

    pub fn load(path: &str) -> Result<Self, Box<dyn Error>> {
        let json = std::fs::read_to_string(path)?;
        Ok(serde_json::from_str(&json)?)
    }
}
```

## State Validation

### Range Checking

**Current**: No validation

**Recommended**:

```cpp
LUA_FUNCTION_STATIC(eightbit_crush) {
    int factor = (int)LUA->GetNumber(1);

    // Validate range
    if (factor < 1 || factor > 10000) {
        LUA->ThrowError("Crush factor must be 1-10000");
        return 0;
    }

    g_eightbit->crushFactor = factor;
    return 0;
}
```

**Rust**:

```rust
fn set_crush_factor(_lua: &Lua, factor: i32) -> LuaResult<()> {
    if !(1..=10000).contains(&factor) {
        return Err(LuaError::RuntimeError(
            "Crush factor must be 1-10000".to_string()
        ));
    }

    with_state_mut(|state| {
        state.crush_factor = factor;
    });

    Ok(())
}
```

### Type Safety

**C++**: Runtime type errors possible

```cpp
int factor = (int)LUA->GetNumber(1);  // What if it's a string?
```

**Rust**: Compile-time type safety

```rust
fn set_crush_factor(_lua: &Lua, factor: i32) -> LuaResult<()> {
    // `factor` is guaranteed to be i32 by mlua
}
```

## Performance Optimization

### Lookup Performance

**HashMap vs Vector**:

```cpp
// Current: O(1) lookup
std::unordered_map<int, ...> afflictedPlayers;

// Alternative: O(n) lookup
std::vector<std::pair<int, ...>> afflictedPlayers;
```

**Benchmark** (1000 players):

-   HashMap: ~50ns per lookup
-   Vector: ~500ns per lookup (linear scan)

**Conclusion**: HashMap is correct choice for O(1) performance.

### Memory Pool

**Current**: Each codec allocated individually

```cpp
IVoiceCodec* codec = new SteamOpus::Opus_FrameDecoder();
```

**Alternative**: Pre-allocate pool

```cpp
class CodecPool {
    std::vector<IVoiceCodec*> available;

    IVoiceCodec* acquire() {
        if (available.empty()) {
            return new SteamOpus::Opus_FrameDecoder();
        }
        IVoiceCodec* codec = available.back();
        available.pop_back();
        codec->ResetState();
        return codec;
    }

    void release(IVoiceCodec* codec) {
        available.push_back(codec);
    }
};
```

**Benefits**:

-   Reduce allocation overhead
-   Better cache locality
-   Faster enable/disable

**Trade-offs**:

-   More complex
-   Memory not freed until pool destroyed

## Debugging

### State Inspection

**Lua Console**:

```lua
PrintTable(eightbit)  -- Show all functions/constants

print("Crush factor:", eightbit.GetCrushFactor())

-- Count afflicted players
local count = 0
for _, ply in ipairs(player.GetAll()) do
    -- No direct way to query if player has effect
    -- Would need additional API: eightbit.HasEffect(userid)
end
```

**Recommended API Addition**:

```cpp
LUA_FUNCTION_STATIC(eightbit_haseffect) {
    int userid = LUA->GetNumber(1);
    bool has = g_eightbit->afflictedPlayers.find(userid) !=
               g_eightbit->afflictedPlayers.end();
    LUA->PushBool(has);
    return 1;
}
```

### Memory Profiling

**Valgrind** (Linux):

```bash
valgrind --leak-check=full srcds_linux -game garrysmod ...
```

**Visual Studio** (Windows):

-   Debug → Performance Profiler
-   Select "Memory Usage"
-   Start profiling

## Best Practices

1. **Always cleanup on disconnect**:

    ```lua
    hook.Add("PlayerDisconnected", "Cleanup", function(ply)
        eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
    end)
    ```

2. **Validate parameters**:

    ```lua
    local factor = math.Clamp(tonumber(factor) or 350, 1, 10000)
    eightbit.SetCrushFactor(factor)
    ```

3. **Use constants**:

    ```lua
    local EFF = eightbit.EFF_BITCRUSH  -- Not magic number
    ```

4. **Check player validity**:

    ```lua
    if IsValid(ply) and ply:IsPlayer() then
        eightbit.EnableEffect(ply:UserID(), effect)
    end
    ```

5. **Document state dependencies**:
    ```lua
    -- SetCrushFactor must be called BEFORE EnableEffect
    eightbit.SetCrushFactor(350)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
    ```
