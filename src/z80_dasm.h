// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#ifndef Z80_DASM_H
#define Z80_DASM_H

#include <cstdint>
#include <cstddef>

// Instruction classification flags
enum Z80InsnType : uint8_t {
    Z80_OTHER       = 0,
    Z80_OUT_N_A     = 1,  // OUT (n), A  — opcode D3 nn
    Z80_IN_A_N      = 2,  // IN A, (n)   — opcode DB nn
    Z80_LD_A_IMM    = 3,  // LD A, n     — opcode 3E nn
    Z80_LD_B_IMM    = 4,  // LD B, n     — opcode 06 nn
    Z80_LD_C_IMM    = 5,  // LD C, n     — opcode 0E nn
    Z80_LD_D_IMM    = 6,  // LD D, n     — opcode 16 nn
    Z80_LD_E_IMM    = 7,  // LD E, n     — opcode 1E nn
    Z80_LD_H_IMM    = 8,  // LD H, n     — opcode 26 nn
    Z80_LD_L_IMM    = 9,  // LD L, n     — opcode 2E nn
    Z80_LD_A_REG    = 10, // LD A, r     — opcodes 78-7F (except 7E=LD A,(HL))
    Z80_LD_A_MEM    = 11, // LD A, (HL)/(IX+d)/(IY+d)/(nn)
    Z80_CALL        = 12, // CALL nn     — opcode CD nn nn
    Z80_RET         = 13, // RET         — opcode C9
    Z80_JP          = 14, // JP nn       — opcode C3 nn nn
    Z80_JR          = 15, // JR n        — opcode 18 nn
    Z80_CALL_CC     = 16, // CALL cc, nn — conditional calls
    Z80_RET_CC      = 17, // RET cc      — conditional returns
    Z80_JP_CC       = 18, // JP cc, nn   — conditional jumps
    Z80_JR_CC       = 19, // JR cc, n    — conditional relative jumps
    Z80_LD_HL_NN    = 20, // LD HL, nn   — opcode 21 nn nn
    Z80_RST         = 21, // RST nn      — opcodes C7/CF/D7/DF/E7/EF/F7/FF
    Z80_LD_R_R      = 22, // LD r, r     — general register-to-register (40-7F block)
    Z80_LD_MEM_A    = 23, // LD (nn), A  — opcode 32 nn nn
    Z80_LD_DE_NN    = 24, // LD DE, nn   — opcode 11 nn nn
    Z80_OUT_C_R     = 25, // OUT (C), r  — ED 41/49/51/59/61/69/79
    Z80_IN_R_C      = 26, // IN r, (C)   — ED 40/48/50/58/60/68/78
    Z80_INC_R       = 27, // INC r       — 04/0C/14/1C/24/2C/3C
    Z80_DEC_R       = 28, // DEC r       — 05/0D/15/1D/25/2D/3D
};

struct Z80Insn {
    uint16_t addr;        // Address in ROM
    uint8_t  length;      // Instruction length in bytes
    uint8_t  type;        // Z80InsnType
    uint8_t  imm8;        // Immediate byte (for LD r,n / OUT (n),A etc.)
    uint16_t imm16;       // Immediate word (for CALL/JP/LD rr,nn)
    uint8_t  src_reg;     // Source register index for LD A,r / LD r,r (B=0..A=7)
    uint8_t  dst_reg;     // Destination register index for LD r,r
};

// Decode a single Z80 instruction at rom[offset].
// Returns instruction length (0 if offset is out of bounds).
int z80_decode(const uint8_t *rom, size_t rom_size, size_t offset, Z80Insn *out);

// Format a decoded instruction as human-readable text.
// Returns number of characters written (excluding null terminator).
int z80_format(const Z80Insn *insn, const uint8_t *rom, char *buf, size_t buf_size);

#endif // Z80_DASM_H
