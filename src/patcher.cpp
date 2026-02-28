// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#include "patcher.h"
#include <cstdio>
#include <cstring>

// Z80 register encoding
static const int REG_B = 0, REG_C = 1, REG_D = 2, REG_E = 3;
static const int REG_H = 4, REG_L = 5, REG_A = 7;

// Z80 opcodes
static const uint8_t OP_JP_NN     = 0xC3;  // JP nn
static const uint8_t OP_LD_A_B    = 0x78;
static const uint8_t OP_LD_A_C    = 0x79;
static const uint8_t OP_LD_A_D    = 0x7A;
static const uint8_t OP_LD_A_E    = 0x7B;
static const uint8_t OP_LD_A_H    = 0x7C;
static const uint8_t OP_LD_A_L    = 0x7D;
static const uint8_t OP_LD_A_A    = 0x7F;
static const uint8_t OP_CP_N      = 0xFE;
static const uint8_t OP_JR_NZ     = 0x20;
static const uint8_t OP_JR_Z      = 0x28;
static const uint8_t OP_AND_N     = 0xE6;
static const uint8_t OP_XOR_N     = 0xEE;

// LD A, r opcode for a given register index
static uint8_t ld_a_reg(int reg) {
    switch (reg) {
        case REG_B: return OP_LD_A_B;
        case REG_C: return OP_LD_A_C;
        case REG_D: return OP_LD_A_D;
        case REG_E: return OP_LD_A_E;
        case REG_H: return OP_LD_A_H;
        case REG_L: return OP_LD_A_L;
        case REG_A: return OP_LD_A_A;
        default: return OP_LD_A_A;
    }
}

// LD r, A opcode for a given register index
static uint8_t ld_reg_a(int reg) {
    // LD r, A = 01xxx111 where xxx = register code
    // B=01000111=0x47, C=0x4F, D=0x57, E=0x5F, H=0x67, L=0x6F, A=0x7F
    switch (reg) {
        case REG_B: return 0x47;
        case REG_C: return 0x4F;
        case REG_D: return 0x57;
        case REG_E: return 0x5F;
        case REG_H: return 0x67;
        case REG_L: return 0x6F;
        case REG_A: return 0x7F;
        default: return 0x7F;
    }
}

