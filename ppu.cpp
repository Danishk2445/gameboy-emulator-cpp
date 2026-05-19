#include "ppu.h"
#include "memory.h"
#include "cpu.h"

#include <algorithm>  // std::max
#include <ostream>
#include <istream>

namespace {
struct PpuStateBlob {
    uint8_t lcdc, stat, ly;
    int32_t scanlineCycles;
    int32_t mode;
    uint8_t frameReady;
    int32_t windowLine;
    uint8_t prevStatLine;
};
}

void PPU::saveState(std::ostream& out) const {
    PpuStateBlob s{};
    s.lcdc = lcdc; s.stat = stat; s.ly = ly;
    s.scanlineCycles = scanlineCycles;
    s.mode = mode;
    s.frameReady = frameReady ? 1 : 0;
    s.windowLine = windowLine;
    s.prevStatLine = prevStatLine ? 1 : 0;
    out.write(reinterpret_cast<const char*>(&s), sizeof(s));
    out.write(reinterpret_cast<const char*>(framebuffer.data()),
              framebuffer.size() * sizeof(uint32_t));
}

void PPU::setPalette(const uint32_t shadesIn[4]) {
    for (int i = 0; i < 4; ++i) shades[i] = shadesIn[i];
}

bool PPU::loadState(std::istream& in) {
    PpuStateBlob s{};
    if (!in.read(reinterpret_cast<char*>(&s), sizeof(s))) return false;
    lcdc = s.lcdc; stat = s.stat; ly = s.ly;
    scanlineCycles = s.scanlineCycles;
    mode = s.mode;
    frameReady = s.frameReady != 0;
    windowLine = s.windowLine;
    prevStatLine = s.prevStatLine != 0;
    if (!in.read(reinterpret_cast<char*>(framebuffer.data()),
                 framebuffer.size() * sizeof(uint32_t))) return false;
    return true;
}

PPU::PPU(Memory& mem) : memory(mem) { reset(); }

void PPU::reset() {
    lcdc = memory.readIO(0x40);
    stat = memory.readIO(0x41);
    ly = 0;
    scanlineCycles = 0;
    mode = 2;
    frameReady = false;
    windowLine = 0;
    prevStatLine = false;
    framebuffer.fill(shades[0]);
}

void PPU::writeLCDC(uint8_t v) {
    bool wasOn = (lcdc & 0x80) != 0;
    bool isOn  = (v & 0x80) != 0;
    lcdc = v;
    if (wasOn && !isOn) {
        ly = 0;
        scanlineCycles = 0;
        mode = 0;
        stat = (stat & 0xFC);
        memory.writeIO(0x41, stat);
        memory.writeIO(0x44, 0);
        windowLine = 0;
    }
}

void PPU::writeSTAT(uint8_t v) {
    // Lower 3 bits (mode + coincidence) are read-only
    stat = (v & 0x78) | (stat & 0x07);
}

uint8_t PPU::readSTAT() const {
    uint8_t s = (stat & 0xF8) | uint8_t(mode & 0x03);
    if (ly == memory.readIO(0x45)) s |= 0x04;
    return s | 0x80;
}

void PPU::setMode(int m) {
    mode = m & 0x03;
    stat = (stat & 0xFC) | uint8_t(mode);
    memory.writeIO(0x41, (memory.readIO(0x41) & 0xFC) | uint8_t(mode));
}

void PPU::updateStatLine() {
    uint8_t curStat = readSTAT();
    memory.writeIO(0x41, curStat);
    bool line = false;
    if ((curStat & 0x40) && (curStat & 0x04)) line = true; // LYC=LY
    if ((curStat & 0x20) && mode == 2) line = true;
    if ((curStat & 0x10) && mode == 1) line = true;
    if ((curStat & 0x08) && mode == 0) line = true;
    if (line && !prevStatLine) {
        memory.setIF(memory.getIF() | INT_STAT);
    }
    prevStatLine = line;
}

