#pragma once

#include <cstdint>
#include <array>

class Memory;

class PPU {
public:
    PPU(Memory& mem);
    
    // Step PPU by given number of CPU cycles
    void step(int cycles);
    
    // Reset PPU state
    void reset();
    
    // Get framebuffer for rendering
    const uint32_t* getFramebuffer() const { return framebuffer.data(); }
    
    // Check if frame is ready
    bool isFrameReady() const { return frameReady; }
    void clearFrameReady() { frameReady = false; }
    
    // Register access
    void writeLCDC(uint8_t val);
    void writeSTAT(uint8_t val);
    void writeLY(uint8_t val);
    uint8_t readSTAT() const;
    uint8_t readLY() const { return ly; }
    
private:
    Memory& memory;
    
    // LCD registers
    uint8_t lcdc;   // 0xFF40 - LCD Control
    uint8_t stat;   // 0xFF41 - LCD Status
    uint8_t scy;    // 0xFF42 - Scroll Y
    uint8_t scx;    // 0xFF43 - Scroll X
    uint8_t ly;     // 0xFF44 - Current scanline
    uint8_t lyc;    // 0xFF45 - LY Compare
    uint8_t wy;     // 0xFF4A - Window Y
    uint8_t wx;     // 0xFF4B - Window X
    uint8_t bgp;    // 0xFF47 - BG Palette
    uint8_t obp0;   // 0xFF48 - OBJ Palette 0
    uint8_t obp1;   // 0xFF49 - OBJ Palette 1
    
    // Internal state
    int scanlineCycles;
    int mode;       // 0=HBlank, 1=VBlank, 2=OAM, 3=Transfer
    bool frameReady;
    int windowLine; // Internal window line counter
    
    // Framebuffer (160x144 ARGB)
    std::array<uint32_t, 160 * 144> framebuffer;
    
    // DMG color palette (original grayscale)
    static constexpr uint32_t colors[4] = {
        0xFFFFFFFF,  // White
        0xFFAAAAAA,  // Light gray
        0xFF555555,  // Dark gray
        0xFF000000   // Black
    };
    
    void renderScanline();
    void renderBackground(std::array<uint8_t, 160>& bgPriority);
    void renderWindow(std::array<uint8_t, 160>& bgPriority);
    void renderSprites(const std::array<uint8_t, 160>& bgPriority);
    
    void setMode(int newMode);
    void checkSTATInterrupt();
    void checkLYC();
};

// Screen dimensions
constexpr int SCREEN_WIDTH = 160;
constexpr int SCREEN_HEIGHT = 144;
