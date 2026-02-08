#pragma once

#include <string>
#include <SDL.h>

class CPU;
class Memory;
class PPU;
class APU;

class GameBoy {
public:
    GameBoy();
    ~GameBoy();
    
    // Initialize emulator
    bool init();
    
    // Load ROM file
    bool loadROM(const std::string& path);
    
    // Run the emulator
    void run();
    
private:
    // Components
    Memory* memory;
    CPU* cpu;
    PPU* ppu;
    APU* apu;
    
    // SDL
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    
    bool running;
    
    // Frame timing
    static constexpr int CYCLES_PER_FRAME = 70224;  // 4194304 Hz / 59.73 FPS
    static constexpr double FRAME_TIME = 1000.0 / 59.7275;
    
    void handleInput(const SDL_Event& event);
    void updateTexture();
};