void PPU::step(int cycles) {
    lcdc = memory.readIO(0x40);
    if ((lcdc & 0x80) == 0) {
        return;
    }

    scanlineCycles += cycles;

    switch (mode) {
        case 2:
            if (scanlineCycles >= 80) {
                scanlineCycles -= 80;
                setMode(3);
                updateStatLine();
            }
            break;
        case 3:
            if (scanlineCycles >= 172) {
                scanlineCycles -= 172;
                renderScanline();
                setMode(0);
                updateStatLine();
            }
            break;
        case 0:
            if (scanlineCycles >= 204) {
                scanlineCycles -= 204;
                ly++;
                memory.writeIO(0x44, ly);
                if (ly == 144) {
                    setMode(1);
                    memory.setIF(memory.getIF() | INT_VBLANK);
                    frameReady = true;
                    windowLine = 0;
                } else {
                    setMode(2);
                }
                updateStatLine();
            }
            break;
        case 1:
            if (scanlineCycles >= 456) {
                scanlineCycles -= 456;
                ly++;
                if (ly > 153) {
                    ly = 0;
                    memory.writeIO(0x44, 0);
                    setMode(2);
                } else {
                    memory.writeIO(0x44, ly);
                }
                updateStatLine();
            }
            break;
    }
}

void PPU::renderScanline() {
    std::array<uint8_t, SCREEN_WIDTH> bgIdx{};
    bgIdx.fill(0);

    if (lcdc & 0x01) {
        renderBackground(bgIdx);
        if (lcdc & 0x20) renderWindow(bgIdx);
    } else {
        uint32_t row = ly * SCREEN_WIDTH;
        for (int x = 0; x < SCREEN_WIDTH; ++x) framebuffer[row + x] = shades[0];
    }

    if (lcdc & 0x02) renderSprites(bgIdx);
}

void PPU::renderBackground(std::array<uint8_t, SCREEN_WIDTH>& bgIdx) {
    uint8_t scy = memory.readIO(0x42);
    uint8_t scx = memory.readIO(0x43);
    uint8_t bgp = memory.readIO(0x47);
    const uint8_t* vram = memory.getVRAM();

    uint16_t mapBase = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    bool unsignedIdx = (lcdc & 0x10) != 0;
    uint16_t tileBase = unsignedIdx ? 0x0000 : 0x1000;

    uint8_t y = uint8_t(ly + scy);
    int tileRow = (y / 8) & 31;

    uint32_t row = ly * SCREEN_WIDTH;
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        uint8_t px = uint8_t(x + scx);
        int tileCol = (px / 8) & 31;
        uint16_t mapAddr = mapBase + tileRow * 32 + tileCol;
        uint8_t tileIdx = vram[mapAddr];

        uint16_t tileAddr;
        if (unsignedIdx) {
            tileAddr = tileBase + tileIdx * 16;
        } else {
            tileAddr = uint16_t(tileBase + int16_t(int8_t(tileIdx)) * 16);
        }
        int line = (y & 7) * 2;
        uint8_t lo = vram[tileAddr + line];
        uint8_t hi = vram[tileAddr + line + 1];
        int bit = 7 - (px & 7);
        uint8_t shade = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        bgIdx[x] = shade;
        uint8_t color = (bgp >> (shade * 2)) & 0x03;
        framebuffer[row + x] = shades[color];
    }
}

