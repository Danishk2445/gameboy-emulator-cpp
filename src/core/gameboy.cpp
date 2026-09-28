#include "gameboy.hpp"

#include <algorithm>
#include <cstring>

namespace gb {

namespace {
constexpr char kStateMagic[4] = {'G', 'B', 'S', 'S'};
constexpr u32 kStateVersion = 1;

void copy_title(char (&dst)[16], const std::string& title) {
    std::memset(dst, 0, sizeof(dst));
    std::memcpy(dst, title.data(), std::min(title.size(), sizeof(dst)));
}
}  // namespace

GameBoy::GameBoy()
    : ppu(irq), serial(irq), joypad(irq), timer(irq, apu, serial), bus(irq, ppu, apu, timer, joypad, serial),
      cpu(bus, irq) {}

void GameBoy::insert_cartridge(std::unique_ptr<Cartridge> cart) {
    cart_ = std::move(cart);
    bus.set_cartridge(cart_.get());
    reset();
}

void GameBoy::set_boot_rom(std::vector<u8> rom) {
    boot_rom_ = std::move(rom);
    bus.set_boot_rom(boot_rom_);
}

void GameBoy::reset() {
    bool boot = boot_rom_.size() >= 0x100;
    bool checksum_nonzero = cart_ && cart_->header().header_checksum != 0;
    irq.ie = 0;
    irq.flags = boot ? 0x00 : 0x01;
    bus.reset(boot);
    cpu.reset(!boot, checksum_nonzero);
    timer.reset(boot ? 0x0000 : 0xABC8);
    ppu.reset(!boot);
    apu.reset(!boot);
    serial.reset();
    joypad.reset(!boot);
    frame_count_ = 0;
    skip_breakpoint_once_ = false;
}

GameBoy::StopReason GameBoy::run_frame() {
    ppu.clear_frame_ready();
    bool first = true;
    while (!ppu.frame_ready()) {
        if (breakpoint_count_ && breakpoints_[cpu.pc] && !cpu.halted()) {
            if (!(first && skip_breakpoint_once_)) {
                skip_breakpoint_once_ = true;  // resuming continues past this breakpoint
                return StopReason::Breakpoint;
            }
        }
        first = false;
        skip_breakpoint_once_ = false;
        cpu.step();
    }
    ++frame_count_;
    return StopReason::Frame;
}

void GameBoy::step_instruction() {
    skip_breakpoint_once_ = false;
    // A halted CPU takes one M-cycle per step; keep going until it wakes (at most one frame).
    u32 budget = kCyclesPerFrame / 4;
    do {
        cpu.step();
        if (ppu.frame_ready()) {
            ppu.clear_frame_ready();
            ++frame_count_;
        }
    } while ((cpu.halted() || cpu.stopped()) && --budget);
}

void GameBoy::add_breakpoint(u16 addr) {
    if (!breakpoints_[addr]) ++breakpoint_count_;
    breakpoints_[addr] = true;
}

void GameBoy::remove_breakpoint(u16 addr) {
    if (breakpoints_[addr]) --breakpoint_count_;
    breakpoints_[addr] = false;
}

void GameBoy::serialize(StateIO& s) {
    cpu.serialize(s);
    s(irq);
    bus.serialize(s);
    timer.serialize(s);
    ppu.serialize(s);
    apu.serialize(s);
    serial.serialize(s);
    joypad.serialize(s);
    cart_->serialize(s);
    s(frame_count_);
}

std::vector<u8> GameBoy::save_state() {
    StateIO s = StateIO::writer();
    char magic[4];
    std::memcpy(magic, kStateMagic, 4);
    s(magic);
    u32 version = kStateVersion;
    s(version);
    u16 checksum = cart_->header().global_checksum;
    s(checksum);
    char title[16];
    copy_title(title, cart_->header().title);
    s(title);
    serialize(s);
    return s.data();
}

bool GameBoy::load_state(const std::vector<u8>& data, std::string& error) {
    if (!cart_) {
        error = "No cartridge loaded";
        return false;
    }
    // Validate the header before touching any state.
    StateIO header = StateIO::reader(data);
    char magic[4] = {};
    u32 version = 0;
    u16 checksum = 0;
    char title[16] = {};
    header(magic);
    header(version);
    header(checksum);
    header(title);
    if (!header.ok() || std::memcmp(magic, kStateMagic, 4) != 0) {
        error = "Not a save state file";
        return false;
    }
    if (version != kStateVersion) {
        error = "Save state version mismatch";
        return false;
    }
    char expected[16];
    copy_title(expected, cart_->header().title);
    if (checksum != cart_->header().global_checksum || std::memcmp(title, expected, sizeof(title)) != 0) {
        error = "Save state belongs to a different game";
        return false;
    }

    std::vector<u8> backup = save_state();
    StateIO s = StateIO::reader(data);
    s(magic);
    s(version);
    s(checksum);
    s(title);
    serialize(s);
    if (!s.ok() || !s.at_end()) {
        StateIO restore = StateIO::reader(std::move(backup));
        restore(magic);
        restore(version);
        restore(checksum);
        restore(title);
        serialize(restore);
        error = "Save state is corrupt or truncated";
        return false;
    }
    apu.samples().clear();
    return true;
}

}  // namespace gb
