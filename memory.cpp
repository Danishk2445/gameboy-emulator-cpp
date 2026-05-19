#include "memory.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <ostream>
#include <istream>

Memory::Memory()
    : vram(0x2000, 0),
      wram(0x2000, 0),
      oam(0xA0, 0),
      io(0x80, 0),
      hram(0x7F, 0) {
    io[0x00] = 0xCF;
    io[0x05] = 0x00;
    io[0x06] = 0x00;
    io[0x07] = 0x00;
    io[0x0F] = 0xE1;
    io[0x40] = 0x91;
    io[0x41] = 0x85;
    io[0x42] = 0x00;
    io[0x43] = 0x00;
    io[0x44] = 0x00;
    io[0x45] = 0x00;
    io[0x47] = 0xFC;
    io[0x48] = 0xFF;
    io[0x49] = 0xFF;
    io[0x4A] = 0x00;
    io[0x4B] = 0x00;
}

Memory::~Memory() {
    saveSRAM();
}

static bool mbcTypeHasBattery(uint8_t t) {
    switch (t) {
        case 0x03: case 0x06: case 0x09: case 0x0D:
        case 0x0F: case 0x10: case 0x13:
        case 0x1B: case 0x1E: case 0x22: case 0xFF:
            return true;
        default:
            return false;
    }
}

bool Memory::loadROM(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::cerr << "Cannot open ROM: " << path << '\n';
        return false;
    }
    auto size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    rom.assign(size, 0);
    if (!f.read(reinterpret_cast<char*>(rom.data()), size)) {
        std::cerr << "Failed to read ROM\n";
        return false;
    }
    if (rom.size() < 0x150) {
        std::cerr << "ROM too small\n";
        return false;
    }

    mbcType = rom[0x0147];
    uint8_t ramCode = rom[0x0149];
    size_t ramSize = 0;
    switch (ramCode) {
        case 0x01: ramSize = 0x800;   break;
        case 0x02: ramSize = 0x2000;  break;
        case 0x03: ramSize = 0x8000;  break;
        case 0x04: ramSize = 0x20000; break;
        case 0x05: ramSize = 0x10000; break;
        default:   ramSize = 0x2000;  break;
    }
    extRam.assign(ramSize, 0);

    loadedPath = path;
    hasBattery = mbcTypeHasBattery(mbcType);
    if (hasBattery) {
        size_t dot = path.find_last_of('.');
        savePath = (dot == std::string::npos) ? path + ".sav"
                                              : path.substr(0, dot) + ".sav";
        std::ifstream sf(savePath, std::ios::binary | std::ios::ate);
        if (sf) {
            auto sSize = static_cast<size_t>(sf.tellg());
            sf.seekg(0, std::ios::beg);
            size_t toRead = std::min(sSize, extRam.size());
            if (sf.read(reinterpret_cast<char*>(extRam.data()), toRead)) {
                std::cout << "Loaded save: " << savePath << " (" << toRead << " bytes)\n";
            }
        }
    }

    std::cout << "Loaded ROM: " << path << " (" << rom.size()
              << " bytes, MBC type " << static_cast<int>(mbcType) << ")\n";
    return true;
}

void Memory::unloadROM() {
    saveSRAM();
    rom.clear();
    extRam.clear();
    std::fill(vram.begin(), vram.end(), 0);
    std::fill(wram.begin(), wram.end(), 0);
    std::fill(oam.begin(), oam.end(), 0);
    std::fill(hram.begin(), hram.end(), 0);
    // io & ie are reset to power-on defaults by reinitializing the relevant ones
    for (auto& b : io) b = 0;
    io[0x00] = 0xCF;
    io[0x0F] = 0xE1;
    io[0x40] = 0x91;
    io[0x41] = 0x85;
    io[0x47] = 0xFC;
    io[0x48] = 0xFF;
    io[0x49] = 0xFF;
    ie = 0;
    mbcType = 0; romBank = 1; ramBank = 0;
    ramEnabled = false; mbc1RamMode = false; rtcRegister = 0;
    hasBattery = false; sramDirty = false;
    savePath.clear(); loadedPath.clear();
    divCounter = 0; timerCounter = 0;
    dmaActive = false; dmaCycles = 0; dmaSource = 0;
    joypadButtons = 0x0F; joypadDpad = 0x0F;
}

