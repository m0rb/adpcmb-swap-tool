// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#include "scanner.h"
#include "z80_dasm.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <set>

// Register indices matching Z80 encoding
static const int REG_D = 2, REG_E = 3, REG_A = 7;

// Disassemble ROM into instruction list
static std::vector<Z80Insn> disassemble_all(const uint8_t *rom, size_t rom_size) {
    std::vector<Z80Insn> insns;
    insns.reserve(rom_size / 2);
    size_t offset = 0;
    while (offset < rom_size) {
        Z80Insn insn;
        int len = z80_decode(rom, rom_size, offset, &insn);
        if (len <= 0) { len = 1; insn.length = 1; insn.type = Z80_OTHER; }
        insns.push_back(insn);
        offset += len;
    }
    return insns;
}

// Build offset-to-index map
static std::vector<int> build_offset_map(const std::vector<Z80Insn> &insns, size_t rom_size) {
    std::vector<int> map(rom_size, -1);
    for (size_t i = 0; i < insns.size(); i++) {
        if (insns[i].addr < rom_size)
            map[insns[i].addr] = (int)i;
    }
    return map;
}

// Analyze a candidate subroutine starting at `addr` for YM write pattern.
// Handles two I/O styles:
//   Style 1: OUT (n), A  — fixed port, value in A (loaded from another register)
//   Style 2: OUT (C), r  — port in C, register directly output
static bool analyze_routine(const std::vector<Z80Insn> &insns,
                            const std::vector<int> &offset_map,
                            size_t addr, size_t rom_size,
                            YMWriteRoutine &result) {
    if (addr >= rom_size) return false;
    int start_idx = offset_map[addr];
    if (start_idx < 0) return false;

    // Collect all OUT instructions within the routine (until RET, max 32 insns)
    struct OutInfo { int idx; int style; int port_or_reg; };
    std::vector<OutInfo> outs;
    int end_idx = start_idx;

    for (int i = start_idx; i < (int)insns.size() && (i - start_idx) < 32; i++) {
        const auto &insn = insns[i];

        // Style 1: OUT (n), A — port number is immediate
        if (insn.type == Z80_OUT_N_A) {
            outs.push_back({i, 1, insn.imm8});
        }
        // Style 2: OUT (C), r — port in C, output register is src_reg
        else if (insn.type == Z80_OUT_C_R) {
            outs.push_back({i, 2, insn.src_reg});
        }

        if (insn.type == Z80_RET) {
            end_idx = i;
            break;
        }
    }

    // Need at least 2 OUT instructions (one for address, one for data)
    if (outs.size() < 2) return false;

    // --- Style 1: Look for OUT (0x04), A followed by OUT (0x05), A ---
    for (size_t a = 0; a < outs.size(); a++) {
        if (outs[a].style != 1 || outs[a].port_or_reg != 0x04) continue;
        for (size_t b = a + 1; b < outs.size(); b++) {
            if (outs[b].style != 1 || outs[b].port_or_reg != 0x05) continue;

            // Found a pair. Determine addr_reg and data_reg.
            result.entry_addr = addr;
            result.body_start = addr;
            result.body_end = insns[end_idx].addr + insns[end_idx].length;
            result.out04_addr = insns[outs[a].idx].addr;
            result.out05_addr = insns[outs[b].idx].addr;

            // Trace what was loaded into A before each OUT
            result.addr_reg = REG_A;
            for (int i = outs[a].idx - 1; i >= start_idx; i--) {
                if (insns[i].type == Z80_LD_A_REG || insns[i].type == Z80_LD_R_R) {
                    if (insns[i].type == Z80_LD_A_REG || insns[i].dst_reg == REG_A) {
                        result.addr_reg = insns[i].src_reg;
                        break;
                    }
                } else if (insns[i].type == Z80_LD_A_IMM) {
                    result.addr_reg = REG_A;
                    break;
                }
            }

            result.data_reg = REG_A;
            for (int i = outs[b].idx - 1; i > outs[a].idx; i--) {
                if (insns[i].type == Z80_LD_A_REG || insns[i].type == Z80_LD_R_R) {
                    if (insns[i].type == Z80_LD_A_REG || insns[i].dst_reg == REG_A) {
                        result.data_reg = insns[i].src_reg;
                        break;
                    }
                } else if (insns[i].type == Z80_LD_A_IMM) {
                    result.data_reg = REG_A;
                    break;
                }
            }

            return true;
        }
    }

    // --- Style 2: Look for two OUT (C),r instructions ---
    // The first outputs the register address, the second outputs the data.
    // Between them there's typically an INC C (to move from port 0x04 to 0x05).
    for (size_t a = 0; a < outs.size(); a++) {
        if (outs[a].style != 2) continue;
        for (size_t b = a + 1; b < outs.size(); b++) {
            if (outs[b].style != 2) continue;

            // Verify there's an INC C between the two OUT (C),r instructions
            bool has_inc_c = false;
            for (int i = outs[a].idx + 1; i < outs[b].idx; i++) {
                if (insns[i].type == Z80_INC_R && insns[i].dst_reg == 1) { // INC C
                    has_inc_c = true;
                    break;
                }
            }
            if (!has_inc_c) continue;

            result.entry_addr = addr;
            result.body_start = addr;
            result.body_end = insns[end_idx].addr + insns[end_idx].length;
            result.out04_addr = insns[outs[a].idx].addr;
            result.out05_addr = insns[outs[b].idx].addr;

            // For OUT (C),r, the registers being output ARE the addr/data regs directly
            result.addr_reg = outs[a].port_or_reg;  // register output first = address
            result.data_reg = outs[b].port_or_reg;  // register output second = data

            return true;
        }
    }

    return false;
}

