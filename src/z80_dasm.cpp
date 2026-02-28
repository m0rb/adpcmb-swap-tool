// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#include "z80_dasm.h"
#include <cstdio>
#include <cstring>

// Base opcode instruction lengths (no prefix).
// Index is the opcode byte. 0 means "should not occur" / use special handling.
static const uint8_t base_lengths[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    1, 3, 1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, // 0x
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 1x
    2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // 2x
    2, 3, 3, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // 3x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 4x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 5x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 6x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 7x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 8x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 9x
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Ax
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // Bx
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 0, 3, 3, 2, 1, // Cx (CB at 0xCB = special)
    1, 1, 3, 2, 3, 1, 2, 1, 1, 1, 3, 2, 3, 0, 2, 1, // Dx (DD at 0xDD = special)
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 0, 2, 1, // Ex (ED at 0xED = special)
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 0, 2, 1, // Fx (FD at 0xFD = special)
};

// ED-prefixed instruction lengths. Most are 2 bytes total.
// A few (like LD (nn),rr and LD rr,(nn)) are 4 bytes.
static uint8_t ed_length(uint8_t op2) {
    // 4-byte ED instructions: ED 43/4B/53/5B/63/6B/73/7B nn nn
    if ((op2 & 0xC7) == 0x43) return 4;
    // Everything else in the ED range is 2 bytes
    return 2;
}

// DD/FD-prefixed instruction lengths.
// DD/FD prefix replaces HL with IX/IY. Instructions referencing (HL) become
// (IX+d)/(IY+d) with an extra displacement byte.
static uint8_t ddfd_length(const uint8_t *rom, size_t rom_size, size_t offset) {
    if (offset + 1 >= rom_size) return 1;
    uint8_t op2 = rom[offset + 1];

    // DD CB d xx / FD CB d xx — always 4 bytes
    if (op2 == 0xCB) return 4;

    // DD DD / DD FD / FD DD / FD FD — prefix is ignored, just consume 1 byte
    if (op2 == 0xDD || op2 == 0xFD) return 1;

    // DD ED / FD ED — ED takes over, prefix ignored
    if (op2 == 0xED) return 1;

    // For instructions that use (HL) in the base set, the DD/FD version
    // adds a displacement byte, making them 1 byte longer.
    // (HL) is encoded as reg field = 6 in the opcode.
    uint8_t base_len = base_lengths[op2];

    // Instructions where source or dest is (HL) → becomes (IX+d), add 1 byte
    // These are: opcodes where bits 2-0 == 6 or bits 5-3 == 6 (in the 40-BF range)
    // Plus: 34 INC(HL), 35 DEC(HL), 36 LD(HL),n, and various others
    bool uses_hl_indirect = false;

    if (op2 >= 0x40 && op2 <= 0x7F) {
        // LD r,r block: source (bits 2-0) == 6 or dest (bits 5-3) == 6
        // But 76 is HALT, not LD (HL),(HL)
        if (op2 != 0x76) {
            uses_hl_indirect = ((op2 & 0x07) == 0x06) || ((op2 & 0x38) == 0x30);
        }
    } else if (op2 >= 0x80 && op2 <= 0xBF) {
        // ALU A,r block: source (bits 2-0) == 6
        uses_hl_indirect = ((op2 & 0x07) == 0x06);
    } else if (op2 == 0x34 || op2 == 0x35 || op2 == 0x36) {
        // INC (HL), DEC (HL), LD (HL),n
        uses_hl_indirect = true;
    } else if (op2 == 0x46 || op2 == 0x4E || op2 == 0x56 || op2 == 0x5E ||
               op2 == 0x66 || op2 == 0x6E || op2 == 0x7E) {
        // Already covered by the 40-7F block above, but just to be safe
        uses_hl_indirect = true;
    } else if (op2 == 0x86 || op2 == 0x8E || op2 == 0x96 || op2 == 0x9E ||
               op2 == 0xA6 || op2 == 0xAE || op2 == 0xB6 || op2 == 0xBE) {
        // Already covered by the 80-BF block above
        uses_hl_indirect = true;
    } else if (op2 == 0xE1 || op2 == 0xE3 || op2 == 0xE5 || op2 == 0xE9 ||
               op2 == 0xF9) {
        // POP IX, EX (SP),IX, PUSH IX, JP (IX), LD SP,IX
        // These use IX/IY as a whole, no displacement byte
        uses_hl_indirect = false;
    } else if (op2 == 0x21 || op2 == 0x22 || op2 == 0x2A) {
        // LD IX,nn / LD (nn),IX / LD IX,(nn)
        // Same length as base (3) + 1 prefix = use base_len + 1 for prefix
        uses_hl_indirect = false;
    } else if (op2 == 0x23 || op2 == 0x2B || op2 == 0x29 ||
               op2 == 0x09 || op2 == 0x19 || op2 == 0x39) {
        // INC IX, DEC IX, ADD IX,rr — no displacement
        uses_hl_indirect = false;
    }

    // Total length = 1 (prefix) + base_len + (1 if displacement added)
    if (base_len == 0) return 2; // unknown, treat as 2
    return 1 + base_len + (uses_hl_indirect ? 1 : 0);
}

