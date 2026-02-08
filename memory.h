#pragma once

#include <cstdint>
#include <vector>
#include <string>

class PPU;
class APU;

class Memory {
public:
    Memory();
    
    // Load ROM file
    bool loadROM(const std::string& path);
    
    // Memory access
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t val);
    
    // Set component references
    void setPPU(PPU* p) { ppu = p; }
    void setAPU(APU* a) { apu = a; }
    
    // Joypad
    void setJoypadState(uint8_t buttons, uint8_t dpad);
    
    // Timer update (called per CPU cycle)
    void updateTimer(int cycles);
    
    // DMA transfer
    void updateDMA(int cycles);
    
    // Get interrupt flags
    uint8_t getIF() const { return io[0x0F]; }
    void setIF(uint8_t val) { io[0x0F] = val; }
    uint8_t getIE() const { return ie; }
    
    // Direct VRAM/OAM access for PPU
    const uint8_t* getVRAM() const { return vram.data(); }
    const uint8_t* getOAM() const { return oam.data(); }
    uint8_t readIO(uint8_t reg) const { return io[reg]; }
    void writeIO(uint8_t reg, uint8_t val) { io[reg] = val; }

private:
    PPU* ppu = nullptr;
    APU* apu = nullptr;
    
    // ROM data
    std::vector<uint8_t> rom;
    std::vector<uint8_t> extRam;
    
    // Memory regions
    std::vector<uint8_t> vram;     // 0x8000-0x9FFF (8KB)
    std::vector<uint8_t> wram;     // 0xC000-0xDFFF (8KB)
    std::vector<uint8_t> oam;      // 0xFE00-0xFE9F (160 bytes)
    std::vector<uint8_t> io;       // 0xFF00-0xFF7F (128 bytes)
    std::vector<uint8_t> hram;     // 0xFF80-0xFFFE (127 bytes)
    uint8_t ie;                    // 0xFFFF
    
    // MBC state
    uint8_t mbcType;
    int romBank;
    int ramBank;
    bool ramEnabled;
    bool rtcEnabled;
    uint8_t rtcRegister;
    
    // Timer state
    int timerCounter;
    int divCounter;
    
    // DMA state
    bool dmaActive;
    int dmaCycles;
    uint16_t dmaSource;
    
    // Joypad state
    uint8_t joypadButtons;  // A, B, Select, Start
    uint8_t joypadDpad;     // Right, Left, Up, Down
    
    uint8_t readJoypad() const;
    void handleMBCWrite(uint16_t addr, uint8_t val);
    int getTimerFrequency() const;
};
