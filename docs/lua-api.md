# Lua API Reference

## Overview

gm_8bit exposes a Lua API through the `eightbit` global table, allowing server-side scripts to control voice effects and network relay on a per-player basis.

**Namespace**: `eightbit`

**Available In**: Server-side Lua only

## Functions

### Effect Configuration

#### eightbit.SetCrushFactor

Sets the quantization divisor for the bitcrush effect.

**Signature**:

```lua
eightbit.SetCrushFactor(number factor)
```

**Parameters**:

-   `factor` (number): Quantization divisor (default: 350)
    -   Range: 1-1000+ recommended
    -   Higher values = more distortion

**Returns**: None

**Example**:

```lua
-- Light distortion
eightbit.SetCrushFactor(500)

-- Default distortion
eightbit.SetCrushFactor(350)

-- Heavy distortion
eightbit.SetCrushFactor(150)

-- Extreme degradation
eightbit.SetCrushFactor(50)
```

**Notes**:

-   Global setting (affects all players using bitcrush)
-   Does not enable effect automatically
-   Changes take effect on next voice packet

---

#### eightbit.GetCrushFactor

Gets the current bitcrush quantization divisor.

**Signature**:

```lua
number eightbit.GetCrushFactor()
```

**Parameters**: None

**Returns**:

-   `number`: Current crush factor value

**Example**:

```lua
local current = eightbit.GetCrushFactor()
print("Current crush factor: " .. current)

-- Increase distortion by 10%
eightbit.SetCrushFactor(current * 0.9)
```

---

#### eightbit.SetGainFactor

Sets the volume multiplier applied after bitcrush.

**Signature**:

```lua
eightbit.SetGainFactor(number gain)
```

**Parameters**:

-   `gain` (number): Gain multiplier (default: 1.2)
    -   Range: 0.1-2.0 recommended
    -   1.0 = no change
    -   > 1.0 = louder
    -   <1.0 = quieter

**Returns**: None

**Example**:

```lua
-- Default compensation
eightbit.SetGainFactor(1.2)

-- Higher gain for heavy distortion
eightbit.SetCrushFactor(150)
eightbit.SetGainFactor(1.5)

-- Quiet effect
eightbit.SetGainFactor(0.8)
```

**Notes**:

-   Only affects bitcrush effect
-   Too high may cause clipping (distortion)
-   Compensates for volume loss from quantization

---

#### eightbit.SetDesampleRate

Sets the sample skip rate for the desample effect.

**Signature**:

```lua
eightbit.SetDesampleRate(number rate)
```

**Parameters**:

-   `rate` (number): Skip rate (default: 2)
    -   2 = Skip every other sample (~12kHz effective)
    -   3 = Skip every 3rd sample (~16kHz effective)
    -   4 = Skip every 4th sample (~18kHz effective)

**Returns**: None

**Example**:

```lua
-- Telephone quality
eightbit.SetDesampleRate(2)

-- Moderate effect
eightbit.SetDesampleRate(3)

-- Subtle effect
eightbit.SetDesampleRate(4)
```

**Notes**:

-   Global setting (affects all players using desample)
-   Higher values = less effect
-   Rate of 1 produces silence (invalid)

---

### Effect Control

#### eightbit.EnableEffect

Enables or changes an audio effect for a specific player.

**Signature**:

```lua
eightbit.EnableEffect(number userid, number effect)
```

**Parameters**:

-   `userid` (number): Player's UserID (from `Player:UserID()`)
-   `effect` (number): Effect type constant
    -   `eightbit.EFF_NONE` (0): Disable all effects
    -   `eightbit.EFF_BITCRUSH` (1): Enable bitcrush
    -   `eightbit.EFF_DESAMPLE` (2): Enable desample

**Returns**: None

**Example**:

```lua
local ply = player.GetByID(1)

-- Enable bitcrush
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)

-- Switch to desample
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_DESAMPLE)

-- Disable effects
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
```

**Behavior**:

-   Creates codec instance for player (if not exists)
-   Switches effect type (if already enabled)
-   Destroys codec instance (if effect = EFF_NONE)
-   Only one effect active per player at a time

**Important**:

-   Use `Player:UserID()`, not `Player:EntIndex()` or `Player:SteamID64()`
-   Effect persists until disabled or player disconnects
-   Must manually disable when player should no longer have effect

---

### Network Relay

#### eightbit.EnableBroadcast

Enables or disables UDP relay of voice packets.

**Signature**:

```lua
eightbit.EnableBroadcast(boolean enabled)
```

**Parameters**:

-   `enabled` (boolean): Enable (true) or disable (false)

**Returns**: None

**Example**:

```lua
-- Enable UDP relay
eightbit.EnableBroadcast(true)

-- Disable UDP relay
eightbit.EnableBroadcast(false)

-- Toggle
local current = ConVarExists("eightbit_broadcast") and GetConVar("eightbit_broadcast"):GetBool() or false
eightbit.EnableBroadcast(not current)
```

**Notes**:

-   Broadcasts all voice packets to configured IP:Port
-   Packet format identical to Steam voice packet
-   Applies to all players (cannot enable per-player)
-   No authentication or encryption

---

#### eightbit.SetBroadcastIP

Sets the destination IP address for UDP relay.

**Signature**:

```lua
eightbit.SetBroadcastIP(string ip)
```

**Parameters**:

-   `ip` (string): IPv4 address (default: "127.0.0.1")

**Returns**: None

**Example**:

```lua
-- Localhost (default)
eightbit.SetBroadcastIP("127.0.0.1")

-- Local network
eightbit.SetBroadcastIP("192.168.1.100")

-- Remote server (use with caution)
eightbit.SetBroadcastIP("203.0.113.5")
```

**Notes**:

-   IPv4 only (no IPv6 support)
-   No validation performed
-   Invalid IPs will cause send failures (silent)

---

#### eightbit.SetBroadcastPort

Sets the destination UDP port for relay.

**Signature**:

```lua
eightbit.SetBroadcastPort(number port)
```

**Parameters**:

-   `port` (number): UDP port (default: 4000)
    -   Range: 1-65535

**Returns**: None

**Example**:

```lua
-- Default
eightbit.SetBroadcastPort(4000)

-- Custom port
eightbit.SetBroadcastPort(8080)
```

**Notes**:

-   Must be valid port number (1-65535)
-   No validation performed
-   Firewall must allow outbound UDP

---

## Constants

### Effect Types

```lua
eightbit.EFF_NONE = 0        -- No effect (disable)
eightbit.EFF_BITCRUSH = 1    -- Bitcrusher effect
eightbit.EFF_DESAMPLE = 2    -- Desampler effect
```

**Usage**:

```lua
if effectType == eightbit.EFF_BITCRUSH then
    print("Bitcrush active")
elseif effectType == eightbit.EFF_DESAMPLE then
    print("Desample active")
else
    print("No effect")
end
```

---

## Usage Examples

### Basic Radio Effect

```lua
-- Server-side script
hook.Add("PlayerSpawn", "RadioEffect", function(ply)
    -- Enable radio effect for all players
    eightbit.SetCrushFactor(350)
    eightbit.SetGainFactor(1.2)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
end)

hook.Add("PlayerDisconnected", "RadioEffectCleanup", function(ply)
    -- Cleanup (optional, automatic on disconnect)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
end)
```

### Proximity-Based Degradation

```lua
local radioTowerPos = Vector(0, 0, 0)  -- Replace with actual position

hook.Add("Think", "ProximityVoice", function()
    for _, ply in ipairs(player.GetAll()) do
        local dist = ply:GetPos():Distance(radioTowerPos)

        if dist < 500 then
            -- Close: no effect
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
        elseif dist < 2000 then
            -- Medium: light distortion
            local factor = 500 - (dist - 500) / 3
            eightbit.SetCrushFactor(math.Clamp(factor, 200, 500))
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        else
            -- Far: heavy distortion
            eightbit.SetCrushFactor(150)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        end
    end
end)
```

### Equipment-Based Effects

```lua
-- Example: Radio item
hook.Add("PlayerSpawn", "RadioItem", function(ply)
    ply:SetNWBool("HasRadio", false)
    ply:SetNWInt("RadioQuality", 100)
end)

hook.Add("Think", "RadioQuality", function()
    for _, ply in ipairs(player.GetAll()) do
        if ply:GetNWBool("HasRadio") then
            local quality = ply:GetNWInt("RadioQuality", 100)

            if quality > 70 then
                -- Good quality
                eightbit.SetCrushFactor(500)
                eightbit.SetGainFactor(1.1)
            elseif quality > 30 then
                -- Medium quality
                eightbit.SetCrushFactor(300)
                eightbit.SetGainFactor(1.3)
            else
                -- Poor quality
                eightbit.SetCrushFactor(150)
                eightbit.SetGainFactor(1.5)
            end

            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        else
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
        end
    end
end)

-- Command to give radio
concommand.Add("give_radio", function(ply)
    ply:SetNWBool("HasRadio", true)
    ply:SetNWInt("RadioQuality", 100)
    ply:ChatPrint("Radio equipped!")
end)

-- Damage radio quality
hook.Add("EntityTakeDamage", "RadioDamage", function(target, dmg)
    if target:IsPlayer() and target:GetNWBool("HasRadio") then
        local quality = target:GetNWInt("RadioQuality", 100)
        quality = math.max(0, quality - dmg:GetDamage() / 2)
        target:SetNWInt("RadioQuality", quality)
    end
end)
```

