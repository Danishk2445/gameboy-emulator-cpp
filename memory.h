#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <iosfwd>

class PPU;
class CPU;
class APU;

class Memory {
public:
    Memory();
    ~Memory();

    bool loadROM(const std::string& path);
    bool saveSRAM() const;
    void unloadROM();

    std::string romTitle() const;
    const std::string& romPath() const { return loadedPath; }
    bool hasROM() const { return !rom.empty(); }

    void saveState(std::ostream& out) const;
    bool loadState(std::istream& in);

    uint8_t read(uint16_t addr) const;
    void    write(uint16_t addr, uint8_t val);

    void setPPU(PPU* p) { ppu = p; }
    void setCPU(CPU* c) { cpu = c; }
    void setAPU(APU* a) { apu = a; }

    void setJoypadState(uint8_t buttons, uint8_t dpad);

    void updateTimer(int cycles);
    void updateDMA(int cycles);

    uint8_t getIF() const { return io[0x0F]; }
    void    setIF(uint8_t v) { io[0x0F] = v; }
    uint8_t getIE() const { return ie; }

    const uint8_t* getVRAM() const { return vram.data(); }
    const uint8_t* getOAM()  const { return oam.data(); }
    uint8_t readIO(uint8_t reg) const { return io[reg]; }
    void    writeIO(uint8_t reg, uint8_t val) { io[reg] = val; }

private:
    PPU* ppu = nullptr;
    CPU* cpu = nullptr;
    APU* apu = nullptr;

    std::vector<uint8_t> rom;
    std::vector<uint8_t> extRam;
    std::vector<uint8_t> vram;
    std::vector<uint8_t> wram;
    std::vector<uint8_t> oam;
    std::vector<uint8_t> io;
    std::vector<uint8_t> hram;
    uint8_t ie = 0;

    uint8_t mbcType = 0;
    int  romBank = 1;
    int  ramBank = 0;
    bool ramEnabled = false;
    bool mbc1RamMode = false;
    uint8_t rtcRegister = 0;

    std::string savePath;
    std::string loadedPath;
    bool hasBattery = false;
    mutable bool sramDirty = false;

    int divCounter = 0;
    int timerCounter = 0;

    bool dmaActive = false;
    int  dmaCycles = 0;
    uint16_t dmaSource = 0;

    uint8_t joypadButtons = 0x0F;
    uint8_t joypadDpad    = 0x0F;

    uint8_t readJoypad() const;
    void    handleMBCWrite(uint16_t addr, uint8_t val);
    int     getTimerFrequency() const;
};
