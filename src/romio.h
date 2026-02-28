// SPDX-License-Identifier: MIT
// https://github.com/m0rb/adpcmb-swap-tool

#ifndef ROMIO_H
#define ROMIO_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct RomFile {
    std::vector<uint8_t> data;
    std::string filename;       // Original filename (basename)
    std::string source_path;    // Full path to source (ZIP or raw file)
    std::string zip_entry;      // Entry name if from ZIP, empty otherwise
    bool from_zip;

    RomFile() : from_zip(false) {}
};

// Load an M1 ROM. Auto-detects ZIP vs raw binary by extension.
// For ZIPs, searches for entries matching M1 ROM naming patterns.
// Returns true on success.
bool rom_load(const std::string &path, RomFile &rom);

// Save a ROM. If output_path ends in .zip, creates a ZIP.
// Otherwise writes raw binary.
bool rom_save(const RomFile &rom, const std::string &output_path);

// Save a raw binary file.
bool rom_save_raw(const std::string &path, const uint8_t *data, size_t size);

// Create a backup of the original file (appends .bak).
bool rom_backup(const std::string &path);

#endif // ROMIO_H