std::vector<YMWriteRoutine> find_ym_write_routines(const uint8_t *rom, size_t rom_size, bool verbose) {
    std::vector<YMWriteRoutine> routines;

    // Only analyze the first 64KB (Z80 address space)
    size_t scan_size = std::min(rom_size, (size_t)0x10000);

    if (verbose) fprintf(stderr, "[scanner] Disassembling ROM (%zu bytes, scanning first %zu)...\n",
                         rom_size, scan_size);

    auto insns = disassemble_all(rom, scan_size);
    auto offset_map = build_offset_map(insns, scan_size);

    if (verbose) fprintf(stderr, "[scanner] Decoded %zu instructions\n", insns.size());

    // Collect candidate entry points: RST vectors + CALL targets
    std::set<size_t> candidates;

    // RST vectors
    for (uint16_t rst = 0; rst <= 0x38; rst += 8) {
        if (rst < scan_size) candidates.insert(rst);
    }

    // CALL targets
    for (const auto &insn : insns) {
        if ((insn.type == Z80_CALL || insn.type == Z80_CALL_CC) && insn.imm16 < scan_size)
            candidates.insert(insn.imm16);
    }

    if (verbose) fprintf(stderr, "[scanner] Checking %zu candidate entry points\n", candidates.size());

    const char *reg_names_arr[] = {"B","C","D","E","H","L","(HL)","A"};

    // Check each candidate for pair-A (ports 0x04/0x05) YM write pattern
    for (size_t addr : candidates) {
        // Skip RST 0x00 — that's the reset vector, not a subroutine
        if (addr == 0) continue;

        YMWriteRoutine routine;
        if (!analyze_routine(insns, offset_map, addr, scan_size, routine))
            continue;

        // Filter: addr_reg must differ from data_reg for a generic write routine
        if (routine.addr_reg == routine.data_reg) {
            if (verbose) {
                fprintf(stderr, "[scanner] Skipping 0x%04zX: addr_reg == data_reg (%s)\n",
                        addr, reg_names_arr[routine.addr_reg]);
            }
            continue;
        }

        // Filter: routine body should be reasonable size
        // Too small (< 8 bytes) = false positive (need at minimum: LD,OUT,LD,OUT,RET = 7 bytes)
        // Too large (> 24 bytes) = complex function that happens to contain OUT, not a write helper
        size_t body_size = routine.body_end - routine.body_start;
        if (body_size < 8) {
            if (verbose) {
                fprintf(stderr, "[scanner] Skipping 0x%04zX: body too small (%zu bytes)\n",
                        addr, body_size);
            }
            continue;
        }
        if (body_size > 24) {
            if (verbose) {
                fprintf(stderr, "[scanner] Skipping 0x%04zX: body too large (%zu bytes)\n",
                        addr, body_size);
            }
            continue;
        }

        // Determine if this is an RST vector
        routine.is_rst = false;
        routine.rst_vec = 0;
        if (addr >= 0x08 && addr <= 0x38 && (addr % 8) == 0) {
            routine.is_rst = true;
            routine.rst_vec = (uint8_t)addr;
        }

        routines.push_back(routine);

        if (verbose) {
            fprintf(stderr, "[scanner] Found pair-A YM write routine at 0x%04zX "
                    "(addr_reg=%s, data_reg=%s, body=0x%04zX-0x%04zX, %zu bytes, %s)\n",
                    addr, reg_names_arr[routine.addr_reg], reg_names_arr[routine.data_reg],
                    routine.body_start, routine.body_end, body_size,
                    routine.is_rst ? "RST" : "CALL");
        }
    }

    // Sort: prefer RST-based routines (more likely to be the primary one),
    // then by number of call sites (most called = most generic)
    // For now, simple: RST first, then by address
    std::sort(routines.begin(), routines.end(), [](const YMWriteRoutine &a, const YMWriteRoutine &b) {
        if (a.is_rst != b.is_rst) return a.is_rst; // RST first
        return a.entry_addr < b.entry_addr;
    });

    return routines;
}

FreeRegion find_free_space(const uint8_t *rom, size_t rom_size, size_t min_size) {
    // Search within Z80 addressable space (first 64KB)
    size_t search_size = std::min(rom_size, (size_t)0x10000);
    // Don't search in RAM area (0xF800-0xFFFF maps to RAM on Neo Geo)
    size_t search_end = std::min(search_size, (size_t)0xF800);

    FreeRegion best = {0, 0, 0};

    for (uint8_t fill : {0xFF, 0x00}) {
        size_t run_start = 0;
        size_t run_len = 0;

        for (size_t i = 0; i < search_end; i++) {
            if (rom[i] == fill) {
                if (run_len == 0) run_start = i;
                run_len++;
            } else {
                if (run_len > best.length && run_len >= min_size) {
                    best.start = run_start;
                    best.length = run_len;
                    best.fill_byte = fill;
                }
                run_len = 0;
            }
        }
        if (run_len > best.length && run_len >= min_size) {
            best.start = run_start;
            best.length = run_len;
            best.fill_byte = fill;
        }
    }

    return best;
}
