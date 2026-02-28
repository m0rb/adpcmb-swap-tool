// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#include "romio.h"
#include "scanner.h"
#include "patcher.h"
#include "z80_dasm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "adpcmb-swap-tool v" APP_VERSION " — Neo Geo MV1C ADPCM-B stereo channel fix\n"
        "\n"
        "Patches the YM2610 pair-A write subroutine in M1 ROMs to swap\n"
        "ADPCM-B register 0x11 pan bits (L/R), fixing the MV1C hardware bug.\n"
        "\n"
        "Usage:\n"
        "  %s patch <rom> [-o output] [--no-backup] [-v]\n"
        "  %s analyze <rom> [-v]\n"
        "  %s disasm <rom> [start [end]]\n"
        "\n"
        "Commands:\n"
        "  patch    Find YM write routine, inject pan-swap code, save patched ROM\n"
        "  analyze  Find YM write routine and free space, report without modifying\n"
        "  disasm   Disassemble M1 ROM (or a range of addresses)\n"
        "\n"
        "Options:\n"
        "  -o FILE  Output path (default: overwrite input, with .bak backup)\n"
        "  -v       Verbose output (show detection details)\n"
        "  --no-backup  Skip creating .bak backup file\n"
        "\n"
        "Supports raw M1 ROM binaries and MAME ZIP romsets.\n",
        prog, prog, prog);
}

static const char *reg_name(int reg) {
    static const char *names[] = {"B","C","D","E","H","L","(HL)","A"};
    if (reg >= 0 && reg <= 7) return names[reg];
    return "?";
}

static int cmd_disasm(const char *rom_path, int argc, char **argv) {
    RomFile rom;
    if (!rom_load(rom_path, rom)) return 1;

    size_t start = 0;
    size_t end = rom.data.size();

    if (argc >= 1) start = strtoul(argv[0], nullptr, 0);
    if (argc >= 2) end = strtoul(argv[1], nullptr, 0);

    if (start >= rom.data.size()) {
        fprintf(stderr, "Error: start address 0x%04zX beyond ROM size\n", start);
        return 1;
    }
    if (end > rom.data.size()) end = rom.data.size();

    printf("; Disassembly of %s (%zu bytes)\n", rom.filename.c_str(), rom.data.size());
    printf("; Range: 0x%04zX - 0x%04zX\n\n", start, end);

    size_t offset = start;
    while (offset < end) {
        Z80Insn insn;
        int len = z80_decode(rom.data.data(), rom.data.size(), offset, &insn);
        if (len <= 0) break;

        char buf[128];
        z80_format(&insn, rom.data.data(), buf, sizeof(buf));
        printf("%s\n", buf);

        offset += len;
    }

    return 0;
}

// Check if the ROM appears to already be patched.
// Looks for our preamble signature: PUSH AF, LD A,r, CP 0x11
// at the target of a JP instruction at common YM write routine locations.
static bool looks_already_patched(const uint8_t *rom, size_t rom_size) {
    // Check RST vectors and scan for JP instructions at typical entry points
    auto check_jp_target = [&](size_t addr) -> bool {
        if (addr + 2 >= rom_size) return false;
        if (rom[addr] != 0xC3) return false; // Not a JP
        uint16_t target = rom[addr + 1] | (rom[addr + 2] << 8);
        if ((size_t)target + 5 >= rom_size) return false;
        // Check for our preamble: F5 (PUSH AF), 7x (LD A,r), FE 11 (CP 0x11)
        return rom[target] == 0xF5 &&
               (rom[target + 1] & 0xF8) == 0x78 && // LD A, r (0x78-0x7F)
               rom[target + 2] == 0xFE &&
               rom[target + 3] == 0x11;
    };

    // Check RST 0x08 and RST 0x10 (common YM write vector locations)
    for (uint16_t rst = 0x08; rst <= 0x38; rst += 8) {
        if (check_jp_target(rst)) return true;
    }

    // Scan CALL targets: look for JP instructions in the first 64KB
    // that point to our preamble signature
    size_t scan_size = rom_size < 0x10000 ? rom_size : 0x10000;
    for (size_t i = 0; i + 2 < scan_size; i++) {
        if (rom[i] == 0xCD) { // CALL nn
            uint16_t target = rom[i + 1] | (rom[i + 2] << 8);
            if (check_jp_target(target)) return true;
        }
    }

    return false;
}