std::string Memory::romTitle() const {
    if (rom.size() < 0x144) return "";
    std::string t;
    for (int i = 0x134; i < 0x144; ++i) {
        char c = static_cast<char>(rom[i]);
        if (c == 0) break;
        if (c >= 32 && c < 127) t += c;
    }
    return t;
}

void Memory::saveState(std::ostream& out) const {
    auto W = [&](const auto& x) {
        out.write(reinterpret_cast<const char*>(&x), sizeof(x));
    };
    out.write(reinterpret_cast<const char*>(vram.data()),
              static_cast<std::streamsize>(vram.size()));
    out.write(reinterpret_cast<const char*>(wram.data()),
              static_cast<std::streamsize>(wram.size()));
    out.write(reinterpret_cast<const char*>(oam.data()),
              static_cast<std::streamsize>(oam.size()));
    out.write(reinterpret_cast<const char*>(io.data()),
              static_cast<std::streamsize>(io.size()));
    out.write(reinterpret_cast<const char*>(hram.data()),
              static_cast<std::streamsize>(hram.size()));
    W(ie);
    W(mbcType); W(romBank); W(ramBank);
    uint8_t flags = (ramEnabled ? 1 : 0) | (mbc1RamMode ? 2 : 0);
    W(flags);
    W(rtcRegister);
    W(divCounter); W(timerCounter);
    uint8_t dma = dmaActive ? 1 : 0;
    W(dma); W(dmaCycles); W(dmaSource);
    W(joypadButtons); W(joypadDpad);
    uint64_t sz = extRam.size();
    W(sz);
    if (sz) {
        out.write(reinterpret_cast<const char*>(extRam.data()),
                  static_cast<std::streamsize>(sz));
    }
}

bool Memory::loadState(std::istream& in) {
    auto R = [&](auto& x) {
        return static_cast<bool>(
            in.read(reinterpret_cast<char*>(&x), sizeof(x)));
    };
    if (!in.read(reinterpret_cast<char*>(vram.data()),
                 static_cast<std::streamsize>(vram.size()))) return false;
    if (!in.read(reinterpret_cast<char*>(wram.data()),
                 static_cast<std::streamsize>(wram.size()))) return false;
    if (!in.read(reinterpret_cast<char*>(oam.data()),
                 static_cast<std::streamsize>(oam.size()))) return false;
    if (!in.read(reinterpret_cast<char*>(io.data()),
                 static_cast<std::streamsize>(io.size()))) return false;
    if (!in.read(reinterpret_cast<char*>(hram.data()),
                 static_cast<std::streamsize>(hram.size()))) return false;
    if (!R(ie)) return false;
    if (!R(mbcType) || !R(romBank) || !R(ramBank)) return false;
    uint8_t flags = 0;
    if (!R(flags)) return false;
    ramEnabled  = (flags & 1) != 0;
    mbc1RamMode = (flags & 2) != 0;
    if (!R(rtcRegister)) return false;
    if (!R(divCounter) || !R(timerCounter)) return false;
    uint8_t dma = 0;
    if (!R(dma) || !R(dmaCycles) || !R(dmaSource)) return false;
    dmaActive = dma != 0;
    if (!R(joypadButtons) || !R(joypadDpad)) return false;
    uint64_t sz = 0;
    if (!R(sz)) return false;
    if (sz != extRam.size()) {
        // Resize to match — should normally match ROM's RAM size.
        extRam.assign(static_cast<size_t>(sz), 0);
    }
    if (sz) {
        if (!in.read(reinterpret_cast<char*>(extRam.data()),
                     static_cast<std::streamsize>(sz))) return false;
        if (hasBattery) sramDirty = true;
    }
    return true;
}

