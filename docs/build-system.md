# Build System

## Overview

gm_8bit uses **Premake5** as its build system generator. Premake generates platform-specific project files (Visual Studio solutions on Windows, Makefiles on Linux) from a single Lua configuration script.

Note: The current streaming-only build no longer links against Opus. Any Opus setup steps in this document are legacy and can be skipped unless you plan to reintroduce effect processing.

## Prerequisites

### All Platforms

1. **garrysmod_common**

    - GitHub: https://github.com/danielga/garrysmod_common
    - Provides SDK headers and build utilities
    - Required for all Garry's Mod binary modules

2. **Premake5**

    - Download: https://premake.github.io/download.html
    - Version: 5.0.0-alpha16 or later
    - Place in PATH or project directory

3. **Opus Library**
    - Already included in `opus/` directory
    - Pre-compiled for Windows (x86/x64)
    - Linux: Use system package or provided libs

### Windows

**Compiler**: Visual Studio 2019 or 2022

-   **Workloads**: "Desktop development with C++"
-   **Components**:
    -   MSVC v142/v143 compiler
    -   Windows SDK 10.0

**Download**: https://visualstudio.microsoft.com/downloads/

### Linux

**Compiler**: GCC 9+ or Clang 10+

**Ubuntu/Debian**:

```bash
sudo apt-get update
sudo apt-get install build-essential g++ libopus-dev
```

**RHEL/CentOS**:

```bash
sudo yum groupinstall "Development Tools"
sudo yum install gcc-c++ opus-devel
```

**Arch**:

```bash
sudo pacman -S base-devel opus
```

## Directory Structure

```
gm_8bit/
├── premake5.lua          # Build configuration
├── source/               # Source code
│   ├── main.cpp
│   ├── *.h
│   └── ...
├── opus/                 # Opus codec
│   ├── include/
│   │   ├── opus.h
│   │   └── ...
│   ├── lib32/           # Windows x86 libraries
│   │   └── opus.lib
│   └── lib64/           # Windows x64 libraries
│       └── opus.lib
└── [garrysmod_common]/  # External dependency
    └── ...
```

## Configuration

### premake5.lua

```lua
newoption({
    trigger = "gmcommon",
    description = "Sets the path to garrysmod_common directory",
    value = "path to garrysmod_common directory"
})

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
    "you didn't provide a path to your garrysmod_common directory")
include(gmcommon .. "/generator.v3.lua")

CreateWorkspace({name = "eightbit"})
    CreateProject({serverside = true})
        -- SDK includes
        IncludeSDKCommon()
        IncludeSDKTier0()
        IncludeSDKTier1()
        IncludeDetouring()
        IncludeScanning()
        IncludeLuaShared()
        IncludeHelpersExtended()

        -- Opus linking
        links("opus")
        includedirs("opus/include")

        filter({"platforms:x86_64"})
            libdirs {"opus/lib64"}

        filter({"platforms:x86"})
            libdirs {"opus/lib32"}

        filter("system:windows")
            links("ws2_32")  -- WinSock2
```

### Key Components

**Workspace**: `eightbit`

-   Container for all projects
-   Defines global settings

**Project**: Server-side binary module

-   Target: `gmsv_eightbit_*.dll` (Windows) or `gmsv_eightbit_*.so` (Linux)
-   Type: Shared library

**SDK Includes**:

-   `IncludeSDKCommon()`: Source SDK common headers
-   `IncludeSDKTier0()`: Tier 0 library (platform abstraction)
-   `IncludeSDKTier1()`: Tier 1 library (interfaces)
-   `IncludeDetouring()`: Function hooking
-   `IncludeScanning()`: Symbol finding
-   `IncludeLuaShared()`: Lua C API
-   `IncludeHelpersExtended()`: garrysmod_common helpers

**Platform Filters**:

-   `x86_64`: 64-bit builds
-   `x86`: 32-bit builds
-   `windows`: Windows-specific settings
-   `linux`: Linux-specific settings (implicit)

## Building

### Windows

**Step 1: Set garrysmod_common Path**

