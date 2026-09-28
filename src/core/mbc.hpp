#pragma once

#include <memory>
#include <vector>

#include "common.hpp"
#include "state.hpp"

namespace gb {

// Memory bank controller. The ROM vector is padded to a power-of-two number
// of 16 KiB banks so bank numbers can simply be masked.
class Mbc {
public:
    Mbc(const std::vector<u8>& rom, std::vector<u8>& ram) : rom_(rom), ram_(ram) {}
    virtual ~Mbc() = default;

    virtual u8 read_rom(u16 addr) const = 0;         // 0000-7FFF
    virtual void write_rom(u16 addr, u8 value) = 0;  // MBC control registers
    virtual u8 read_ram(u16 addr) const = 0;         // A000-BFFF
    virtual void write_ram(u16 addr, u8 value) = 0;
    virtual void tick(u32 /*cycles*/) {}
    virtual void serialize(StateIO& s) = 0;

    // MBC3 real-time clock, stored as a 48-byte footer after the battery RAM.
    virtual bool has_rtc() const { return false; }
    virtual void save_rtc(std::vector<u8>& /*out*/) const {}
    virtual void load_rtc(const u8* /*data*/, size_t /*size*/) {}

    bool ram_dirty = false;

protected:
    u8 rom_byte(u32 bank, u16 offset) const {
        return rom_[((bank & (rom_banks() - 1)) << 14) | (offset & 0x3FFF)];
    }
    u32 rom_banks() const { return static_cast<u32>(rom_.size() >> 14); }
    u8 ram_byte(u32 bank, u16 addr) const {
        if (ram_.empty()) return 0xFF;
        return ram_[(bank * 0x2000u + (addr & 0x1FFFu)) % ram_.size()];
    }
    void set_ram_byte(u32 bank, u16 addr, u8 value) {
        if (ram_.empty()) return;
        u8& slot = ram_[(bank * 0x2000u + (addr & 0x1FFFu)) % ram_.size()];
        if (slot != value) {
            slot = value;
            ram_dirty = true;
        }
    }

    const std::vector<u8>& rom_;
    std::vector<u8>& ram_;
};

enum class MbcKind { None, Mbc1, Mbc2, Mbc3, Mbc5, Unsupported };

struct CartTypeInfo {
    MbcKind kind;
    bool ram;
    bool battery;
    bool rtc;
    bool rumble;
    const char* name;
};

CartTypeInfo cart_type_info(u8 type);

std::unique_ptr<Mbc> create_mbc(const CartTypeInfo& info, const std::vector<u8>& rom, std::vector<u8>& ram,
                                bool mbc1_multicart);

}  // namespace gb
