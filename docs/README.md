# gm_8bit Documentation

Complete technical documentation for the gm_8bit Garry's Mod voice relay module. The current codebase forwards Steam voice packets without in-engine effects or Opus processing.

## Table of Contents

1. [Overview](overview.md) - Project introduction and high-level architecture
2. [Architecture](architecture.md) - Detailed component architecture and data flow
3. [Steam Voice Protocol](steam-voice-protocol.md) - Steam voice packet format and handling
4. [Lua API Reference](lua-api.md) - Streaming-only Lua API
5. [Build System](build-system.md) - Building and deploying the module
6. [Network Protocol](network-protocol.md) - UDP relay system
7. [Rust Migration Guide](rust-migration.md) - Guide for recreating in Rust

## Quick Start

For developers looking to understand the codebase:

1. Start with [Overview](overview.md) for the big picture
2. Read [Architecture](architecture.md) to understand component interactions
3. Review [Steam Voice Protocol](steam-voice-protocol.md) for packet handling
4. Check [Lua API Reference](lua-api.md) for usage examples

## For Rust Recreation

If you're recreating this project in Rust:

1. Read all architecture documents first
2. Pay special attention to [Steam Voice Protocol](steam-voice-protocol.md) for packet details
3. Follow the [Rust Migration Guide](rust-migration.md) for specific implementation patterns

## Contributing

When modifying the module:

-   Update relevant documentation files
-   Test with packet loss simulation
-   Verify backward compatibility with existing Lua scripts
-   Check memory safety (buffer overflows)

## License

LGPL 2.1 - See [LICENSE](../LICENSE)