Option A - Environment Variable:

```powershell
# PowerShell
$env:GARRYSMOD_COMMON = "C:\path\to\garrysmod_common"

# CMD
set GARRYSMOD_COMMON=C:\path\to\garrysmod_common
```

Option B - Command Line Flag:

```powershell
# Use --gmcommon flag (see Step 2)
```

**Step 2: Generate Project Files**

```powershell
# Using environment variable
.\premake5.exe vs2022

# Using command line flag
.\premake5.exe --gmcommon=C:\path\to\garrysmod_common vs2022

# For Visual Studio 2019
.\premake5.exe --gmcommon=C:\path\to\garrysmod_common vs2019
```

**Step 3: Build**

Option A - Visual Studio GUI:

1. Open `eightbit.sln`
2. Select configuration: `Release` or `Debug`
3. Select platform: `x86` or `x86_64`
4. Build → Build Solution (Ctrl+Shift+B)

Option B - Command Line:

```powershell
# Release x86
msbuild eightbit.sln /p:Configuration=Release /p:Platform=x86

# Release x64
msbuild eightbit.sln /p:Configuration=Release /p:Platform=x64

# Debug x86
msbuild eightbit.sln /p:Configuration=Debug /p:Platform=x86
```

**Output Location**:

```
projects/windows/vs2022/x86/Release/gmsv_eightbit_win32.dll
projects/windows/vs2022/x86_64/Release/gmsv_eightbit_win64.dll
```

### Linux

**Step 1: Set garrysmod_common Path**

```bash
# Add to ~/.bashrc or ~/.zshrc for persistence
export GARRYSMOD_COMMON="/path/to/garrysmod_common"

# Or set temporarily
GARRYSMOD_COMMON="/path/to/garrysmod_common"
```

**Step 2: Generate Makefiles**

```bash
# Using environment variable
./premake5 gmake2

# Using command line flag
./premake5 --gmcommon=/path/to/garrysmod_common gmake2
```

**Step 3: Build**

```bash
# Enter build directory
cd projects/linux/gmake2

# Build release x86_64
make config=release_x86_64

# Build release x86 (if 32-bit libs available)
make config=release_x86

# Build debug x86_64
make config=debug_x86_64

# Clean build
make clean
```

**Output Location**:

```
projects/linux/gmake2/x86_64/Release/gmsv_eightbit_linux64.so
projects/linux/gmake2/x86/Release/gmsv_eightbit_linux.so
```

## Build Configurations

### Release

**Optimization**: Maximum (`-O3` / `/O2`)
**Debug Symbols**: Minimal or none
**Assertions**: Disabled
**Use For**: Production servers

**Characteristics**:

-   Fastest performance
-   Smallest binary size
-   Difficult to debug crashes

### Debug

**Optimization**: None (`-O0` / `/Od`)
**Debug Symbols**: Full
**Assertions**: Enabled
**Use For**: Development, debugging

**Characteristics**:

-   Slower performance
-   Larger binary size
-   Easy to debug with debugger

## Platform-Specific Details

### Windows

**Output Names**:

-   32-bit: `gmsv_eightbit_win32.dll`
-   64-bit: `gmsv_eightbit_win64.dll`

**Dependencies**:

-   `opus.lib`: Statically linked
-   `ws2_32.lib`: Windows Sockets (system library)

**Calling Convention**:

-   x86: `__cdecl` or `__thiscall`
-   x64: Microsoft x64 calling convention

**Runtime Library**: Multithreaded DLL (`/MD` or `/MDd`)

### Linux

**Output Names**:

-   32-bit: `gmsv_eightbit_linux.so`
-   64-bit: `gmsv_eightbit_linux64.so`

**Dependencies**:

-   `libopus.so`: Dynamically linked (system)
-   `libc.so`: C standard library
-   `libpthread.so`: POSIX threads
-   `libdl.so`: Dynamic loading

**Symbol Visibility**: Hidden by default (garrysmod_common sets this)

**RPATH**: Not set (relies on `LD_LIBRARY_PATH` or system paths)

## Troubleshooting

