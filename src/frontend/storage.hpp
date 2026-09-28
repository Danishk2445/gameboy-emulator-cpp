#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/common.hpp"

namespace gbemu {

// ~/.local/share/gbemu/saves on Linux (via SDL_GetPrefPath).
std::filesystem::path default_save_dir();

std::filesystem::path battery_path(const std::filesystem::path& save_dir, const std::string& rom_path);
std::filesystem::path state_path(const std::filesystem::path& save_dir, const std::string& rom_path, int slot);

bool read_file(const std::filesystem::path& path, std::vector<gb::u8>& out);
// Writes to a temporary file and renames it over the target so a crash never leaves a half-written save.
bool write_file_atomic(const std::filesystem::path& path, const std::vector<gb::u8>& data);

}  // namespace gbemu