int z80_decode(const uint8_t *rom, size_t rom_size, size_t offset, Z80Insn *out) {
    if (offset >= rom_size) return 0;

    memset(out, 0, sizeof(*out));
    out->addr = (uint16_t)offset;

    uint8_t op = rom[offset];

    // Handle prefixes
    if (op == 0xCB) {
        // CB prefix: all CB xx are 2 bytes
        out->length = 2;
        out->type = Z80_OTHER;
        return 2;
    }

    if (op == 0xED) {
        if (offset + 1 >= rom_size) { out->length = 1; return 1; }
        uint8_t op2 = rom[offset + 1];
        out->length = ed_length(op2);

        // ED 41/49/51/59/61/69/79 = OUT (C),r
        if ((op2 & 0xC7) == 0x41) {
            out->type = Z80_OUT_C_R;
            out->src_reg = (op2 >> 3) & 0x07;
            return out->length;
        }
        // ED 40/48/50/58/60/68/78 = IN r,(C)
        if ((op2 & 0xC7) == 0x40) {
            out->type = Z80_IN_R_C;
            out->dst_reg = (op2 >> 3) & 0x07;
            return out->length;
        }
        out->type = Z80_OTHER;
        return out->length;
    }

    if (op == 0xDD || op == 0xFD) {
        out->length = ddfd_length(rom, rom_size, offset);
        out->type = Z80_OTHER;

        // Check for LD A, (IX+d) / LD A, (IY+d) — DD 7E d / FD 7E d
        if (offset + 2 < rom_size && rom[offset + 1] == 0x7E) {
            out->type = Z80_LD_A_MEM;
            out->length = 3;
        }
        return out->length;
    }

    // Base (unprefixed) opcodes
    out->length = base_lengths[op];
    if (out->length == 0) out->length = 1; // safety fallback

    switch (op) {
        // OUT (n), A
        case 0xD3:
            out->type = Z80_OUT_N_A;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // IN A, (n)
        case 0xDB:
            out->type = Z80_IN_A_N;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD A, n
        case 0x3E:
            out->type = Z80_LD_A_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD B, n
        case 0x06:
            out->type = Z80_LD_B_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD C, n
        case 0x0E:
            out->type = Z80_LD_C_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD D, n
        case 0x16:
            out->type = Z80_LD_D_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD E, n
        case 0x1E:
            out->type = Z80_LD_E_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD H, n
        case 0x26:
            out->type = Z80_LD_H_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD L, n
        case 0x2E:
            out->type = Z80_LD_L_IMM;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD A, r  (78=LD A,B  79=LD A,C  7A=LD A,D  7B=LD A,E  7C=LD A,H  7D=LD A,L  7F=LD A,A)
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7F:
            out->type = Z80_LD_A_REG;
            out->src_reg = op & 0x07; // B=0,C=1,D=2,E=3,H=4,L=5,A=7
            out->dst_reg = 7; // A
            break;

        // LD A, (HL)
        case 0x7E:
            out->type = Z80_LD_A_MEM;
            break;

        // LD A, (BC)
        case 0x0A:
            out->type = Z80_LD_A_MEM;
            break;

        // LD A, (DE)
        case 0x1A:
            out->type = Z80_LD_A_MEM;
            break;

        // LD A, (nn)
        case 0x3A:
            out->type = Z80_LD_A_MEM;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // LD (nn), A
        case 0x32:
            out->type = Z80_LD_MEM_A;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // LD DE, nn
        case 0x11:
            out->type = Z80_LD_DE_NN;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // INC r (single register)
        case 0x04: out->type = Z80_INC_R; out->dst_reg = 0; break; // INC B
        case 0x0C: out->type = Z80_INC_R; out->dst_reg = 1; break; // INC C
        case 0x14: out->type = Z80_INC_R; out->dst_reg = 2; break; // INC D
        case 0x1C: out->type = Z80_INC_R; out->dst_reg = 3; break; // INC E
        case 0x24: out->type = Z80_INC_R; out->dst_reg = 4; break; // INC H
        case 0x2C: out->type = Z80_INC_R; out->dst_reg = 5; break; // INC L
        case 0x3C: out->type = Z80_INC_R; out->dst_reg = 7; break; // INC A

        // DEC r (single register)
        case 0x05: out->type = Z80_DEC_R; out->dst_reg = 0; break; // DEC B
        case 0x0D: out->type = Z80_DEC_R; out->dst_reg = 1; break; // DEC C
        case 0x15: out->type = Z80_DEC_R; out->dst_reg = 2; break; // DEC D
        case 0x1D: out->type = Z80_DEC_R; out->dst_reg = 3; break; // DEC E
        case 0x25: out->type = Z80_DEC_R; out->dst_reg = 4; break; // DEC H
        case 0x2D: out->type = Z80_DEC_R; out->dst_reg = 5; break; // DEC L
        case 0x3D: out->type = Z80_DEC_R; out->dst_reg = 7; break; // DEC A

        // RST instructions — call to fixed addresses
        case 0xC7: out->type = Z80_RST; out->imm8 = 0x00; break;
        case 0xCF: out->type = Z80_RST; out->imm8 = 0x08; break;
        case 0xD7: out->type = Z80_RST; out->imm8 = 0x10; break;
        case 0xDF: out->type = Z80_RST; out->imm8 = 0x18; break;
        case 0xE7: out->type = Z80_RST; out->imm8 = 0x20; break;
        case 0xEF: out->type = Z80_RST; out->imm8 = 0x28; break;
        case 0xF7: out->type = Z80_RST; out->imm8 = 0x30; break;
        case 0xFF: out->type = Z80_RST; out->imm8 = 0x38; break;

        // CALL nn
        case 0xCD:
            out->type = Z80_CALL;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // CALL cc, nn
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC:
            out->type = Z80_CALL_CC;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // RET
        case 0xC9:
            out->type = Z80_RET;
            break;

        // RET cc
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            out->type = Z80_RET_CC;
            break;

        // JP nn
        case 0xC3:
            out->type = Z80_JP;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // JP cc, nn
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA:
            out->type = Z80_JP_CC;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        // JR n
        case 0x18:
            out->type = Z80_JR;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // JR cc, n
        case 0x20: case 0x28: case 0x30: case 0x38:
            out->type = Z80_JR_CC;
            if (offset + 1 < rom_size) out->imm8 = rom[offset + 1];
            break;

        // LD HL, nn
        case 0x21:
            out->type = Z80_LD_HL_NN;
            if (offset + 2 < rom_size)
                out->imm16 = rom[offset + 1] | (rom[offset + 2] << 8);
            break;

        default:
            // General LD r,r block: 0x40-0x7F (except 0x76=HALT and already-handled 0x78-0x7F, 0x7E)
            if (op >= 0x40 && op <= 0x7F && op != 0x76) {
                uint8_t dst = (op >> 3) & 0x07;
                uint8_t src = op & 0x07;
                if (src == 6 || dst == 6) {
                    // (HL) involved — memory access, not reg-to-reg
                    out->type = Z80_OTHER;
                } else {
                    out->type = Z80_LD_R_R;
                    out->dst_reg = dst;
                    out->src_reg = src;
                }
            } else {
                out->type = Z80_OTHER;
            }
            break;
    }

    return out->length;
}