### "garrysmod_common not found"

**Error**:

```
you didn't provide a path to your garrysmod_common directory
```

**Solution**:

```bash
# Set environment variable
export GARRYSMOD_COMMON="/path/to/garrysmod_common"

# Or use flag
./premake5 --gmcommon=/path/to/garrysmod_common gmake2
```

### "opus.h not found"

**Windows**: Ensure `opus/include/opus.h` exists

**Linux**:

```bash
# Install system package
sudo apt-get install libopus-dev

# Or verify include path in premake5.lua
```

### "Cannot open file 'opus.lib'"

**Windows**:

-   Verify `opus/lib32/opus.lib` (x86) or `opus/lib64/opus.lib` (x64) exists
-   Check platform matches (x86 vs x86_64)

**Linux**:

```bash
# Install opus library
sudo apt-get install libopus0

# Check library path
ldconfig -p | grep opus
```

### "Undefined reference to opus_encode"

**Windows**: Ensure `opus.lib` is in correct lib directory

**Linux**:

```bash
# Link explicitly
links {"opus"}

# Or install development package
sudo apt-get install libopus-dev
```

### Build Fails with "C2039: Unknown identifier"

**Cause**: Missing SDK include

**Solution**: Verify garrysmod_common is up to date:

```bash
cd /path/to/garrysmod_common
git pull
```

### Module Fails to Load in Garry's Mod

**Check**:

1. **Correct architecture**: Server is 32-bit (use x86 build)? 64-bit (use x86_64)?
2. **File name**: Must be `gmsv_eightbit_*.dll/.so`
3. **Location**: `garrysmod/lua/bin/`
4. **Dependencies**: Opus library available? (Linux)

**Test**:

```bash
# Linux: Check dependencies
ldd gmsv_eightbit_linux64.so

# Should show all libraries found
# libopus.so.0 => /usr/lib/x86_64-linux-gnu/libopus.so.0
```

## Advanced Build Options

### Custom Opus Library

**Static Linking** (Windows):

```lua
-- premake5.lua
filter({"system:windows"})
    -- Use custom opus build
    includedirs {"path/to/custom/opus/include"}
    libdirs {"path/to/custom/opus/lib"}
```

**System Library** (Linux):

```lua
-- Already uses system library by default
links("opus")
```

### Third-Party Integration

**Enable zsvoicechat linking**:

```lua
-- premake5.lua
filter({"system:linux"})
    defines {"THIRDPARTY_LINK"}
```

**Effect**: Enables `checkIfMuted()` function integration

### Debug Symbols in Release

```lua
-- premake5.lua
filter({"configurations:Release"})
    symbols "On"  -- Include debug symbols
    optimize "On" -- Keep optimizations
```

**Use Case**: Profiling, crash analysis

### Link-Time Optimization (LTO)

```lua
-- premake5.lua
filter({"configurations:Release"})
    flags {"LinkTimeOptimization"}
```

**Benefits**: Smaller binary, better optimization
**Drawback**: Slower build times

## Deployment

### Installation

1. **Build the module** (Release configuration)
2. **Locate output file**:
    - Windows: `projects/windows/vs2022/[arch]/Release/gmsv_eightbit_*.dll`
    - Linux: `projects/linux/gmake2/[arch]/Release/gmsv_eightbit_*.so`
3. **Copy to server**:
    ```
    [server]/garrysmod/lua/bin/gmsv_eightbit_win32.dll      (Windows 32-bit)
    [server]/garrysmod/lua/bin/gmsv_eightbit_win64.dll      (Windows 64-bit)
    [server]/garrysmod/lua/bin/gmsv_eightbit_linux.so       (Linux 32-bit)
    [server]/garrysmod/lua/bin/gmsv_eightbit_linux64.so     (Linux 64-bit)
    ```
4. **Restart server**

### Verification

**Console Output**:

```
[gm_8bit] Module loaded successfully
```

**Lua Test**:

```lua
-- In server console or autorun
if eightbit then
    print("gm_8bit loaded: " .. type(eightbit))
    print("Functions: SetCrushFactor, EnableEffect, etc.")
else
    print("ERROR: gm_8bit not loaded")
end
```

