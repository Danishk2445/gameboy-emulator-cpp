#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "core/gameboy.hpp"
#include "image_io.hpp"

namespace gbemu {

// Second window showing CPU state, disassembly, IO registers, the tile set,
// the background map with the visible viewport, OAM and palettes.
class Debugger {
public:
    Debugger() = default;
    ~Debugger() { close(); }
    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;

    bool open();
    void close();
    bool is_open() const { return window_ != nullptr; }
    SDL_WindowID window_id() const { return window_ ? SDL_GetWindowID(window_) : 0; }
    void toggle_map() { map_9c00_ = !map_9c00_; }

    void render(gb::GameBoy& gb, bool paused, const Palette& palette);

private:
    void text(float x, float y, const std::string& s, SDL_Color color);
    void draw_tiles(const gb::GameBoy& gb, const Palette& palette);
    void draw_map(const gb::GameBoy& gb, const Palette& palette);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* tiles_ = nullptr;
    SDL_Texture* map_ = nullptr;
    bool map_9c00_ = false;
};

}  // namespace gbemu
