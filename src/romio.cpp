// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#include "romio.h"
#include "miniz.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

// Check if a string ends with a suffix (case-insensitive)
static bool ends_with_ci(const std::string &str, const std::string &suffix) {
    if (suffix.size() > str.size()) return false;
    auto it = str.end() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (tolower((unsigned char)*(it + i)) != tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

// Check if a filename looks like an M1 ROM
static bool is_m1_filename(const std::string &name) {
    // Common patterns: *-m1.m1, *.m1, *m1*.bin
    if (ends_with_ci(name, ".m1")) return true;
    // Also check for names containing "m1" with common ROM extensions
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return tolower(c); });
    if (lower.find("m1") != std::string::npos &&
        (ends_with_ci(name, ".bin") || ends_with_ci(name, ".rom"))) {
        return true;
    }
    return false;
}

static bool load_raw(const std::string &path, RomFile &rom) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", path.c_str());
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 0x100000) { // Max 1MB for an M1 ROM
        fprintf(stderr, "Error: file size %ld is unexpected for an M1 ROM\n", size);
        fclose(f);
        return false;
    }

    rom.data.resize(size);
    size_t read = fread(rom.data.data(), 1, size, f);
    fclose(f);

    if ((long)read != size) {
        fprintf(stderr, "Error: short read from '%s'\n", path.c_str());
        return false;
    }

    // Extract basename
    size_t slash = path.find_last_of("/\\");
    rom.filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    rom.source_path = path;
    rom.from_zip = false;
    return true;
}

static bool load_zip(const std::string &path, RomFile &rom) {
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        fprintf(stderr, "Error: cannot open ZIP file '%s'\n", path.c_str());
        return false;
    }

    int num_files = (int)mz_zip_reader_get_num_files(&zip);
    int m1_index = -1;

    // Search for M1 ROM entry
    for (int i = 0; i < num_files; i++) {
        char name[256];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        if (is_m1_filename(name)) {
            m1_index = i;
            rom.zip_entry = name;
            break;
        }
    }

    if (m1_index < 0) {
        fprintf(stderr, "Error: no M1 ROM found in ZIP '%s'\n", path.c_str());
        mz_zip_reader_end(&zip);
        return false;
    }

    // Extract to memory
    size_t uncomp_size = 0;
    void *data = mz_zip_reader_extract_to_heap(&zip, m1_index, &uncomp_size, 0);
    mz_zip_reader_end(&zip);

    if (!data) {
        fprintf(stderr, "Error: failed to extract '%s' from ZIP\n", rom.zip_entry.c_str());
        return false;
    }

    rom.data.assign((uint8_t *)data, (uint8_t *)data + uncomp_size);
    mz_free(data);

    rom.filename = rom.zip_entry;
    rom.source_path = path;
    rom.from_zip = true;
    return true;
}

bool rom_load(const std::string &path, RomFile &rom) {
    if (ends_with_ci(path, ".zip")) {
        return load_zip(path, rom);
    }
    return load_raw(path, rom);
}

bool rom_save(const RomFile &rom, const std::string &output_path) {
    if (ends_with_ci(output_path, ".zip")) {
        // Write as ZIP
        mz_zip_archive zip;
        memset(&zip, 0, sizeof(zip));

        if (!mz_zip_writer_init_file(&zip, output_path.c_str(), 0)) {
            fprintf(stderr, "Error: cannot create ZIP file '%s'\n", output_path.c_str());
            return false;
        }

        std::string entry_name = rom.zip_entry.empty() ? rom.filename : rom.zip_entry;
        if (!mz_zip_writer_add_mem(&zip, entry_name.c_str(),
                                    rom.data.data(), rom.data.size(),
                                    MZ_DEFAULT_COMPRESSION)) {
            fprintf(stderr, "Error: failed to write entry to ZIP\n");
            mz_zip_writer_end(&zip);
            return false;
        }

        if (!mz_zip_writer_finalize_archive(&zip)) {
            fprintf(stderr, "Error: failed to finalize ZIP archive\n");
            mz_zip_writer_end(&zip);
            return false;
        }

        mz_zip_writer_end(&zip);
        return true;
    }

    return rom_save_raw(output_path, rom.data.data(), rom.data.size());
}

bool rom_save_raw(const std::string &path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "Error: cannot create file '%s'\n", path.c_str());
        return false;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        fprintf(stderr, "Error: short write to '%s'\n", path.c_str());
        return false;
    }

    return true;
}

bool rom_backup(const std::string &path) {
    std::string bak = path + ".bak";
    FILE *src = fopen(path.c_str(), "rb");
    if (!src) return false;

    FILE *dst = fopen(bak.c_str(), "wb");
    if (!dst) { fclose(src); return false; }

    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        fwrite(buf, 1, n, dst);
    }

    fclose(src);
    fclose(dst);
    return true;
}
