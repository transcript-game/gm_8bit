# gm_8bit Documentation

Complete technical documentation for the gm_8bit Garry's Mod voice manipulation module.

## Table of Contents

1. [Overview](overview.md) - Project introduction and high-level architecture
2. [Architecture](architecture.md) - Detailed component architecture and data flow
3. [Steam Voice Protocol](steam-voice-protocol.md) - Steam voice packet format and handling
4. [Opus Integration](opus-integration.md) - Opus codec configuration and usage
5. [Audio Effects](audio-effects.md) - Effect algorithms and implementation
6. [Lua API Reference](lua-api.md) - Complete Lua API documentation
7. [Build System](build-system.md) - Building and deploying the module
8. [Network Protocol](network-protocol.md) - UDP relay system
9. [State Management](state-management.md) - Internal state handling
10. [Rust Migration Guide](rust-migration.md) - Guide for recreating in Rust

## Quick Start

For developers looking to understand the codebase:

1. Start with [Overview](overview.md) for the big picture
2. Read [Architecture](architecture.md) to understand component interactions
3. Review [Steam Voice Protocol](steam-voice-protocol.md) for packet handling
4. Check [Lua API Reference](lua-api.md) for usage examples

## For Rust Recreation

If you're recreating this project in Rust:

1. Read all architecture documents first
2. Pay special attention to [Opus Integration](opus-integration.md) and [Steam Voice Protocol](steam-voice-protocol.md)
3. Follow the [Rust Migration Guide](rust-migration.md) for specific implementation patterns
4. Refer to [Audio Effects](audio-effects.md) for algorithm implementations

## Contributing

When modifying the module:

-   Update relevant documentation files
-   Test with packet loss simulation
-   Verify backward compatibility with existing Lua scripts
-   Check memory safety (buffer overflows)

## License

LGPL 2.1 - See [LICENSE](../LICENSE)
