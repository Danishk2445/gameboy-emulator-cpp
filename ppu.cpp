#include "ppu.h"
#include "memory.h"
#include "cpu.h"

PPU::PPU(Memory& mem) : memory(mem) {
    reset();
}

void PPU::reset() {
    lcdc = 0x91;
    stat = 0x00;
    scy = 0;
    scx = 0;
    ly = 0;
    lyc = 0;
    wy = 0;
    wx = 0;
    bgp = 0xFC;
    obp0 = 0xFF;
    obp1 = 0xFF;
    
    scanlineCycles = 0;
    mode = 2;  // Start in OAM mode
    frameReady = false;
    windowLine = 0;
    
    framebuffer.fill(colors[0]);
}

void PPU::writeLCDC(uint8_t val) {
    bool wasEnabled = (lcdc & 0x80) != 0;
    bool isEnabled = (val & 0x80) != 0;
    
    if (wasEnabled && !isEnabled) {
        // LCD disabled
        ly = 0;
        scanlineCycles = 0;
        setMode(0);
    }
    
    lcdc = val;
}

void PPU::writeSTAT(uint8_t val) {
    // Lower 3 bits are read-only
    stat = (val & 0x78) | (stat & 0x07);
}

void PPU::writeLY(uint8_t) {
    ly = 0;
}

uint8_t PPU::readSTAT() const {
    return stat | 0x80;  // Bit 7 always 1
}

void PPU::setMode(int newMode) {
    mode = newMode;
    stat = (stat & 0xFC) | (mode & 0x03);
    checkSTATInterrupt();
}

void PPU::checkSTATInterrupt() {
    bool interrupt = false;
    
    // Mode interrupts
    if ((stat & 0x20) && mode == 2) interrupt = true;  // OAM interrupt
    if ((stat & 0x10) && mode == 1) interrupt = true;  // VBlank interrupt
    if ((stat & 0x08) && mode == 0) interrupt = true;  // HBlank interrupt
    
    // LYC=LY interrupt
    if ((stat & 0x40) && (stat & 0x04)) interrupt = true;
    
    if (interrupt) {
        memory.setIF(memory.getIF() | INT_STAT);
    }
}

void PPU::checkLYC() {
    if (ly == lyc) {
        stat |= 0x04;  // Set coincidence flag
    } else {
        stat &= ~0x04;
    }
    checkSTATInterrupt();
}

void PPU::step(int cycles) {
    // Update registers from memory
    scy = memory.readIO(0x42);
    scx = memory.readIO(0x43);
    lyc = memory.readIO(0x45);
    bgp = memory.readIO(0x47);
    obp0 = memory.readIO(0x48);
    obp1 = memory.readIO(0x49);
    wy = memory.readIO(0x4A);
    wx = memory.readIO(0x4B);
    lcdc = memory.readIO(0x40);
    
    // LCD disabled
    if (!(lcdc & 0x80)) {
        return;
    }
    
    scanlineCycles += cycles;
    
    switch (mode) {
        case 2:  // OAM Search (80 cycles)
            if (scanlineCycles >= 80) {
                scanlineCycles -= 80;
                setMode(3);
            }
            break;
            
        case 3:  // Pixel Transfer (172 cycles, variable)
            if (scanlineCycles >= 172) {
                scanlineCycles -= 172;
                
                // Render scanline
                renderScanline();
                
                setMode(0);
            }
            break;
            
        case 0:  // HBlank (204 cycles)
            if (scanlineCycles >= 204) {
                scanlineCycles -= 204;
                ly++;
                
                if (ly == 144) {
                    // Enter VBlank
                    setMode(1);
                    memory.setIF(memory.getIF() | INT_VBLANK);
                    frameReady = true;
                    windowLine = 0;
                } else {
                    setMode(2);
                }
                
                memory.writeIO(0x44, ly);
                checkLYC();
            }
            break;
            
        case 1:  // VBlank (10 lines, 456 cycles each)
            if (scanlineCycles >= 456) {
                scanlineCycles -= 456;
                ly++;
                
                if (ly > 153) {
                    ly = 0;
                    setMode(2);
                }
                
                memory.writeIO(0x44, ly);
                checkLYC();
            }
            break;
    }
}

void PPU::renderScanline() {
    if (ly >= SCREEN_HEIGHT) return;
    
    std::array<uint8_t, 160> bgPriority;
    bgPriority.fill(0);
    
    // Render background
    if (lcdc & 0x01) {
        renderBackground(bgPriority);
    } else {
        // Background disabled - fill with color 0
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            framebuffer[ly * SCREEN_WIDTH + x] = colors[0];
        }
    }
    
    // Render window
    if ((lcdc & 0x20) && (lcdc & 0x01)) {
        renderWindow(bgPriority);
    }
    
    // Render sprites
    if (lcdc & 0x02) {
        renderSprites(bgPriority);
    }
}

