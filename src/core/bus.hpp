#pragma once

#include <array>
#include <utility>
#include <vector>

#include "common.hpp"
#include "state.hpp"

namespace gb {

class Apu;
class Cartridge;
class Joypad;
class Ppu;
class Serial;
class Timer;

// Memory map and IO dispatch. Every CPU access through read()/write() first
// advances the rest of the machine by one M-cycle.
class Bus {
public:
    Bus(Interrupts& irq, Ppu& ppu, Apu& apu, Timer& timer, Joypad& joypad, Serial& serial)
        : irq_(irq), ppu_(ppu), apu_(apu), timer_(timer), joypad_(joypad), serial_(serial) {}

    void set_cartridge(Cartridge* cart) { cart_ = cart; }
    void set_boot_rom(std::vector<u8> rom) { boot_rom_ = std::move(rom); }
    bool boot_rom_active() const { return boot_rom_active_; }
    void reset(bool use_boot_rom);

    u8 read(u16 addr) {
        tick();
        return peek(addr);
    }
    void write(u16 addr, u8 value) {
        tick();
        poke(addr, value);
    }
    void tick();  // advance timer, PPU, APU, DMA and cartridge RTC by one M-cycle

    u8 peek(u16 addr) const;  // CPU-visible value, no time passes
    void poke(u16 addr, u8 value);
    u8 debug_read(u16 addr) const;  // ignores PPU/DMA access locks (debugger, test harness)

    u64 cycles() const { return cycles_; }  // T-cycles since reset
    bool dma_active() const { return dma_active_; }

    Joypad& joypad() { return joypad_; }
    Timer& timer() { return timer_; }

    void serialize(StateIO& s);

private:
    u8 read_io(u16 addr) const;
    void write_io(u16 addr, u8 value);
    u8 dma_source_read(u16 addr) const;

    Interrupts& irq_;
    Ppu& ppu_;
    Apu& apu_;
    Timer& timer_;
    Joypad& joypad_;
    Serial& serial_;
    Cartridge* cart_ = nullptr;

    std::array<u8, 0x2000> wram_{};
    std::array<u8, 0x7F> hram_{};
    std::vector<u8> boot_rom_;
    bool boot_rom_active_ = false;

    u8 dma_reg_ = 0xFF;
    bool dma_active_ = false;
    bool dma_pending_ = false;
    u8 dma_delay_ = 0;
    u8 dma_index_ = 0;
    u16 dma_source_ = 0;
    u16 dma_pending_source_ = 0;

    u64 cycles_ = 0;
};

}  // namespace gb
