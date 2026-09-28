#include "storage.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

namespace gbemu {

namespace fs = std::filesystem;

fs::path default_save_dir() {
    fs::path base;
    if (char* pref = SDL_GetPrefPath("", "gbemu")) {
        base = pref;
        SDL_free(pref);
    } else if (const char* home = std::getenv("HOME")) {
        base = fs::path(home) / ".local/share/gbemu";
    } else {
        base = ".";
    }
    return base / "saves";
}

fs::path battery_path(const fs::path& save_dir, const std::string& rom_path) {
    return save_dir / (fs::path(rom_path).stem().string() + ".sav");
}

fs::path state_path(const fs::path& save_dir, const std::string& rom_path, int slot) {
    return save_dir / (fs::path(rom_path).stem().string() + ".st" + std::to_string(slot));
}

bool read_file(const fs::path& path, std::vector<gb::u8>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool write_file_atomic(const fs::path& path, const std::vector<gb::u8>& data) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

}  // namespace gbemu