bool Memory::saveSRAM() const {
    if (!hasBattery || savePath.empty() || extRam.empty()) return false;
    if (!sramDirty) return false;
    std::ofstream sf(savePath, std::ios::binary | std::ios::trunc);
    if (!sf) {
        std::cerr << "Cannot write save: " << savePath << '\n';
        return false;
    }
    sf.write(reinterpret_cast<const char*>(extRam.data()),
             static_cast<std::streamsize>(extRam.size()));
    if (!sf) {
        std::cerr << "Failed writing save: " << savePath << '\n';
        return false;
    }
    sramDirty = false;
    std::cout << "Saved: " << savePath << " (" << extRam.size() << " bytes)\n";
    return true;
}

int Memory::getTimerFrequency() const {
    switch (io[0x07] & 0x03) {
        case 0: return 1024;
        case 1: return 16;
        case 2: return 64;
        case 3: return 256;
    }
    return 1024;
}

void Memory::updateTimer(int cycles) {
    divCounter += cycles;
    while (divCounter >= 256) {
        divCounter -= 256;
        io[0x04]++;
    }
    if (io[0x07] & 0x04) {
        timerCounter += cycles;
        int freq = getTimerFrequency();
        while (timerCounter >= freq) {
            timerCounter -= freq;
            if (io[0x05] == 0xFF) {
                io[0x05] = io[0x06];
                io[0x0F] |= INT_TIMER;
            } else {
                io[0x05]++;
            }
        }
    }
}

void Memory::updateDMA(int cycles) {
    if (!dmaActive) return;
    dmaCycles += cycles;
    if (dmaCycles >= 640) {
        for (int i = 0; i < 0xA0; ++i) {
            oam[i] = read(static_cast<uint16_t>(dmaSource + i));
        }
        dmaActive = false;
        dmaCycles = 0;
    }
}

void Memory::setJoypadState(uint8_t buttons, uint8_t dpad) {
    uint8_t oldButtons = joypadButtons;
    uint8_t oldDpad    = joypadDpad;
    joypadButtons = buttons & 0x0F;
    joypadDpad    = dpad & 0x0F;
    if (((oldButtons & ~joypadButtons) | (oldDpad & ~joypadDpad)) != 0) {
        io[0x0F] |= INT_JOYPAD;
    }
}

uint8_t Memory::readJoypad() const {
    uint8_t sel = io[0x00];
    uint8_t lo = 0x0F;
    if ((sel & 0x10) == 0) lo &= joypadDpad;
    if ((sel & 0x20) == 0) lo &= joypadButtons;
    return (sel & 0xF0) | lo | 0xC0;
}

void Memory::handleMBCWrite(uint16_t addr, uint8_t val) {
    if (mbcType == 0x00) return;

    if (mbcType >= 0x01 && mbcType <= 0x03) {
        if (addr < 0x2000) {
            bool newEnabled = ((val & 0x0F) == 0x0A);
            if (ramEnabled && !newEnabled && sramDirty) saveSRAM();
            ramEnabled = newEnabled;
        } else if (addr < 0x4000) {
            int lo = val & 0x1F;
            if (lo == 0) lo = 1;
            romBank = (romBank & 0x60) | lo;
        } else if (addr < 0x6000) {
            int hi = val & 0x03;
            if (mbc1RamMode) {
                ramBank = hi;
            } else {
                romBank = (romBank & 0x1F) | (hi << 5);
            }
        } else {
            mbc1RamMode = (val & 0x01) != 0;
        }
        return;
    }

    if (mbcType >= 0x0F && mbcType <= 0x13) {
        if (addr < 0x2000) {
            bool newEnabled = ((val & 0x0F) == 0x0A);
            if (ramEnabled && !newEnabled && sramDirty) saveSRAM();
            ramEnabled = newEnabled;
        } else if (addr < 0x4000) {
            int b = val & 0x7F;
            if (b == 0) b = 1;
            romBank = b;
        } else if (addr < 0x6000) {
            if (val <= 0x03) {
                ramBank = val;
                rtcRegister = 0;
            } else if (val >= 0x08 && val <= 0x0C) {
                rtcRegister = val;
            }
        } else {
            // RTC latch — ignored (clock not modeled)
        }
        return;
    }
}

