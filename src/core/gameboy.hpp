#pragma once

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include "apu.hpp"
#include "bus.hpp"
#include "cartridge.hpp"
#include "common.hpp"
#include "cpu.hpp"
#include "joypad.hpp"
#include "ppu.hpp"
#include "serial.hpp"
#include "timer.hpp"

namespace gb {

class GameBoy {
public:
    enum class StopReason { Frame, Breakpoint };

    GameBoy();

    // Replaces the cartridge and resets the machine.
    void insert_cartridge(std::unique_ptr<Cartridge> cart);
    Cartridge* cartridge() { return cart_.get(); }
    const Cartridge* cartridge() const { return cart_.get(); }
    bool has_cartridge() const { return cart_ != nullptr; }

    // Optional 256-byte DMG boot ROM; used on the next reset().
    void set_boot_rom(std::vector<u8> rom);
    void reset();

    // Runs until the PPU finishes a frame (or a breakpoint is reached).
    StopReason run_frame();
    void step_instruction();

    const Ppu::Framebuffer& framebuffer() const { return ppu.framebuffer(); }
    void set_button(Button button, bool pressed) { joypad.set_button(button, pressed); }
    u64 frame_count() const { return frame_count_; }

    void add_breakpoint(u16 addr);
    void remove_breakpoint(u16 addr);
    bool has_breakpoint(u16 addr) const { return breakpoints_[addr]; }

    std::vector<u8> save_state();
    bool load_state(const std::vector<u8>& data, std::string& error);

    // Components, declared in construction order.
    Interrupts irq;
    Ppu ppu;
    Apu apu;
    Serial serial;
    Joypad joypad;
    Timer timer;
    Bus bus;
    Cpu cpu;

private:
    void serialize(StateIO& s);

    std::unique_ptr<Cartridge> cart_;
    std::vector<u8> boot_rom_;
    u64 frame_count_ = 0;
    std::bitset<0x10000> breakpoints_;
    size_t breakpoint_count_ = 0;
    bool skip_breakpoint_once_ = false;
};

}  // namespace gb
