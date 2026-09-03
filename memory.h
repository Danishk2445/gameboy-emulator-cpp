#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <iosfwd>

class PPU;
class CPU;
class APU;

// What the CPU drove onto the bus during an M-cycle, for the purposes of the
// DMG OAM corruption bug. ReadWrite is a read sharing its M-cycle with a
// 16-bit increment/decrement, which corrupts differently from either alone.
enum class OamBugOp { Read, Write, ReadWrite };

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

    // Advance timer, DMA, PPU and APU by `cycles` T-cycles. The CPU calls this
    // from inside each instruction (once per memory access / internal cycle),
    // so peripherals stay in step with the bus rather than lurching forward a
    // whole instruction at a time.
    void tick(int cycles);

    // The DMG OAM corruption bug: any CPU bus activity aimed at $FE00-$FEFF
    // while the PPU is scanning OAM scrambles the row it is reading. The CPU
    // calls this for real accesses and for the address its 16-bit
    // increment/decrement unit puts on the bus. Addresses outside OAM and
    // M-cycles outside the scan are ignored, so callers need not check.
    void oamBugAccess(uint16_t addr, OamBugOp op);

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

    // Internal 16-bit system counter, incremented every T-cycle. DIV (FF04) is
    // simply its top 8 bits, and TIMA counts *falling edges* of one of its bits
    // (selected by TAC) — which is why writing DIV or TAC can tick the timer.
    uint16_t divCounter = 0;
    // Counter value before the current M-cycle's step, needed because a TAC
    // write lands partway through the M-cycle: on hardware the new TAC is
    // already in effect for the counter transition we have just stepped over.
    uint16_t prevDivCounter = 0;
    bool timerSignal = false;
    // An overflowed TIMA reads 0 for one M-cycle before TMA is loaded and the
    // interrupt is raised; a write to TIMA inside that window cancels both.
    bool timaReloading = false;
    // True for the rest of the M-cycle in which the reload actually happened.
    // On that cycle a TIMA write is ignored and a TMA write also lands in TIMA.
    bool timaJustReloaded = false;

    bool dmaActive = false;
    int  dmaCycles = 0;
    uint16_t dmaSource = 0;

    uint8_t joypadButtons = 0x0F;
    uint8_t joypadDpad    = 0x0F;

    void    updateTimer(int cycles);
    void    setDivCounter(uint16_t v);
    void    updateTimerEdge();
    void    incTIMA();
    void    updateDMA(int cycles);
    uint8_t readJoypad() const;
    void    handleMBCWrite(uint16_t addr, uint8_t val);
    int     timerBit() const;

    // OAM is 20 rows of four 16-bit words, and the corruption works on whole
    // words because that is how wide the OAM data bus is.
    uint16_t oamWord(int row, int word) const;
    void     setOamWord(int row, int word, uint16_t v);
    void     oamCorruptRead(int row);
    void     oamCorruptWrite(int row);
    void     oamCorruptReadWrite(int row);
};