### Voice Recording Setup

```lua
-- Enable UDP relay for external recording
hook.Add("Initialize", "VoiceRecording", function()
    eightbit.SetBroadcastIP("127.0.0.1")
    eightbit.SetBroadcastPort(4000)
    eightbit.EnableBroadcast(true)

    print("[gm_8bit] Voice relay enabled on localhost:4000")
end)

-- Console command to toggle recording
concommand.Add("sv_voicerecord", function(ply, cmd, args)
    if not IsValid(ply) or ply:IsAdmin() then
        local enabled = tobool(args[1])
        eightbit.EnableBroadcast(enabled)

        local msg = enabled and "Voice recording ENABLED" or "Voice recording DISABLED"
        if IsValid(ply) then
            ply:ChatPrint(msg)
        else
            print(msg)
        end
    end
end)
```

### Dynamic Effect Switching

```lua
-- Toggle between effects with key press
hook.Add("PlayerButtonDown", "VoiceEffectToggle", function(ply, button)
    if button == KEY_F6 then
        local currentEffect = ply:GetNWInt("VoiceEffect", 0)

        currentEffect = (currentEffect + 1) % 3
        ply:SetNWInt("VoiceEffect", currentEffect)

        if currentEffect == 0 then
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
            ply:ChatPrint("Voice effect: NONE")
        elseif currentEffect == 1 then
            eightbit.SetCrushFactor(350)
            eightbit.SetGainFactor(1.2)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
            ply:ChatPrint("Voice effect: BITCRUSH")
        else
            eightbit.SetDesampleRate(2)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_DESAMPLE)
            ply:ChatPrint("Voice effect: DESAMPLE")
        end
    end
end)
```

### Random Interference

```lua
-- Simulate random radio interference
local nextInterference = CurTime() + math.random(5, 15)

hook.Add("Think", "RadioInterference", function()
    if CurTime() < nextInterference then return end
    nextInterference = CurTime() + math.random(5, 15)

    -- Pick random player
    local players = player.GetAll()
    if #players == 0 then return end

    local ply = players[math.random(#players)]

    -- Apply heavy interference briefly
    local oldFactor = eightbit.GetCrushFactor()
    eightbit.SetCrushFactor(100)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)

    ply:ChatPrint("* Radio interference *")

    -- Restore after 2 seconds
    timer.Simple(2, function()
        if IsValid(ply) then
            eightbit.SetCrushFactor(oldFactor)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        end
    end)
end)
```

## Best Practices

### 1. Clean Up on Disconnect

```lua
hook.Add("PlayerDisconnected", "VoiceCleanup", function(ply)
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
    -- Frees codec resources
end)
```

**Why**: Prevents memory leaks from orphaned codec instances

### 2. Store Effect State

```lua
-- Bad: Global state
local crushFactor = 350

-- Good: Per-player state
ply:SetNWInt("CrushFactor", 350)
```

**Why**: Multiple players may need different settings

### 3. Validate UserID

```lua
-- Bad: Assume player is valid
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)

-- Good: Check validity
if IsValid(ply) and ply:IsPlayer() then
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
end
```

**Why**: Prevents errors with invalid players

### 4. Limit Think Hook Frequency

```lua
-- Bad: Every frame
hook.Add("Think", "VoiceEffects", function()
    for _, ply in ipairs(player.GetAll()) do
        -- Heavy computation
    end
end)

-- Good: Throttled
local nextUpdate = 0
hook.Add("Think", "VoiceEffects", function()
    if CurTime() < nextUpdate then return end
    nextUpdate = CurTime() + 0.5  -- Update every 0.5s

    for _, ply in ipairs(player.GetAll()) do
        -- Heavy computation
    end
end)
```

**Why**: Reduces CPU usage, effects don't need frame-perfect updates

### 5. Graceful Degradation

```lua
-- Check if module exists
if eightbit then
    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
else
    print("[Warning] gm_8bit module not loaded")
end
```

**Why**: Server can run without module (no voice effects)

## Common Mistakes

### Using Wrong Player ID

```lua
-- WRONG: Entity index
eightbit.EnableEffect(ply:EntIndex(), eightbit.EFF_BITCRUSH)

-- WRONG: SteamID64
eightbit.EnableEffect(ply:SteamID64(), eightbit.EFF_BITCRUSH)

-- WRONG: SteamID string
eightbit.EnableEffect(ply:SteamID(), eightbit.EFF_BITCRUSH)

-- CORRECT: UserID
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
```

### Not Disabling Effects

