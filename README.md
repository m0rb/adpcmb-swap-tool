# adpcmb-swap-tool

Patches Neo Geo M1 (Z80 sound driver) ROMs to fix the ADPCM-B stereo channel
swap on the MV1C motherboard.

## The Problem

The Neo Geo MV1C board has the YM2610 ADPCM-B left and right output pins
swapped compared to earlier boards (MV1, MV1F, etc.). Games that use ADPCM-B
for mono-panned sound effects write pan values with L/R bits that are correct
for the original hardware but produce reversed stereo on the MV1C. 

YM2610 register `0x11` controls ADPCM-B panning:
- Bit 7: Left channel enable
- Bit 6: Right channel enable

On the MV1C, these are physically reversed.

## The Fix

This tool finds the YM2610 "pair-A write" subroutine in the Z80 sound driver
ROM and injects a small patch that swaps bits 6 and 7 of the data byte
whenever register `0x11` is being written. The patcher:

1. **Scans** the ROM using Z80 disassembly to locate the YM2610 write routine
   (handles both `OUT (n),A` and `OUT (C),r` I/O styles)
2. **Finds free space** (0xFF or 0x00 fill regions) in the ROM
3. **Injects** a 20-byte preamble + relocated copy of the original routine
4. **Redirects** the original entry point with a `JP` to the patch

The injected Z80 code checks if register `0x11` is being written, and if so,
XORs the data byte with `0xC0` to swap the pan bits -- but only when exactly
one of L/R is set (no-op when both or neither are set, preserving stereo and
muted states).

All hardware testing was performed on a OpenMVS modded MV1C via flashcart.

## Building

Requires CMake 3.10+ and a C++17 compiler.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

```
adpcmb-swap patch <rom> [-o output] [--no-backup] [-v]
adpcmb-swap analyze <rom> [-v]
adpcmb-swap disasm <rom> [start [end]]
```

### Commands

- **patch** -- Find the YM write routine, inject the pan-swap patch, save the
  patched ROM. Creates a `.bak` backup by default.
- **analyze** -- Find the YM write routine and report free space without
  modifying the ROM.
- **disasm** -- Disassemble an M1 ROM (full or address range).

Supports both raw M1 ROM binaries and MAME ZIP romsets.

### Examples

```bash
# Analyze a ROM to see if it can be patched
adpcmb-swap analyze 001-m1.m1

# Patch a ROM (creates 001-m1.m1.bak backup)
adpcmb-swap patch 001-m1.m1

# Patch with explicit output path
adpcmb-swap patch 007-m1.m1 -o 007-m1-patched.m1

# Verbose output showing detection details
adpcmb-swap patch 241-m1.m1 -v
```

## How It Works

The tool performs Z80 static analysis to locate the YM2610 write subroutine:

1. Disassembles the first 64KB of the M1 ROM into an instruction stream
2. Collects candidate entry points from RST vectors and CALL targets
3. For each candidate, checks for the YM write pattern: load register address,
   `OUT` to port `0x04`, load data value, `OUT` to port `0x05`, `RET`
   (also handles the `OUT (C),r` / `INC C` / `OUT (C),r` variant)
4. Filters by routine body size (8-24 bytes) and validates that the address
   register differs from the data register

Patched areas of code are placed in the largest contiguous free-space region. 
The original routine entry is replaced with `JP patch_addr`. 
Any `JP` instructions within the relocated routine body that reference 
the original address range are fixed up to point into the copy, 
thus preventing re-entry (important for routines with busy-wait loops).

## Attribution and References

### Third-Party Libraries

- **[miniz](https://github.com/richgel999/miniz)** -- Public domain / MIT
  deflate/inflate and ZIP library by Rich Geldreich, RAD Game Tools, and
  Valve Software. Used for ZIP romset support.

### Technical Resources & More

- [Neo Geo Dev Wiki](https://wiki.neogeodev.org/) -- Neo Geo hardware
  documentation, memory maps, and I/O register descriptions
- [Neo Geo Dev Wiki: YM2610](https://wiki.neogeodev.org/index.php?title=YM2610) --
  YM2610 register map including ADPCM-B channel control
- [Sean Young, "The Undocumented Z80 Documented"](http://www.z80.info/zip/z80-documented.pdf) --
  Comprehensive Z80 instruction set reference
- [MAME](https://github.com/mamedev/mame) -- Reference implementation of
  YM2610 emulation and Neo Geo hardware; used for testing
- [Backbit Platinum Cartridge for MVS] (https://www.backbit.io/) --
| An amazing dev-friendly flashcart used for testing.

## License

MIT License. See [LICENSE](LICENSE) for details.