PatchInfo apply_subroutine_patch(uint8_t *rom, size_t /*rom_size*/,
                                  const YMWriteRoutine &routine,
                                  const FreeRegion &free_space,
                                  bool verbose) {
    PatchInfo info;
    info.routine_addr = routine.entry_addr;
    info.success = false;

    size_t routine_size = routine.body_end - routine.body_start;

    // Patch code layout:
    //
    // patch_entry:
    //   PUSH AF              ; 1 byte  — save caller's A and flags
    //   LD A, <addr_reg>     ; 1 byte  — load YM register address
    //   CP 0x11              ; 2 bytes — is it register 0x11?
    //   JR NZ, done          ; 2 bytes — no, skip swap
    //   LD A, <data_reg>     ; 1 byte  — load data value
    //   AND 0xC0             ; 2 bytes — isolate pan bits
    //   JR Z, done           ; 2 bytes — both zero, no swap needed
    //   CP 0xC0              ; 2 bytes — both set?
    //   JR Z, done           ; 2 bytes — both set, no swap needed
    //   LD A, <data_reg>     ; 1 byte  — reload full data value
    //   XOR 0xC0             ; 2 bytes — flip bits 6 and 7
    //   LD <data_reg>, A     ; 1 byte  — store back
    // done:
    //   POP AF               ; 1 byte  — restore caller's A and flags
    // original:
    //   <original routine bytes>  — copied from entry_addr
    //
    // PUSH/POP AF is critical: many routines (001, 007, 2501, BB01) begin
    // with PUSH AF / end with POP AF to preserve the caller's A and flags.
    // Without this wrapper, the preamble's CP/AND/XOR trash A and flags
    // before the routine's own PUSH AF, corrupting the value that gets
    // restored on return.
    //
    // Total patch preamble: 20 bytes
    // Total patch: 20 + routine_size bytes

    size_t preamble_size = 20;

    // Calculate JR offsets for the preamble
    // JR NZ at byte 4 (counting from 0): needs to jump to POP AF at byte 19
    //   offset = 19 - (4 + 2) = 13
    // JR Z at byte 9: offset = 19 - (9 + 2) = 8
    // JR Z at byte 13: offset = 19 - (13 + 2) = 4

    size_t total_patch_size = preamble_size + routine_size;

    if (free_space.length < total_patch_size) {
        info.description = "Not enough free space for patch code";
        fprintf(stderr, "Error: need %zu bytes of free space, only %zu available\n",
                total_patch_size, free_space.length);
        return info;
    }

    // Place patch at the start of free space
    size_t patch_addr = free_space.start;
    info.patch_addr = patch_addr;
    info.patch_size = total_patch_size;

    if (verbose) {
        fprintf(stderr, "[patcher] Placing %zu-byte patch at 0x%04zX\n",
                total_patch_size, patch_addr);
        fprintf(stderr, "[patcher] Routine at 0x%04zX (%zu bytes), addr_reg=%d, data_reg=%d\n",
                routine.entry_addr, routine_size, routine.addr_reg, routine.data_reg);
    }

    // Build the patch preamble with explicit byte positions
    uint8_t preamble[20];

    preamble[0]  = 0xF5;                             // PUSH AF
    preamble[1]  = ld_a_reg(routine.addr_reg);       // LD A, <addr_reg>
    preamble[2]  = OP_CP_N;                          // CP 0x11
    preamble[3]  = 0x11;
    preamble[4]  = OP_JR_NZ;                         // JR NZ, done (POP AF)
    preamble[5]  = 13;                               // offset: 19 - 6 = 13
    preamble[6]  = ld_a_reg(routine.data_reg);       // LD A, <data_reg>
    preamble[7]  = OP_AND_N;                          // AND 0xC0
    preamble[8]  = 0xC0;
    preamble[9]  = OP_JR_Z;                           // JR Z, done (POP AF)
    preamble[10] = 8;                                 // offset: 19 - 11 = 8
    preamble[11] = OP_CP_N;                           // CP 0xC0
    preamble[12] = 0xC0;
    preamble[13] = OP_JR_Z;                           // JR Z, done (POP AF)
    preamble[14] = 4;                                 // offset: 19 - 15 = 4
    preamble[15] = ld_a_reg(routine.data_reg);        // LD A, <data_reg>
    preamble[16] = OP_XOR_N;                          // XOR 0xC0
    preamble[17] = 0xC0;
    preamble[18] = ld_reg_a(routine.data_reg);        // LD <data_reg>, A
    preamble[19] = 0xF1;                              // POP AF

    // Write preamble to patch area
    memcpy(rom + patch_addr, preamble, preamble_size);

    // Copy original routine body to patch area after preamble
    memcpy(rom + patch_addr + preamble_size,
           rom + routine.body_start, routine_size);

    // Relocate any JP/JP cc instructions in the copied body that jump back into
    // the original routine. This is critical for routines with busy-wait loops
    // (e.g., JP M, entry_addr) — without relocation, re-entering through the
    // patched entry would re-run the preamble and toggle pan bits repeatedly.
    {
        size_t copy_start = patch_addr + preamble_size;
        size_t orig_start = routine.body_start;
        size_t orig_end   = routine.body_end;

        for (size_t i = 0; i < routine_size; ) {
            uint8_t op = rom[copy_start + i];

            // JP nn (C3) and JP cc,nn (C2/CA/D2/DA/E2/EA/F2/FA) are 3-byte:
            // opcode, low addr, high addr
            bool is_jp = (op == 0xC3) ||
                         (op == 0xC2 || op == 0xCA || op == 0xD2 || op == 0xDA ||
                          op == 0xE2 || op == 0xEA || op == 0xF2 || op == 0xFA);

            if (is_jp && i + 2 < routine_size) {
                uint16_t target = rom[copy_start + i + 1] |
                                  (rom[copy_start + i + 2] << 8);
                if (target >= orig_start && target < orig_end) {
                    // Relocate: target within original routine → same offset in copy
                    uint16_t new_target = (uint16_t)(copy_start + (target - orig_start));
                    rom[copy_start + i + 1] = (uint8_t)(new_target & 0xFF);
                    rom[copy_start + i + 2] = (uint8_t)((new_target >> 8) & 0xFF);
                    if (verbose) {
                        fprintf(stderr, "[patcher] Relocated JP at copy+0x%02zX: 0x%04X → 0x%04X\n",
                                i, target, new_target);
                    }
                }
                i += 3;
            } else {
                // Skip instruction — use base_lengths table knowledge or just advance 1
                // For safety, advance 1 byte at a time (we only care about JP patterns)
                i++;
            }
        }
    }

    // Replace original routine entry with JP patch_addr
    // JP nn = C3 lo hi
    info.jp_addr = routine.entry_addr;
    rom[routine.entry_addr] = OP_JP_NN;
    rom[routine.entry_addr + 1] = (uint8_t)(patch_addr & 0xFF);
    rom[routine.entry_addr + 2] = (uint8_t)((patch_addr >> 8) & 0xFF);

    // Fill remaining bytes of original routine with NOPs (0x00) for cleanliness
    // (only the bytes after the JP that are within the original routine)
    for (size_t i = routine.entry_addr + 3; i < routine.body_end; i++) {
        rom[i] = 0x00;
    }

    info.success = true;
    char desc[256];
    snprintf(desc, sizeof(desc),
             "Patched routine at 0x%04zX → JP 0x%04zX (%zu bytes)",
             routine.entry_addr, patch_addr, total_patch_size);
    info.description = desc;

    return info;
}