```lua
-- WRONG: Effect stays enabled forever
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)

-- CORRECT: Disable when no longer needed
timer.Simple(10, function()
    if IsValid(ply) then
        eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
    end
end)
```

### Forgetting to Configure Parameters

```lua
-- WRONG: Uses default parameters (may not be desired)
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)

-- CORRECT: Set parameters first
eightbit.SetCrushFactor(500)
eightbit.SetGainFactor(1.1)
eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
```

### Changing Parameters Too Frequently

```lua
-- WRONG: Update every tick
hook.Add("Think", "UpdateEffects", function()
    eightbit.SetCrushFactor(math.random(100, 500))  -- Very noisy!
end)

-- CORRECT: Smooth changes
local targetFactor = 350
local currentFactor = 350

hook.Add("Think", "UpdateEffects", function()
    currentFactor = currentFactor + (targetFactor - currentFactor) * 0.1
    eightbit.SetCrushFactor(math.floor(currentFactor))
end)
```

## Performance Considerations

### Effect Overhead

-   **BitCrush**: ~0.1-0.2ms per player speaking
-   **Desample**: ~0.05ms per player speaking
-   **No Effect**: <0.01ms per player speaking (minimal)

### Recommended Limits

-   **Max simultaneous effects**: 20-30 players
-   **Think hook frequency**: 0.1-1.0 seconds
-   **Parameter changes**: Avoid every frame

### Profiling

```lua
hook.Add("Think", "ProfileVoiceEffects", function()
    local start = SysTime()

    -- Your effect code here

    local duration = (SysTime() - start) * 1000
    if duration > 1.0 then
        print("[gm_8bit] Warning: Effect processing took " .. duration .. "ms")
    end
end)
```

## Troubleshooting

### Effect Not Working

**Check**:

1. Module loaded? (`gmsv_eightbit_*.dll/.so` in `garrysmod/lua/bin/`)
2. Using correct UserID? (`ply:UserID()`, not EntIndex)
3. Parameters set? (SetCrushFactor, SetGainFactor, etc.)
4. Effect enabled? (`EnableEffect` called)

### Voice Sounds Distorted

**Solutions**:

-   Reduce gain factor: `eightbit.SetGainFactor(1.0)`
-   Increase crush factor: `eightbit.SetCrushFactor(500)`
-   Check for multiple effects: Only one per player

### UDP Relay Not Working

**Check**:

1. Broadcast enabled? (`EnableBroadcast(true)`)
2. IP/port correct? (`SetBroadcastIP`, `SetBroadcastPort`)
3. Firewall allowing UDP?
4. Receiver listening on port?

### Performance Issues

**Solutions**:

-   Reduce Think hook frequency
-   Limit number of players with effects
-   Disable UDP relay if not needed
-   Profile with `SysTime()` measurements

## Advanced Topics

### Effect Automation

```lua
-- Automatically enable effect in certain areas
hook.Add("Think", "AreaEffects", function()
    for _, ply in ipairs(player.GetAll()) do
        local area = ply:GetArea()  -- Custom function

        if area == "radio_tower" then
            eightbit.SetCrushFactor(400)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_BITCRUSH)
        elseif area == "underwater" then
            eightbit.SetDesampleRate(3)
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_DESAMPLE)
        else
            eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
        end
    end
end)
```

### Effect Persistence

```lua
-- Save effect state to database
hook.Add("PlayerInitialSpawn", "LoadVoiceEffects", function(ply)
    local steamid = ply:SteamID()
    local data = LoadFromDatabase(steamid)  -- Custom function

    if data.voiceEffect then
        eightbit.SetCrushFactor(data.crushFactor or 350)
        eightbit.EnableEffect(ply:UserID(), data.voiceEffect)
    end
end)

hook.Add("PlayerDisconnected", "SaveVoiceEffects", function(ply)
    local steamid = ply:SteamID()
    local data = {
        voiceEffect = ply:GetNWInt("VoiceEffect", 0),
        crushFactor = eightbit.GetCrushFactor(),
    }
    SaveToDatabase(steamid, data)  -- Custom function

    eightbit.EnableEffect(ply:UserID(), eightbit.EFF_NONE)
end)
```

### Network Synchronization

```lua
-- Sync effect state to clients (for UI)
util.AddNetworkString("VoiceEffectUpdate")

function UpdateVoiceEffect(ply, effect)
    eightbit.EnableEffect(ply:UserID(), effect)

    net.Start("VoiceEffectUpdate")
    net.WriteUInt(effect, 2)  -- 0-2 (EFF_NONE/BITCRUSH/DESAMPLE)
    net.Send(ply)
end

-- Client-side
net.Receive("VoiceEffectUpdate", function()
    local effect = net.ReadUInt(2)
    -- Update UI to show current effect
end)
```
