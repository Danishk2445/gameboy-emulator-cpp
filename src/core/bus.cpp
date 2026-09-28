#include "bus.hpp"

#include "apu.hpp"
#include "cartridge.hpp"
#include "joypad.hpp"
#include "ppu.hpp"
#include "serial.hpp"
#include "timer.hpp"

namespace gb {

void Bus::reset(bool use_boot_rom) {
    wram_.fill(0);
    hram_.fill(0);
    boot_rom_active_ = use_boot_rom && boot_rom_.size() >= 0x100;
    dma_reg_ = 0xFF;
    dma_active_ = false;
    dma_pending_ = false;
    dma_delay_ = 0;
    dma_index_ = 0;
    dma_source_ = 0;
    dma_pending_source_ = 0;
    cycles_ = 0;
}

void Bus::tick() {
    cycles_ += 4;
    irq_.late = 0;
    timer_.tick();
    ppu_.tick();
    apu_.tick();
    if (cart_) cart_->tick(4);

    // OAM DMA: starts 2 M-cycles after the FF46 write, then copies one byte per M-cycle.
    // A restart keeps the old transfer running until the new one takes over.
    if (dma_pending_ && --dma_delay_ == 0) {
        dma_pending_ = false;
        dma_active_ = true;
        dma_index_ = 0;
        dma_source_ = dma_pending_source_;
    }
    if (dma_active_) {
        if (dma_index_ == 160) {
            dma_active_ = false;
        } else {
            ppu_.dma_write_oam(dma_index_, dma_source_read(static_cast<u16>(dma_source_ + dma_index_)));
            ++dma_index_;
        }
    }
}

u8 Bus::dma_source_read(u16 addr) const {
    if (addr >= 0xE000) addr = static_cast<u16>(addr - 0x2000);  // FE/FF pages hit echo RAM
    switch (addr >> 13) {
        case 0:
        case 1:
        case 2:
        case 3: return cart_ ? cart_->read_rom(addr) : 0xFF;
        case 4: return ppu_.vram_byte(addr);
        case 5: return cart_ ? cart_->read_ram(addr) : 0xFF;
        default: return wram_[addr & 0x1FFF];
    }
}

u8 Bus::peek(u16 addr) const {
    if (addr < 0x8000) {
        if (boot_rom_active_ && addr < 0x100) return boot_rom_[addr];
        return cart_ ? cart_->read_rom(addr) : 0xFF;
    }
    if (addr < 0xA000) return ppu_.cpu_read_vram(addr);
    if (addr < 0xC000) return cart_ ? cart_->read_ram(addr) : 0xFF;
    if (addr < 0xFE00) return wram_[addr & 0x1FFF];
    if (addr < 0xFEA0) return dma_active_ ? 0xFF : ppu_.cpu_read_oam(addr);
    if (addr < 0xFF00) return (dma_active_ || ppu_.oam_blocked()) ? 0xFF : 0x00;
    if (addr < 0xFF80) return read_io(addr);
    if (addr < 0xFFFF) return hram_[addr - 0xFF80];
    return irq_.ie;
}

u8 Bus::debug_read(u16 addr) const {
    if (addr >= 0x8000 && addr < 0xA000) return ppu_.vram_byte(addr);
    if (addr >= 0xFE00 && addr < 0xFEA0) return ppu_.oam()[addr - 0xFE00];
    if (addr >= 0xFEA0 && addr < 0xFF00) return 0x00;
    return peek(addr);
}

void Bus::poke(u16 addr, u8 value) {
    if (addr < 0x8000) {
        if (cart_) cart_->write_rom(addr, value);
    } else if (addr < 0xA000) {
        ppu_.cpu_write_vram(addr, value);
    } else if (addr < 0xC000) {
        if (cart_) cart_->write_ram(addr, value);
    } else if (addr < 0xFE00) {
        wram_[addr & 0x1FFF] = value;
    } else if (addr < 0xFEA0) {
        if (!dma_active_) ppu_.cpu_write_oam(addr, value);
    } else if (addr < 0xFF00) {
        // unusable area
    } else if (addr < 0xFF80) {
        write_io(addr, value);
    } else if (addr < 0xFFFF) {
        hram_[addr - 0xFF80] = value;
    } else {
        irq_.ie = value;
    }
}

u8 Bus::read_io(u16 addr) const {
    switch (addr) {
        case 0xFF00: return joypad_.read();
        case 0xFF01:
        case 0xFF02: return serial_.read(addr);
        case 0xFF04:
        case 0xFF05:
        case 0xFF06:
        case 0xFF07: return timer_.read(addr);
        case 0xFF0F: return 0xE0 | irq_.flags;
        case 0xFF46: return dma_reg_;
        default: break;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) return apu_.read(addr);
    if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu_.read_reg(addr);
    return 0xFF;
}

void Bus::write_io(u16 addr, u8 value) {
    switch (addr) {
        case 0xFF00: joypad_.write(value); return;
        case 0xFF01:
        case 0xFF02: serial_.write(addr, value); return;
        case 0xFF04:
        case 0xFF05:
        case 0xFF06:
        case 0xFF07: timer_.write(addr, value); return;
        case 0xFF0F: irq_.flags = value & 0x1F; return;
        case 0xFF46:
            dma_reg_ = value;
            dma_pending_ = true;
            dma_delay_ = 2;
            dma_pending_source_ = static_cast<u16>(value << 8);
            return;
        case 0xFF50:
            if (value & 1) boot_rom_active_ = false;
            return;
        default: break;
    }
    if (addr >= 0xFF10 && addr <= 0xFF3F) {
        apu_.write(addr, value);
    } else if (addr >= 0xFF40 && addr <= 0xFF4B) {
        ppu_.write_reg(addr, value);
    }
}

void Bus::serialize(StateIO& s) {
    s(wram_);
    s(hram_);
    s(boot_rom_active_);
    s(dma_reg_);
    s(dma_active_);
    s(dma_pending_);
    s(dma_delay_);
    s(dma_index_);
    s(dma_source_);
    s(dma_pending_source_);
    s(cycles_);
}

}  // namespace gb
