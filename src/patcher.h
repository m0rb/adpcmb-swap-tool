// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#ifndef PATCHER_H
#define PATCHER_H

#include "scanner.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

struct PatchInfo {
    size_t  routine_addr;       // Original subroutine address
    size_t  patch_addr;         // Address where patch code was placed
    size_t  patch_size;         // Size of injected patch code
    size_t  jp_addr;            // Address where JP was written (routine entry)
    bool    success;
    std::string description;
};

// Generate and apply the subroutine-level patch.
// Replaces the YM write routine entry with a JP to patch code that
// swaps ADPCM-B pan bits (register 0x11, bits 6-7) before writing.
PatchInfo apply_subroutine_patch(uint8_t *rom, size_t rom_size,
                                  const YMWriteRoutine &routine,
                                  const FreeRegion &free_space,
                                  bool verbose);

// Swap bits 7 and 6, preserve bits 5-0.
inline uint8_t swap_pan(uint8_t val) {
    uint8_t b7 = (val >> 7) & 1;
    uint8_t b6 = (val >> 6) & 1;
    return (uint8_t)((b6 << 7) | (b7 << 6) | (val & 0x3F));
}

#endif // PATCHER_H
