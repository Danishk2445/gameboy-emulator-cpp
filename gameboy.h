#pragma once

#include <string>
#include <SDL.h>

class CPU;
class Memory;
class PPU;
class APU;
class UI;

class GameBoy {
public:
    GameBoy();
    ~GameBoy();

    bool init();
    bool loadROM(const std::string& path);
    void run();

private:
    Memory* memory = nullptr;
    CPU*    cpu    = nullptr;
    PPU*    ppu    = nullptr;
    APU*    apu    = nullptr;
    UI*     ui     = nullptr;

    SDL_Window*   window   = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture  = nullptr;

    bool running = false;
    bool paused = false;
    bool fastForward = false;
    bool fullscreen = false;
    bool muted = false;
    int  windowScale = 4;
    int  saveSlot = 0;
    int  paletteIdx = 1;

    uint8_t buttons = 0x0F;
    uint8_t dpad    = 0x0F;

    static constexpr int    CYCLES_PER_FRAME = 70224;
    static constexpr double FRAME_TIME = 1000.0 / 59.7275;
    static constexpr int    FAST_FORWARD_FRAMES = 4;

    void pollInput();
    void presentFrame();
    void runOneFrame();
    void applyUIAction();

    void resetGame();
    bool unloadCurrentROM();

    bool saveStateToFile(const std::string& path);
    bool loadStateFromFile(const std::string& path);
    void saveStateSlot(int slot);
    void loadStateSlot(int slot);
    std::string statePathForSlot(int slot) const;
    void takeScreenshot();

    void setPaletteByIndex(int idx);
    const char* paletteName(int idx) const;
    void toggleFullscreen();
};
