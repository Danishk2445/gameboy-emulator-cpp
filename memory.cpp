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
    divCounter = 0; prevDivCounter = 0;
    timerSignal = false; timaReloading = false; timaJustReloaded = false;
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
    W(divCounter);
    uint8_t timerFlags = (timerSignal ? 1 : 0) | (timaReloading ? 2 : 0);
    W(timerFlags);
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
    if (!R(divCounter)) return false;
    uint8_t timerFlags = 0;
    if (!R(timerFlags)) return false;
    timerSignal   = (timerFlags & 1) != 0;
    timaReloading = (timerFlags & 2) != 0;
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

// Which bit of the system counter TIMA watches, per TAC bits 0-1. Falling
// edges of that bit are what drive TIMA, so the periods it produces are the
// familiar 1024 / 16 / 64 / 256 T-cycles.
int Memory::timerBit() const {
    switch (io[0x07] & 0x03) {
        case 0: return 9;
        case 1: return 3;
        case 2: return 5;
        case 3: return 7;
    }
    return 9;
}

void Memory::tick(int cycles) {
    // Step everything one M-cycle at a time. The CPU usually calls this with a
    // single M-cycle anyway, but finish() can top up several at once, and the
    // PPU's OAM-scan row has to be attributed to the right M-cycle for the OAM
    // corruption bug to land on the row hardware would have been reading.
    for (int i = 0; i < cycles; i += 4) {
        updateTimer(4);
        updateDMA(4);
        if (ppu) ppu->step(4);
        if (apu) apu->step(4);
    }
}

uint16_t Memory::oamWord(int row, int word) const {
    size_t i = static_cast<size_t>(row) * 8 + static_cast<size_t>(word) * 2;
    return static_cast<uint16_t>(oam[i] | (oam[i + 1] << 8));
}

void Memory::setOamWord(int row, int word, uint16_t v) {
    size_t i = static_cast<size_t>(row) * 8 + static_cast<size_t>(word) * 2;
    oam[i]     = static_cast<uint8_t>(v);
    oam[i + 1] = static_cast<uint8_t>(v >> 8);
}

// Both single-access patterns mangle the row's first word using the row above
// it, then copy that row's remaining three words down verbatim. Row 0 is wired
// differently on hardware and never corrupts, which is why objects 0 and 1 are
// the traditional safe place to park sprites.
void Memory::oamCorruptWrite(int row) {
    if (row <= 0 || row >= 20) return;
    uint16_t a = oamWord(row, 0);
    uint16_t b = oamWord(row - 1, 0);
    uint16_t c = oamWord(row - 1, 2);
    setOamWord(row, 0, static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c));
    for (int w = 1; w < 4; ++w) setOamWord(row, w, oamWord(row - 1, w));
}

void Memory::oamCorruptRead(int row) {
    if (row <= 0 || row >= 20) return;
    uint16_t a = oamWord(row, 0);
    uint16_t b = oamWord(row - 1, 0);
    uint16_t c = oamWord(row - 1, 2);
    setOamWord(row, 0, static_cast<uint16_t>(b | (a & c)));
    for (int w = 1; w < 4; ++w) setOamWord(row, w, oamWord(row - 1, w));
}

// A read sharing its M-cycle with an increment/decrement puts both a read and a
// glitched write on the bus at once, which smears the preceding row across
// three rows before the ordinary read corruption is applied on top.
void Memory::oamCorruptReadWrite(int row) {
    if (row >= 4 && row <= 18) {
        uint16_t a = oamWord(row - 2, 0);
        uint16_t b = oamWord(row - 1, 0);
        uint16_t c = oamWord(row, 0);
        uint16_t d = oamWord(row - 1, 2);
        setOamWord(row - 1, 0,
                   static_cast<uint16_t>((b & (a | c | d)) | (a & c & d)));
        for (int w = 0; w < 4; ++w) {
            uint16_t v = oamWord(row - 1, w);
            setOamWord(row,     w, v);
            setOamWord(row - 2, w, v);
        }
    }
    oamCorruptRead(row);
}

void Memory::oamBugAccess(uint16_t addr, OamBugOp op) {
    if (addr < 0xFE00 || addr > 0xFEFF) return;
    if (!ppu || !ppu->oamScanActive()) return;
    int row = ppu->oamScanRow();
    switch (op) {
        case OamBugOp::Read:      oamCorruptRead(row);      break;
        case OamBugOp::Write:     oamCorruptWrite(row);     break;
        case OamBugOp::ReadWrite: oamCorruptReadWrite(row); break;
    }
}

void Memory::updateTimer(int cycles) {
    // One M-cycle at a time: the fastest TAC setting ticks TIMA every 16
    // T-cycles, so this never steps over an edge.
    for (int i = 0; i < cycles; i += 4) {
        timaJustReloaded = false;
        if (timaReloading) {
            io[0x05] = io[0x06];
            io[0x0F] |= INT_TIMER;
            timaReloading = false;
            timaJustReloaded = true;
        }
        prevDivCounter = divCounter;
        setDivCounter(static_cast<uint16_t>(divCounter + 4));
    }
}

void Memory::setDivCounter(uint16_t v) {
    divCounter = v;
    updateTimerEdge();
}

// TIMA is clocked by the falling edge of (selected counter bit AND timer
// enable). Recomputing this after every counter, DIV or TAC change is what
// gives the hardware's "spurious" increments for free.
void Memory::updateTimerEdge() {
    bool signal = (io[0x07] & 0x04) != 0 &&
                  ((divCounter >> timerBit()) & 1) != 0;
    if (timerSignal && !signal) incTIMA();
    timerSignal = signal;
}

void Memory::incTIMA() {
    if (io[0x05] == 0xFF) {
        io[0x05] = 0;           // reads as 0 until the reload one M-cycle later
        timaReloading = true;
    } else {
        io[0x05]++;
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
        if (reg == 0x04) return uint8_t(divCounter >> 8);
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
                // Any write clears the whole counter, which can drop the
                // watched bit and tick TIMA on the way past.
                setDivCounter(0);
                return;
            case 0x05:
                // Writing TIMA during the post-overflow window aborts the
                // pending TMA reload and its interrupt — but on the cycle the
                // reload itself lands, the write is dropped instead.
                if (!timaJustReloaded) {
                    io[0x05] = val;
                    timaReloading = false;
                }
                return;
            case 0x06:
                // A TMA write on the reload cycle is picked up by that reload.
                io[0x06] = val;
                if (timaJustReloaded) io[0x05] = val;
                return;
            case 0x07: {
                io[0x07] = val | 0xF8;
                // The write takes effect before the last T-cycle of this
                // M-cycle, so enabling the timer just as the watched bit falls
                // still produces the edge. Treat the signal as having been high
                // if it was high either before or after the step we just took.
                bool enabled = (io[0x07] & 0x04) != 0;
                bool sigPrev = enabled && ((prevDivCounter >> timerBit()) & 1);
                bool sigNow  = enabled && ((divCounter     >> timerBit()) & 1);
                if ((timerSignal || sigPrev) && !sigNow) incTIMA();
                timerSignal = sigNow;
                return;
            }
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
