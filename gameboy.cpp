#include "gameboy.h"
#include "cpu.h"
#include "memory.h"
#include "ppu.h"
#include "apu.h"
#include <iostream>
#include <chrono>
#include <thread>

GameBoy::GameBoy() : 
    memory(nullptr),
    cpu(nullptr),
    ppu(nullptr),
    apu(nullptr),
    window(nullptr),
    renderer(nullptr),
    texture(nullptr),
    running(false)
{
}

GameBoy::~GameBoy() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    
    delete apu;
    delete ppu;
    delete cpu;
    delete memory;
}

bool GameBoy::init() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL init failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create window (4x scale)
    window = SDL_CreateWindow(
        "Game Boy Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * 4,
        SCREEN_HEIGHT * 4,
        SDL_WINDOW_SHOWN
    );
    
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create texture
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );
    
    if (!texture) {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create emulator components
    memory = new Memory();
    ppu = new PPU(*memory);
    apu = new APU();
    cpu = new CPU(*memory);
    
    memory->setPPU(ppu);
    memory->setAPU(apu);
    
    // Initialize audio
    if (!apu->init()) {
        std::cerr << "Audio init failed" << std::endl;
        // Continue without audio
    }
    
    return true;
}

bool GameBoy::loadROM(const std::string& path) {
    if (!memory->loadROM(path)) {
        std::cerr << "Failed to load ROM: " << path << std::endl;
        return false;
    }
    return true;
}

void GameBoy::handleInput(const SDL_Event& event) {
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) return;
    
    bool pressed = (event.type == SDL_KEYDOWN);
    
    // Current state (active low)
    static uint8_t buttons = 0xFF;  // A, B, Select, Start
    static uint8_t dpad = 0xFF;     // Right, Left, Up, Down
    
    switch (event.key.keysym.sym) {
        // D-pad
        case SDLK_RIGHT: dpad = pressed ? (dpad & ~0x01) : (dpad | 0x01); break;
        case SDLK_LEFT:  dpad = pressed ? (dpad & ~0x02) : (dpad | 0x02); break;
        case SDLK_UP:    dpad = pressed ? (dpad & ~0x04) : (dpad | 0x04); break;
        case SDLK_DOWN:  dpad = pressed ? (dpad & ~0x08) : (dpad | 0x08); break;
        
        // Buttons
        case SDLK_z:         buttons = pressed ? (buttons & ~0x01) : (buttons | 0x01); break;  // A
        case SDLK_x:         buttons = pressed ? (buttons & ~0x02) : (buttons | 0x02); break;  // B
        case SDLK_BACKSPACE: buttons = pressed ? (buttons & ~0x04) : (buttons | 0x04); break;  // Select
        case SDLK_RETURN:    buttons = pressed ? (buttons & ~0x08) : (buttons | 0x08); break;  // Start
        
        default: break;
    }
    
    memory->setJoypadState(buttons, dpad);
    
    // Request joypad interrupt on button press
    if (pressed) {
        cpu->requestInterrupt(INT_JOYPAD);
    }
}

void GameBoy::updateTexture() {
    SDL_UpdateTexture(texture, nullptr, ppu->getFramebuffer(), SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void GameBoy::run() {
    running = true;
    
    auto lastFrame = std::chrono::high_resolution_clock::now();
    
    while (running) {
        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                handleInput(event);
                
                // Escape to quit
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }
        
        // Run emulation for one frame
        int cyclesThisFrame = 0;
        while (cyclesThisFrame < CYCLES_PER_FRAME) {
            int cycles = cpu->step();
            cyclesThisFrame += cycles;
            
            ppu->step(cycles);
            apu->step(cycles);
            memory->updateTimer(cycles);
            memory->updateDMA(cycles);
            
            // Render when frame is ready
            if (ppu->isFrameReady()) {
                ppu->clearFrameReady();
                updateTexture();
            }
        }
        
        // Frame timing
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrame).count();
        
        if (elapsed < FRAME_TIME) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(FRAME_TIME - elapsed)));
        }
        
        lastFrame = std::chrono::high_resolution_clock::now();
    }
}
