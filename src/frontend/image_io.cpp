#include "image_io.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#include "stb_image_write.h"

namespace gbemu {

bool write_png(const std::string& path, const gb::Ppu::Framebuffer& fb, const Palette& palette) {
    std::vector<gb::u8> rgb(fb.size() * 3);
    for (size_t i = 0; i < fb.size(); ++i) {
        gb::u32 c = palette[fb[i] & 3];
        rgb[i * 3 + 0] = static_cast<gb::u8>(c >> 16);
        rgb[i * 3 + 1] = static_cast<gb::u8>(c >> 8);
        rgb[i * 3 + 2] = static_cast<gb::u8>(c);
    }
    return stbi_write_png(path.c_str(), gb::kScreenWidth, gb::kScreenHeight, 3, rgb.data(), gb::kScreenWidth * 3) != 0;
}

bool read_png_shades(const std::string& path, std::vector<gb::u8>& shades, std::string& error) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 3);
    if (!data) {
        error = "cannot read " + path + ": " + stbi_failure_reason();
        return false;
    }
    if (w != gb::kScreenWidth || h != gb::kScreenHeight) {
        stbi_image_free(data);
        error = path + " is not 160x144";
        return false;
    }
    shades.resize(static_cast<size_t>(w * h));
    for (int i = 0; i < w * h; ++i) {
        int lum = (data[i * 3] * 299 + data[i * 3 + 1] * 587 + data[i * 3 + 2] * 114) / 1000;
        shades[static_cast<size_t>(i)] = lum >= 0xD5 ? 0 : lum >= 0x80 ? 1 : lum >= 0x2B ? 2 : 3;
    }
    stbi_image_free(data);
    return true;
}

}  // namespace gbemu