### Binary Compatibility

**Important**: Match server architecture

-   32-bit server → `gmsv_eightbit_win32.dll` or `gmsv_eightbit_linux.so`
-   64-bit server → `gmsv_eightbit_win64.dll` or `gmsv_eightbit_linux64.so`

**Check Server Architecture**:

```lua
-- In Lua console
print(jit.arch)  -- "x86" or "x64"
```

### Distribution

**For Public Release**:

1. Build both architectures (x86, x86_64)
2. Build both platforms (Windows, Linux)
3. Package structure:
    ```
    gm_8bit-v1.0.zip
    ├── README.md
    ├── LICENSE
    └── lua/
        └── bin/
            ├── gmsv_eightbit_win32.dll
            ├── gmsv_eightbit_win64.dll
            ├── gmsv_eightbit_linux.so
            └── gmsv_eightbit_linux64.so
    ```
4. Include installation instructions

## Continuous Integration

### GitHub Actions Example

```yaml
name: Build

on: [push, pull_request]

jobs:
    build-windows:
        runs-on: windows-latest
        steps:
            - uses: actions/checkout@v2
            - name: Checkout garrysmod_common
              uses: actions/checkout@v2
              with:
                  repository: danielga/garrysmod_common
                  path: garrysmod_common
            - name: Generate project
              run: |
                  .\premake5.exe --gmcommon=garrysmod_common vs2022
            - name: Build x86
              run: msbuild eightbit.sln /p:Configuration=Release /p:Platform=x86
            - name: Build x64
              run: msbuild eightbit.sln /p:Configuration=Release /p:Platform=x64
            - name: Upload artifacts
              uses: actions/upload-artifact@v2
              with:
                  name: gm_8bit-windows
                  path: projects/windows/vs2022/*/Release/*.dll

    build-linux:
        runs-on: ubuntu-latest
        steps:
            - uses: actions/checkout@v2
            - name: Install dependencies
              run: |
                  sudo apt-get update
                  sudo apt-get install -y build-essential libopus-dev
            - name: Checkout garrysmod_common
              uses: actions/checkout@v2
              with:
                  repository: danielga/garrysmod_common
                  path: garrysmod_common
            - name: Generate makefiles
              run: |
                  chmod +x premake5
                  ./premake5 --gmcommon=garrysmod_common gmake2
            - name: Build x86_64
              run: |
                  cd projects/linux/gmake2
                  make config=release_x86_64
            - name: Upload artifacts
              uses: actions/upload-artifact@v2
              with:
                  name: gm_8bit-linux
                  path: projects/linux/gmake2/*/Release/*.so
```

## Development Workflow

### Recommended Setup

1. **IDE**: Visual Studio (Windows) or VSCode with C++ extensions (Linux)
2. **Build**: Use Release for testing, Debug for development
3. **Test Server**: Local Garry's Mod server for quick iteration
4. **Debugging**: Attach debugger to `srcds.exe`/`srcds_linux`

### Rapid Iteration

**Windows**:

```powershell
# Build and deploy script
msbuild eightbit.sln /p:Configuration=Release /p:Platform=x86_64
Copy-Item "projects\windows\vs2022\x86_64\Release\*.dll" "C:\GameServers\gmod\garrysmod\lua\bin\"
```

**Linux**:

```bash
# Build and deploy script
cd projects/linux/gmake2
make config=release_x86_64
cp x86_64/Release/*.so /opt/gmod/garrysmod/lua/bin/
```

### Version Bumping

**Add version to code**:

```cpp
// source/main.cpp
#define EIGHTBIT_VERSION "1.2.0"

GMOD_MODULE_OPEN() {
    LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
    LUA->GetField(-1, "eightbit");
    LUA->PushString(EIGHTBIT_VERSION);
    LUA->SetField(-2, "VERSION");
    LUA->Pop(2);

    // ... rest of initialization
}
```

**Lua access**:

```lua
print("gm_8bit version: " .. eightbit.VERSION)
```