void PPU::renderWindow(std::array<uint8_t, SCREEN_WIDTH>& bgIdx) {
    uint8_t wy = memory.readIO(0x4A);
    uint8_t wx = memory.readIO(0x4B);
    if (ly < wy) return;
    if (wx >= 167) return;
    uint8_t bgp = memory.readIO(0x47);
    const uint8_t* vram = memory.getVRAM();

    uint16_t mapBase = (lcdc & 0x40) ? 0x1C00 : 0x1800;
    bool unsignedIdx = (lcdc & 0x10) != 0;
    uint16_t tileBase = unsignedIdx ? 0x0000 : 0x1000;

    int wyLine = windowLine;
    int tileRow = (wyLine / 8) & 31;

    uint32_t row = ly * SCREEN_WIDTH;
    int startX = int(wx) - 7;

    bool drew = false;
    for (int x = std::max(0, startX); x < SCREEN_WIDTH; ++x) {
        int wxPos = x - startX;
        if (wxPos < 0) continue;
        int tileCol = (wxPos / 8) & 31;
        uint16_t mapAddr = mapBase + tileRow * 32 + tileCol;
        uint8_t tileIdx = vram[mapAddr];
        uint16_t tileAddr;
        if (unsignedIdx) tileAddr = tileBase + tileIdx * 16;
        else             tileAddr = uint16_t(tileBase + int16_t(int8_t(tileIdx)) * 16);
        int line = (wyLine & 7) * 2;
        uint8_t lo = vram[tileAddr + line];
        uint8_t hi = vram[tileAddr + line + 1];
        int bit = 7 - (wxPos & 7);
        uint8_t shade = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        bgIdx[x] = shade;
        uint8_t color = (bgp >> (shade * 2)) & 0x03;
        framebuffer[row + x] = shades[color];
        drew = true;
    }
    if (drew) windowLine++;
}

void PPU::renderSprites(const std::array<uint8_t, SCREEN_WIDTH>& bgIdx) {
    bool tall = (lcdc & 0x04) != 0;
    int spriteH = tall ? 16 : 8;
    const uint8_t* oam = memory.getOAM();
    uint8_t obp0 = memory.readIO(0x48);
    uint8_t obp1 = memory.readIO(0x49);

    struct Spr { int x, oamIdx; uint8_t y, tile, attr; };
    Spr visible[10];
    int count = 0;
    for (int i = 0; i < 40 && count < 10; ++i) {
        uint8_t sy = oam[i * 4 + 0];
        int top = int(sy) - 16;
        if (int(ly) < top || int(ly) >= top + spriteH) continue;
        visible[count++] = { int(oam[i * 4 + 1]) - 8, i, sy, oam[i * 4 + 2], oam[i * 4 + 3] };
    }
    // Manual insertion sort: draw lower-X last (highest priority); ties → lower OAM idx last
    for (int i = 1; i < count; ++i) {
        Spr key = visible[i];
        int j = i - 1;
        while (j >= 0 && (visible[j].x < key.x ||
                          (visible[j].x == key.x && visible[j].oamIdx < key.oamIdx))) {
            visible[j + 1] = visible[j];
            --j;
        }
        visible[j + 1] = key;
    }

    const uint8_t* vram = memory.getVRAM();
    uint32_t row = ly * SCREEN_WIDTH;

    for (int s = 0; s < count; ++s) {
        const Spr& sp = visible[s];
        bool flipY = (sp.attr & 0x40) != 0;
        bool flipX = (sp.attr & 0x20) != 0;
        bool bgPrio = (sp.attr & 0x80) != 0;
        uint8_t palette = (sp.attr & 0x10) ? obp1 : obp0;

        int spriteY = int(ly) - (int(sp.y) - 16);
        if (flipY) spriteY = spriteH - 1 - spriteY;

        uint8_t tile = sp.tile;
        if (tall) tile &= 0xFE;
        uint16_t tileAddr = tile * 16 + spriteY * 2;
        uint8_t lo = vram[tileAddr];
        uint8_t hi = vram[tileAddr + 1];

        for (int px = 0; px < 8; ++px) {
            int sx = sp.x + px;
            if (sx < 0 || sx >= SCREEN_WIDTH) continue;
            int bit = flipX ? px : (7 - px);
            uint8_t shade = uint8_t(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            if (shade == 0) continue;
            if (bgPrio && bgIdx[sx] != 0) continue;
            uint8_t color = (palette >> (shade * 2)) & 0x03;
            framebuffer[row + sx] = shades[color];
        }
    }
}
