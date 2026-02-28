// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#ifndef SCANNER_H
#define SCANNER_H

#include <cstdint>
#include <cstddef>
#include <vector>

// Describes a YM2610 pair-A write subroutine found in the ROM
struct YMWriteRoutine {
    size_t  entry_addr;     // Subroutine entry point in ROM
    size_t  body_start;     // Start of the actual routine body (may differ if entry has PUSH etc.)
    size_t  body_end;       // End of routine (address after final RET)
    int     addr_reg;       // Register holding YM register address (D=2, A=7, etc.)
    int     data_reg;       // Register holding data value (E=3, C=1, etc.)
    size_t  out04_addr;     // Address of OUT (0x04), A instruction
    size_t  out05_addr;     // Address of OUT (0x05), A instruction
    bool    is_rst;         // Whether this is called via RST (vs CALL)
    uint8_t rst_vec;        // RST vector number if is_rst (0x00, 0x08, etc.)
};

// Describes a region of free space in the ROM
struct FreeRegion {
    size_t  start;
    size_t  length;
    uint8_t fill_byte;      // 0x00 or 0xFF
};

// Scan an M1 ROM to find the pair-A YM2610 write subroutine.
// Returns all candidate routines found. Typically there's exactly one
// for pair A (ports 0x04/0x05) and one for pair B (ports 0x06/0x07).
std::vector<YMWriteRoutine> find_ym_write_routines(const uint8_t *rom, size_t rom_size, bool verbose);

// Find the largest region of free space (0x00 or 0xFF fill) in the ROM.
// Searches within the first 64KB (Z80 address space) or entire ROM.
FreeRegion find_free_space(const uint8_t *rom, size_t rom_size, size_t min_size);

#endif // SCANNER_H
