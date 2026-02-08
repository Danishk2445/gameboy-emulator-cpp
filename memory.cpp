#include "memory.h"
#include "ppu.h"
#include "apu.h"
#include "cpu.h"
#include <fstream>
#include <cstring>

Memory::Memory() : 
    vram(0x2000, 0),
    wram(0x2000, 0),
    oam(0xA0, 0),
    io(0x80, 0),
    hram(0x7F, 0),
    ie(0),
    mbcType(0),
    romBank(1),
    ramBank(0),
    ramEnabled(false),
    rtcEnabled(false),
    rtcRegister(0),
    timerCounter(0),
    divCounter(0),
    dmaActive(false),
    dmaCycles(0),
    dmaSource(0),
    joypadButtons(0xFF),
    joypadDpad(0xFF)
{
    // Initialize I/O registers to default values
    io[0x00] = 0xCF;  // JOYP
    io[0x05] = 0x00;  // TIMA
    io[0x06] = 0x00;  // TMA
    io[0x07] = 0x00;  // TAC
    io[0x0F] = 0xE1;  // IF
    io[0x10] = 0x80;  // NR10
    io[0x11] = 0xBF;  // NR11
    io[0x12] = 0xF3;  // NR12
    io[0x14] = 0xBF;  // NR14
    io[0x16] = 0x3F;  // NR21
    io[0x17] = 0x00;  // NR22
    io[0x19] = 0xBF;  // NR24
    io[0x1A] = 0x7F;  // NR30
    io[0x1B] = 0xFF;  // NR31
    io[0x1C] = 0x9F;  // NR32
    io[0x1E] = 0xBF;  // NR34
    io[0x20] = 0xFF;  // NR41
    io[0x21] = 0x00;  // NR42
    io[0x22] = 0x00;  // NR43
    io[0x23] = 0xBF;  // NR44
    io[0x24] = 0x77;  // NR50
    io[0x25] = 0xF3;  // NR51
    io[0x26] = 0xF1;  // NR52
    io[0x40] = 0x91;  // LCDC
    io[0x42] = 0x00;  // SCY
    io[0x43] = 0x00;  // SCX
    io[0x45] = 0x00;  // LYC
    io[0x47] = 0xFC;  // BGP
    io[0x48] = 0xFF;  // OBP0
    io[0x49] = 0xFF;  // OBP1
    io[0x4A] = 0x00;  // WY
    io[0x4B] = 0x00;  // WX
}

bool Memory::loadROM(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    
    size_t size = file.tellg();
    file.seekg(0);
    
    rom.resize(size);
    file.read(reinterpret_cast<char*>(rom.data()), size);
    
    // Detect MBC type from cartridge header
    if (rom.size() >= 0x148) {
        uint8_t cartType = rom[0x147];
        switch (cartType) {
            case 0x00: mbcType = 0; break;  // ROM only
            case 0x01: case 0x02: case 0x03: mbcType = 1; break;  // MBC1
            case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: mbcType = 3; break;  // MBC3
            case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: mbcType = 5; break;  // MBC5
            default: mbcType = 1; break;  // Default to MBC1
        }
        
        // Initialize external RAM
        uint8_t ramSize = rom[0x149];
        switch (ramSize) {
            case 0x02: extRam.resize(0x2000); break;   // 8KB
            case 0x03: extRam.resize(0x8000); break;   // 32KB
            case 0x04: extRam.resize(0x20000); break;  // 128KB
            case 0x05: extRam.resize(0x10000); break;  // 64KB
            default: extRam.resize(0x2000); break;     // Default 8KB
        }
    }
    
    return true;
}

void Memory::setJoypadState(uint8_t buttons, uint8_t dpad) {
    joypadButtons = buttons;
    joypadDpad = dpad;
}

uint8_t Memory::readJoypad() const {
    uint8_t select = io[0x00] & 0x30;
    uint8_t result = 0xCF;  // Upper bits always 1
    
    if ((select & 0x20) == 0) {
        // Button keys selected (A, B, Select, Start)
        result = (result & 0xF0) | (joypadButtons & 0x0F);
    }
    if ((select & 0x10) == 0) {
        // D-pad selected
        result = (result & 0xF0) | (joypadDpad & 0x0F);
    }
    
    return result;
}

uint8_t Memory::read(uint16_t addr) const {
    // ROM Bank 0
    if (addr < 0x4000) {
        return rom[addr];
    }
    
    // ROM Bank N (switchable)
    if (addr < 0x8000) {
        size_t bankOffset = addr - 0x4000;
        size_t romAddr = (romBank * 0x4000) + bankOffset;
        if (romAddr < rom.size()) {
            return rom[romAddr];
        }
        return 0xFF;
    }
    
    // VRAM
    if (addr < 0xA000) {
        return vram[addr - 0x8000];
    }
    
    // External RAM
    if (addr < 0xC000) {
        if (ramEnabled && !extRam.empty()) {
            size_t ramAddr = (ramBank * 0x2000) + (addr - 0xA000);
            if (ramAddr < extRam.size()) {
                return extRam[ramAddr];
            }
        }
        return 0xFF;
    }
    
    // Work RAM
    if (addr < 0xE000) {
        return wram[addr - 0xC000];
    }
    
    // Echo RAM
    if (addr < 0xFE00) {
        return wram[addr - 0xE000];
    }
    
    // OAM
    if (addr < 0xFEA0) {
        return oam[addr - 0xFE00];
    }
    
    // Unusable
    if (addr < 0xFF00) {
        return 0xFF;
    }
    
    // I/O Registers
    if (addr < 0xFF80) {
        uint8_t reg = addr - 0xFF00;
        
        switch (reg) {
            case 0x00: return readJoypad();
            case 0x44: return ppu ? ppu->readLY() : 0;
            case 0x41: return ppu ? ppu->readSTAT() : 0;
            default: 
                if (reg >= 0x10 && reg <= 0x3F && apu) {
                    return apu->readRegister(reg);
                }
                return io[reg];
        }
    }
    
    // High RAM
    if (addr < 0xFFFF) {
        return hram[addr - 0xFF80];
    }
    
    // IE register
    return ie;
}

