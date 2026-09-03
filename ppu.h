#pragma once

#include <cstdint>
#include <array>
#include <iosfwd>

class Memory;

constexpr int SCREEN_WIDTH  = 160;
constexpr int SCREEN_HEIGHT = 144;

class PPU {
public:
    explicit PPU(Memory& mem);

    void reset();
    void step(int cycles);

    const uint32_t* getFramebuffer() const { return framebuffer.data(); }
    bool isFrameReady() const { return frameReady; }
    void clearFrameReady() { frameReady = false; }

    void writeLCDC(uint8_t v);
    void writeSTAT(uint8_t v);
    void writeLY(uint8_t /*v*/) { ly = 0; }
    uint8_t readSTAT() const;
    uint8_t readLY()   const { return ly; }

    // During mode 2 the PPU walks OAM one 8-byte row per M-cycle. These report
    // whether the M-cycle that just elapsed overlapped that scan and which of
    // the 20 rows it read — the CPU needs both for the DMG OAM corruption bug.
    // The row can come back as 20, one past the end; callers ignore it.
    bool oamScanActive() const { return scanActive; }
    int  oamScanRow()    const { return scanRow; }

    void saveState(std::ostream& out) const;
    bool loadState(std::istream& in);

    void setPalette(const uint32_t shadesIn[4]);

private:
    Memory& memory;

    uint8_t lcdc = 0;
    uint8_t stat = 0;
    uint8_t ly   = 0;

    int  scanlineCycles = 0;
    int  mode = 2;
    bool frameReady = false;
    int  windowLine = 0;
    bool prevStatLine = false;
    bool scanActive = false;
    int  scanRow = 0;

    std::array<uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT> framebuffer{};

    uint32_t shades[4] = {
        0xFFFFFFFFu, 0xFFAAAAAAu, 0xFF555555u, 0xFF000000u
    };

    void renderScanline();
    void renderBackground(std::array<uint8_t, SCREEN_WIDTH>& bgIdx);
    void renderWindow(std::array<uint8_t, SCREEN_WIDTH>& bgIdx);
    void renderSprites(const std::array<uint8_t, SCREEN_WIDTH>& bgIdx);

    void setMode(int m);
    void updateStatLine();
};