void PPU::renderBackground(std::array<uint8_t, 160>& bgPriority) {
    const uint8_t* vram = memory.getVRAM();
    
    // Tile map address
    uint16_t tileMapBase = (lcdc & 0x08) ? 0x1C00 : 0x1800;
    
    // Tile data address and addressing mode
    bool signedMode = !(lcdc & 0x10);
    uint16_t tileDataBase = signedMode ? 0x1000 : 0x0000;
    
    uint8_t yPos = ly + scy;
    uint16_t tileRow = (yPos / 8) * 32;
    
    for (int pixel = 0; pixel < SCREEN_WIDTH; pixel++) {
        uint8_t xPos = pixel + scx;
        uint16_t tileCol = xPos / 8;
        
        // Get tile number
        uint16_t tileAddr = tileMapBase + tileRow + tileCol;
        uint8_t tileNum = vram[tileAddr];
        
        // Get tile data address
        uint16_t tileDataAddr;
        if (signedMode) {
            tileDataAddr = tileDataBase + ((int8_t)tileNum * 16);
        } else {
            tileDataAddr = tileDataBase + (tileNum * 16);
        }
        
        // Get pixel within tile
        uint8_t line = (yPos % 8) * 2;
        uint8_t lo = vram[tileDataAddr + line];
        uint8_t hi = vram[tileDataAddr + line + 1];
        
        int bit = 7 - (xPos % 8);
        uint8_t colorNum = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
        
        // Apply palette
        uint8_t color = (bgp >> (colorNum * 2)) & 0x03;
        
        framebuffer[ly * SCREEN_WIDTH + pixel] = colors[color];
        bgPriority[pixel] = colorNum;
    }
}

void PPU::renderWindow(std::array<uint8_t, 160>& bgPriority) {
    if (wy > ly) return;
    if (wx > 166) return;
    
    const uint8_t* vram = memory.getVRAM();
    
    // Window tile map
    uint16_t tileMapBase = (lcdc & 0x40) ? 0x1C00 : 0x1800;
    
    // Tile data addressing
    bool signedMode = !(lcdc & 0x10);
    uint16_t tileDataBase = signedMode ? 0x1000 : 0x0000;
    
    uint16_t tileRow = (windowLine / 8) * 32;
    bool windowVisible = false;
    
    for (int pixel = 0; pixel < SCREEN_WIDTH; pixel++) {
        int windowX = pixel - (wx - 7);
        if (windowX < 0) continue;
        
        windowVisible = true;
        uint16_t tileCol = windowX / 8;
        
        uint16_t tileAddr = tileMapBase + tileRow + tileCol;
        uint8_t tileNum = vram[tileAddr];
        
        uint16_t tileDataAddr;
        if (signedMode) {
            tileDataAddr = tileDataBase + ((int8_t)tileNum * 16);
        } else {
            tileDataAddr = tileDataBase + (tileNum * 16);
        }
        
        uint8_t line = (windowLine % 8) * 2;
        uint8_t lo = vram[tileDataAddr + line];
        uint8_t hi = vram[tileDataAddr + line + 1];
        
        int bit = 7 - (windowX % 8);
        uint8_t colorNum = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
        
        uint8_t color = (bgp >> (colorNum * 2)) & 0x03;
        
        framebuffer[ly * SCREEN_WIDTH + pixel] = colors[color];
        bgPriority[pixel] = colorNum;
    }
    
    if (windowVisible) {
        windowLine++;
    }
}

void PPU::renderSprites(const std::array<uint8_t, 160>& bgPriority) {
    const uint8_t* vram = memory.getVRAM();
    const uint8_t* oam = memory.getOAM();
    
    int spriteHeight = (lcdc & 0x04) ? 16 : 8;
    int spritesOnLine = 0;
    
    // Sprites with lower OAM addresses have priority
    for (int sprite = 0; sprite < 40 && spritesOnLine < 10; sprite++) {
        uint8_t yPos = oam[sprite * 4] - 16;
        uint8_t xPos = oam[sprite * 4 + 1] - 8;
        uint8_t tileNum = oam[sprite * 4 + 2];
        uint8_t attrs = oam[sprite * 4 + 3];
        
        // Check if sprite is on this scanline
        if (ly < yPos || ly >= yPos + spriteHeight) continue;
        
        spritesOnLine++;
        
        bool priority = (attrs & 0x80) != 0;
        bool yFlip = (attrs & 0x40) != 0;
        bool xFlip = (attrs & 0x20) != 0;
        uint8_t palette = (attrs & 0x10) ? obp1 : obp0;
        
        // For 8x16 sprites, mask bit 0 of tile number
        if (spriteHeight == 16) {
            tileNum &= 0xFE;
        }
        
        int line = ly - yPos;
        if (yFlip) {
            line = (spriteHeight - 1) - line;
        }
        
        uint16_t tileDataAddr = tileNum * 16 + line * 2;
        uint8_t lo = vram[tileDataAddr];
        uint8_t hi = vram[tileDataAddr + 1];
        
        for (int pixel = 0; pixel < 8; pixel++) {
            int screenX = xPos + pixel;
            if (screenX < 0 || screenX >= SCREEN_WIDTH) continue;
            
            int bit = xFlip ? pixel : (7 - pixel);
            uint8_t colorNum = ((hi >> bit) & 1) << 1 | ((lo >> bit) & 1);
            
            // Color 0 is transparent
            if (colorNum == 0) continue;
            
            // BG priority: sprite behind BG color 1-3
            if (priority && bgPriority[screenX] != 0) continue;
            
            uint8_t color = (palette >> (colorNum * 2)) & 0x03;
            framebuffer[ly * SCREEN_WIDTH + screenX] = colors[color];
        }
    }
}