static const char *reg_names[] = { "B", "C", "D", "E", "H", "L", "(HL)", "A" };

int z80_format(const Z80Insn *insn, const uint8_t *rom, char *buf, size_t buf_size) {
    // Print address and raw bytes first
    int pos = 0;
    pos += snprintf(buf + pos, buf_size - pos, "%04X  ", insn->addr);

    for (int i = 0; i < insn->length && i < 4; i++) {
        pos += snprintf(buf + pos, buf_size - pos, "%02X ", rom[insn->addr + i]);
    }
    // Pad to fixed column
    while (pos < 18 && pos < (int)buf_size - 1) buf[pos++] = ' ';

    switch (insn->type) {
        case Z80_OUT_N_A:
            pos += snprintf(buf + pos, buf_size - pos, "OUT (0x%02X), A", insn->imm8);
            break;
        case Z80_IN_A_N:
            pos += snprintf(buf + pos, buf_size - pos, "IN A, (0x%02X)", insn->imm8);
            break;
        case Z80_LD_A_IMM:
            pos += snprintf(buf + pos, buf_size - pos, "LD A, 0x%02X", insn->imm8);
            break;
        case Z80_LD_B_IMM: case Z80_LD_C_IMM: case Z80_LD_D_IMM:
        case Z80_LD_E_IMM: case Z80_LD_H_IMM: case Z80_LD_L_IMM: {
            int reg_idx = (rom[insn->addr] >> 3) & 0x07;
            pos += snprintf(buf + pos, buf_size - pos, "LD %s, 0x%02X",
                           reg_names[reg_idx], insn->imm8);
            break;
        }
        case Z80_LD_A_REG:
            pos += snprintf(buf + pos, buf_size - pos, "LD A, %s",
                           reg_names[insn->src_reg]);
            break;
        case Z80_LD_A_MEM:
            if (rom[insn->addr] == 0x3A)
                pos += snprintf(buf + pos, buf_size - pos, "LD A, (0x%04X)", insn->imm16);
            else if (rom[insn->addr] == 0x7E)
                pos += snprintf(buf + pos, buf_size - pos, "LD A, (HL)");
            else if (rom[insn->addr] == 0x0A)
                pos += snprintf(buf + pos, buf_size - pos, "LD A, (BC)");
            else if (rom[insn->addr] == 0x1A)
                pos += snprintf(buf + pos, buf_size - pos, "LD A, (DE)");
            else
                pos += snprintf(buf + pos, buf_size - pos, "LD A, (IX/IY+d)");
            break;
        case Z80_CALL:
            pos += snprintf(buf + pos, buf_size - pos, "CALL 0x%04X", insn->imm16);
            break;
        case Z80_CALL_CC:
            pos += snprintf(buf + pos, buf_size - pos, "CALL cc, 0x%04X", insn->imm16);
            break;
        case Z80_RET:
            pos += snprintf(buf + pos, buf_size - pos, "RET");
            break;
        case Z80_RET_CC:
            pos += snprintf(buf + pos, buf_size - pos, "RET cc");
            break;
        case Z80_JP:
            pos += snprintf(buf + pos, buf_size - pos, "JP 0x%04X", insn->imm16);
            break;
        case Z80_JP_CC:
            pos += snprintf(buf + pos, buf_size - pos, "JP cc, 0x%04X", insn->imm16);
            break;
        case Z80_JR:
            pos += snprintf(buf + pos, buf_size - pos, "JR 0x%04X",
                           (uint16_t)(insn->addr + 2 + (int8_t)insn->imm8));
            break;
        case Z80_JR_CC:
            pos += snprintf(buf + pos, buf_size - pos, "JR cc, 0x%04X",
                           (uint16_t)(insn->addr + 2 + (int8_t)insn->imm8));
            break;
        case Z80_LD_HL_NN:
            pos += snprintf(buf + pos, buf_size - pos, "LD HL, 0x%04X", insn->imm16);
            break;
        case Z80_LD_DE_NN:
            pos += snprintf(buf + pos, buf_size - pos, "LD DE, 0x%04X", insn->imm16);
            break;
        case Z80_RST:
            pos += snprintf(buf + pos, buf_size - pos, "RST 0x%02X", insn->imm8);
            break;
        case Z80_LD_R_R:
            pos += snprintf(buf + pos, buf_size - pos, "LD %s, %s",
                           reg_names[insn->dst_reg], reg_names[insn->src_reg]);
            break;
        case Z80_LD_MEM_A:
            pos += snprintf(buf + pos, buf_size - pos, "LD (0x%04X), A", insn->imm16);
            break;
        case Z80_OUT_C_R:
            pos += snprintf(buf + pos, buf_size - pos, "OUT (C), %s", reg_names[insn->src_reg]);
            break;
        case Z80_IN_R_C:
            pos += snprintf(buf + pos, buf_size - pos, "IN %s, (C)", reg_names[insn->dst_reg]);
            break;
        case Z80_INC_R:
            pos += snprintf(buf + pos, buf_size - pos, "INC %s", reg_names[insn->dst_reg]);
            break;
        case Z80_DEC_R:
            pos += snprintf(buf + pos, buf_size - pos, "DEC %s", reg_names[insn->dst_reg]);
            break;
        default: {
            // Generic disassembly: just show the opcode byte mnemonic
            uint8_t op = rom[insn->addr];
            if (op == 0x00)
                pos += snprintf(buf + pos, buf_size - pos, "NOP");
            else if (op == 0x76)
                pos += snprintf(buf + pos, buf_size - pos, "HALT");
            else if (op == 0xF3)
                pos += snprintf(buf + pos, buf_size - pos, "DI");
            else if (op == 0xFB)
                pos += snprintf(buf + pos, buf_size - pos, "EI");
            else
                pos += snprintf(buf + pos, buf_size - pos, "db 0x%02X ...", op);
            break;
        }
    }

    if (pos < (int)buf_size) buf[pos] = '\0';
    return pos;
}