void Memory::write(uint16_t addr, uint8_t val) {
    // ROM area - MBC control
    if (addr < 0x8000) {
        handleMBCWrite(addr, val);
        return;
    }
    
    // VRAM
    if (addr < 0xA000) {
        vram[addr - 0x8000] = val;
        return;
    }
    
    // External RAM
    if (addr < 0xC000) {
        if (ramEnabled && !extRam.empty()) {
            size_t ramAddr = (ramBank * 0x2000) + (addr - 0xA000);
            if (ramAddr < extRam.size()) {
                extRam[ramAddr] = val;
            }
        }
        return;
    }
    
    // Work RAM
    if (addr < 0xE000) {
        wram[addr - 0xC000] = val;
        return;
    }
    
    // Echo RAM
    if (addr < 0xFE00) {
        wram[addr - 0xE000] = val;
        return;
    }
    
    // OAM
    if (addr < 0xFEA0) {
        oam[addr - 0xFE00] = val;
        return;
    }
    
    // Unusable
    if (addr < 0xFF00) {
        return;
    }
    
    // I/O Registers
    if (addr < 0xFF80) {
        uint8_t reg = addr - 0xFF00;
        
        switch (reg) {
            case 0x00:  // JOYP
                io[0x00] = (val & 0x30) | (io[0x00] & 0xCF);
                break;
            case 0x04:  // DIV - writing resets to 0
                io[0x04] = 0;
                divCounter = 0;
                break;
            case 0x40:  // LCDC
                if (ppu) ppu->writeLCDC(val);
                io[0x40] = val;
                break;
            case 0x41:  // STAT
                if (ppu) ppu->writeSTAT(val);
                io[0x41] = val;
                break;
            case 0x44:  // LY (read-only, but writing resets)
                if (ppu) ppu->writeLY(val);
                break;
            case 0x46:  // DMA
                dmaActive = true;
                dmaCycles = 0;
                dmaSource = val << 8;
                io[0x46] = val;
                break;
            default:
                if (reg >= 0x10 && reg <= 0x3F && apu) {
                    apu->writeRegister(reg, val);
                }
                io[reg] = val;
                break;
        }
        return;
    }
    
    // High RAM
    if (addr < 0xFFFF) {
        hram[addr - 0xFF80] = val;
        return;
    }
    
    // IE register
    ie = val;
}

void Memory::handleMBCWrite(uint16_t addr, uint8_t val) {
    switch (mbcType) {
        case 0:  // ROM only
            break;
            
        case 1:  // MBC1
            if (addr < 0x2000) {
                ramEnabled = (val & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                romBank = val & 0x1F;
                if (romBank == 0) romBank = 1;
            } else if (addr < 0x6000) {
                ramBank = val & 0x03;
            }
            break;
            
        case 3:  // MBC3
            if (addr < 0x2000) {
                ramEnabled = (val & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                romBank = val & 0x7F;
                if (romBank == 0) romBank = 1;
            } else if (addr < 0x6000) {
                if (val <= 0x03) {
                    ramBank = val;
                    rtcEnabled = false;
                } else if (val >= 0x08 && val <= 0x0C) {
                    rtcEnabled = true;
                    rtcRegister = val;
                }
            }
            break;
            
        case 5:  // MBC5
            if (addr < 0x2000) {
                ramEnabled = (val & 0x0F) == 0x0A;
            } else if (addr < 0x3000) {
                romBank = (romBank & 0x100) | val;
            } else if (addr < 0x4000) {
                romBank = (romBank & 0xFF) | ((val & 0x01) << 8);
            } else if (addr < 0x6000) {
                ramBank = val & 0x0F;
            }
            break;
    }
}

void Memory::updateTimer(int cycles) {
    // Update DIV register
    divCounter += cycles;
    while (divCounter >= 256) {
        divCounter -= 256;
        io[0x04]++;
    }
    
    // Check if timer is enabled
    uint8_t tac = io[0x07];
    if (!(tac & 0x04)) return;
    
    timerCounter += cycles;
    int freq = getTimerFrequency();
    
    while (timerCounter >= freq) {
        timerCounter -= freq;
        io[0x05]++;  // TIMA
        
        // Timer overflow
        if (io[0x05] == 0) {
            io[0x05] = io[0x06];  // Reset to TMA
            io[0x0F] |= INT_TIMER;  // Request timer interrupt
        }
    }
}

int Memory::getTimerFrequency() const {
    switch (io[0x07] & 0x03) {
        case 0: return 1024;  // 4096 Hz
        case 1: return 16;    // 262144 Hz
        case 2: return 64;    // 65536 Hz
        case 3: return 256;   // 16384 Hz
        default: return 1024;
    }
}

void Memory::updateDMA(int cycles) {
    if (!dmaActive) return;
    
    dmaCycles += cycles;
    
    // DMA takes 160 machine cycles (640 clocks)
    if (dmaCycles >= 640) {
        // Copy 160 bytes to OAM
        for (int i = 0; i < 160; i++) {
            oam[i] = read(dmaSource + i);
        }
        dmaActive = false;
    }
}
