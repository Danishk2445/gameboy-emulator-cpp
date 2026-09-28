#pragma once

#include <array>
#include <string>
#include <vector>

#include "core/ppu.hpp"

namespace gbemu {

// 0xRRGGBB colours for shades 0-3.
using Palette = std::array<gb::u32, 4>;

constexpr Palette kPaletteGrey = {0xFFFFFF, 0xAAAAAA, 0x555555, 0x000000};
constexpr Palette kPaletteGreen = {0xE0F8D0, 0x88C070, 0x346856, 0x081820};

bool write_png(const std::string& path, const gb::Ppu::Framebuffer& fb, const Palette& palette);

// Loads a 160x144 reference image and converts it to shades 0-3 by luminance.
bool read_png_shades(const std::string& path, std::vector<gb::u8>& shades, std::string& error);

}  // namespace gbemu