uint8_t Memory::read(uint16_t addr) const {
    if (addr < 0x4000) {
        return rom[addr];
    }
    if (addr < 0x8000) {
        size_t off = static_cast<size_t>(romBank) * 0x4000 + (addr - 0x4000);
        if (off < rom.size()) return rom[off];
        return 0xFF;
    }
    if (addr < 0xA000) {
        return vram[addr - 0x8000];
    }
    if (addr < 0xC000) {
        if (!ramEnabled || extRam.empty()) return 0xFF;
        if (mbcType >= 0x0F && mbcType <= 0x13 && rtcRegister != 0) {
            return 0x00;
        }
        size_t off = static_cast<size_t>(ramBank) * 0x2000 + (addr - 0xA000);
        if (off < extRam.size()) return extRam[off];
        return 0xFF;
    }
    if (addr < 0xE000) {
        return wram[addr - 0xC000];
    }
    if (addr < 0xFE00) {
        return wram[addr - 0xE000];
    }
    if (addr < 0xFEA0) {
        return oam[addr - 0xFE00];
    }
    if (addr < 0xFF00) {
        return 0xFF;
    }
    if (addr < 0xFF80) {
        uint8_t reg = addr & 0x7F;
        if (reg == 0x00) return readJoypad();
        if (reg == 0x41) {
            return ppu ? ppu->readSTAT() : io[0x41];
        }
        if (reg == 0x44) {
            return ppu ? ppu->readLY() : io[0x44];
        }
        if (reg >= 0x10 && reg <= 0x3F) {
            return apu ? apu->readRegister(reg) : 0xFF;
        }
        return io[reg];
    }
    if (addr < 0xFFFF) {
        return hram[addr - 0xFF80];
    }
    return ie;
}

void Memory::write(uint16_t addr, uint8_t val) {
    if (addr < 0x8000) {
        handleMBCWrite(addr, val);
        return;
    }
    if (addr < 0xA000) {
        vram[addr - 0x8000] = val;
        return;
    }
    if (addr < 0xC000) {
        if (!ramEnabled || extRam.empty()) return;
        if (mbcType >= 0x0F && mbcType <= 0x13 && rtcRegister != 0) return;
        size_t off = static_cast<size_t>(ramBank) * 0x2000 + (addr - 0xA000);
        if (off < extRam.size()) {
            if (extRam[off] != val) {
                extRam[off] = val;
                if (hasBattery) sramDirty = true;
            }
        }
        return;
    }
    if (addr < 0xE000) {
        wram[addr - 0xC000] = val;
        return;
    }
    if (addr < 0xFE00) {
        wram[addr - 0xE000] = val;
        return;
    }
    if (addr < 0xFEA0) {
        oam[addr - 0xFE00] = val;
        return;
    }
    if (addr < 0xFF00) {
        return;
    }
    if (addr < 0xFF80) {
        uint8_t reg = addr & 0x7F;
        switch (reg) {
            case 0x00:
                io[0x00] = (io[0x00] & 0x0F) | (val & 0x30);
                return;
            case 0x04:
                io[0x04] = 0;
                divCounter = 0;
                return;
            case 0x07:
                if ((io[0x07] & 0x03) != (val & 0x03)) timerCounter = 0;
                io[0x07] = val | 0xF8;
                return;
            case 0x0F:
                io[0x0F] = val | 0xE0;
                return;
            case 0x40:
                if (ppu) ppu->writeLCDC(val);
                io[0x40] = val;
                return;
            case 0x41:
                if (ppu) ppu->writeSTAT(val);
                io[0x41] = val;
                return;
            case 0x44:
                if (ppu) ppu->writeLY(val);
                io[0x44] = 0;
                return;
            case 0x46: {
                io[0x46] = val;
                dmaActive = true;
                dmaCycles = 0;
                dmaSource = static_cast<uint16_t>(val) << 8;
                return;
            }
            default:
                if (reg >= 0x10 && reg <= 0x3F) {
                    if (apu) apu->writeRegister(reg, val);
                    return;
                }
                io[reg] = val;
                return;
        }
    }
    if (addr < 0xFFFF) {
        hram[addr - 0xFF80] = val;
        return;
    }
    ie = val;
}
