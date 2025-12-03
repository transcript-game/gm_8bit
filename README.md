# gm_transcript native module

Tiny Garry's Mod binary module that mirrors in-engine voice to an external HTTPS endpoint while leaving vanilla voice chat untouched. Intended to pair with our private transcript backend (license required).

- Reverse engineered from the excellent groundwork by https://github.com/Meachamp/gm_8bit. Huge credit to that project for the signatures, hook approach, and packet format clues.
- Works on Windows (WinHTTP) and Linux (libcurl). Uses premake5 for project generation; source lives in `source/`.

## Building

1) Install premake5 and toolchains (MSVC on Windows, gcc/clang on Linux).  
2) From this folder, run `premake5 vs2022` (Windows) or `premake5 gmake2` (Linux) to generate projects.  
3) Build the produced solution/Makefile; the module will be named `gm_transcript`.  
4) Deploy the compiled binary to your Garry's Mod server's `garrysmod/lua/bin` (and the `.dll/.so` name must match the binary output). Contact us for license keys and backend access.

## Credits

- Original reverse engineering and baseline implementation: [Meachamp/gm_8bit](https://github.com/Meachamp/gm_8bit).  
- This fork adapts the transport, naming (`gm_transcript`), and server expectations to match the transcript-game stack.

## License (LGPL-2.1)

This module is released under the GNU Lesser General Public License v2.1 (see `LICENSE` in this folder). Key points to keep in mind if you publish binaries or source:

- You must keep the LGPL text with any distribution and note any changes you make.
- If you ship a binary, you must also provide the corresponding source (or a written offer) for this library and any modifications.
- Applications may link to this library without being forced under the LGPL, but recipients must be able to relink with a modified version of this library (e.g., by providing object files or dynamic linking).
- Preserve attribution to the upstream `gm_8bit` project by Meachamp.