static int cmd_analyze(const char *rom_path, bool verbose) {
    RomFile rom;
    if (!rom_load(rom_path, rom)) return 1;

    printf("ROM: %s (%zu bytes)\n\n", rom.filename.c_str(), rom.data.size());

    auto routines = find_ym_write_routines(rom.data.data(), rom.data.size(), verbose);

    if (routines.empty()) {
        if (looks_already_patched(rom.data.data(), rom.data.size()))
            printf("No YM2610 pair-A write routines found (ROM appears already patched).\n");
        else
            printf("No YM2610 pair-A write routines found.\n");
        return 1;
    }

    printf("YM2610 pair-A write routine(s) found:\n");
    for (const auto &r : routines) {
        printf("  0x%04zX: addr_reg=%s, data_reg=%s, body=0x%04zX-0x%04zX (%s)\n",
               r.entry_addr, reg_name(r.addr_reg), reg_name(r.data_reg),
               r.body_start, r.body_end,
               r.is_rst ? "RST" : "CALL");

        // Show the routine bytes
        printf("    bytes:");
        for (size_t i = r.body_start; i < r.body_end && i < r.body_start + 20; i++)
            printf(" %02X", rom.data[i]);
        if (r.body_end - r.body_start > 20) printf(" ...");
        printf("\n");
    }

    // Find free space
    size_t needed = 20 + (routines[0].body_end - routines[0].body_start);
    auto free = find_free_space(rom.data.data(), rom.data.size(), needed);

    printf("\nFree space: %zu bytes at 0x%04zX (fill: 0x%02X)\n",
           free.length, free.start, free.fill_byte);
    printf("Patch needs: %zu bytes\n", needed);

    if (free.length >= needed)
        printf("Status: READY to patch\n");
    else
        printf("Status: NOT ENOUGH free space (need %zu, have %zu)\n", needed, free.length);

    return 0;
}

static int cmd_patch(const char *rom_path, const char *output_path,
                     bool verbose, bool no_backup) {
    RomFile rom;
    if (!rom_load(rom_path, rom)) return 1;

    printf("ROM: %s (%zu bytes)\n\n", rom.filename.c_str(), rom.data.size());

    auto routines = find_ym_write_routines(rom.data.data(), rom.data.size(), verbose);

    if (routines.empty()) {
        if (looks_already_patched(rom.data.data(), rom.data.size()))
            printf("ROM appears already patched. Use an unpatched ROM.\n");
        else
            printf("No YM2610 pair-A write routines found. Cannot patch.\n");
        return 1;
    }

    // Use the first pair-A routine found
    // If multiple found, prefer one that is called via RST or most frequently called
    const auto &routine = routines[0];

    printf("Target routine: 0x%04zX (addr_reg=%s, data_reg=%s, %s)\n",
           routine.entry_addr, reg_name(routine.addr_reg), reg_name(routine.data_reg),
           routine.is_rst ? "RST" : "CALL");

    // Find free space
    size_t needed = 20 + (routine.body_end - routine.body_start);
    auto free_space = find_free_space(rom.data.data(), rom.data.size(), needed);

    if (free_space.length < needed) {
        fprintf(stderr, "Error: not enough free space (need %zu bytes, have %zu)\n",
                needed, free_space.length);
        return 1;
    }

    printf("Free space: %zu bytes at 0x%04zX\n", free_space.length, free_space.start);

    // Apply patch
    auto result = apply_subroutine_patch(rom.data.data(), rom.data.size(),
                                          routine, free_space, verbose);

    if (!result.success) {
        fprintf(stderr, "Error: %s\n", result.description.c_str());
        return 1;
    }

    printf("\n%s\n", result.description.c_str());

    // If ROM is repeated (e.g., 256KB = 4x64KB), replicate the patched first 64KB
    size_t block_size = 0x10000;
    if (rom.data.size() > block_size && (rom.data.size() % block_size) == 0) {
        size_t num_blocks = rom.data.size() / block_size;
        // Replicate patched block 0 to all other blocks
        for (size_t b = 1; b < num_blocks; b++) {
            memcpy(rom.data.data() + b * block_size,
                   rom.data.data(), block_size);
        }
        if (num_blocks > 1) {
            printf("Replicated patch across %zu x %zuKB blocks\n",
                   num_blocks, block_size / 1024);
        }
    }

    // Save
    std::string out = output_path ? output_path : rom.source_path;

    if (!output_path && !no_backup) {
        if (rom_backup(rom.source_path))
            printf("Backup: %s.bak\n", rom.source_path.c_str());
        else
            fprintf(stderr, "Warning: failed to create backup\n");
    }

    if (rom_save(rom, out)) {
        printf("Wrote: %s\n", out.c_str());
    } else {
        fprintf(stderr, "Error: failed to write output\n");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (argc < 3) {
        fprintf(stderr, "Error: missing ROM path\n");
        print_usage(argv[0]);
        return 1;
    }

    const char *rom_path = argv[2];

    if (strcmp(command, "disasm") == 0) {
        return cmd_disasm(rom_path, argc - 3, argv + 3);
    }

    // Parse common options
    bool verbose = false;
    bool no_backup = false;
    const char *output_path = nullptr;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = true;
        else if (strcmp(argv[i], "--no-backup") == 0) no_backup = true;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output_path = argv[++i];
    }

    if (strcmp(command, "analyze") == 0) {
        return cmd_analyze(rom_path, verbose);
    }

    if (strcmp(command, "patch") == 0) {
        return cmd_patch(rom_path, output_path, verbose, no_backup);
    }

    fprintf(stderr, "Error: unknown command '%s'\n", command);
    print_usage(argv[0]);
    return 1;
}
