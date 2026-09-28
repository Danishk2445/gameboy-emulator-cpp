#include "mbc.hpp"

#include <ctime>

namespace gb {

namespace {

class NoMbc final : public Mbc {
public:
    using Mbc::Mbc;

    u8 read_rom(u16 addr) const override { return rom_byte(addr >> 14, addr); }
    void write_rom(u16, u8) override {}
    u8 read_ram(u16 addr) const override { return ram_byte(0, addr); }
    void write_ram(u16 addr, u8 value) override { set_ram_byte(0, addr, value); }
    void serialize(StateIO&) override {}
};

class Mbc1 final : public Mbc {
public:
    Mbc1(const std::vector<u8>& rom, std::vector<u8>& ram, bool multicart)
        : Mbc(rom, ram), shift_(multicart ? 4 : 5), bank1_mask_(multicart ? 0x0F : 0x1F) {}

    u8 read_rom(u16 addr) const override {
        if (addr < 0x4000) return rom_byte(mode_ ? (bank2_ << shift_) : 0, addr);
        return rom_byte((bank2_ << shift_) | (bank1_ & bank1_mask_), addr);
    }

    void write_rom(u16 addr, u8 value) override {
        switch (addr >> 13) {
            case 0: ram_enabled_ = (value & 0x0F) == 0x0A; break;
            case 1:
                bank1_ = value & 0x1F;
                if (bank1_ == 0) bank1_ = 1;
                break;
            case 2: bank2_ = value & 0x03; break;
            case 3: mode_ = value & 0x01; break;
        }
    }

    u8 read_ram(u16 addr) const override {
        if (!ram_enabled_) return 0xFF;
        return ram_byte(mode_ ? bank2_ : 0, addr);
    }

    void write_ram(u16 addr, u8 value) override {
        if (ram_enabled_) set_ram_byte(mode_ ? bank2_ : 0, addr, value);
    }

    void serialize(StateIO& s) override {
        s(ram_enabled_);
        s(bank1_);
        s(bank2_);
        s(mode_);
    }

private:
    u32 shift_;
    u8 bank1_mask_;
    bool ram_enabled_ = false;
    u8 bank1_ = 1;
    u8 bank2_ = 0;
    u8 mode_ = 0;
};

class Mbc2 final : public Mbc {
public:
    using Mbc::Mbc;

    u8 read_rom(u16 addr) const override {
        if (addr < 0x4000) return rom_byte(0, addr);
        return rom_byte(rom_bank_, addr);
    }

    void write_rom(u16 addr, u8 value) override {
        if (addr >= 0x4000) return;
        if (addr & 0x0100) {
            rom_bank_ = value & 0x0F;
            if (rom_bank_ == 0) rom_bank_ = 1;
        } else {
            ram_enabled_ = (value & 0x0F) == 0x0A;
        }
    }

    // 512 x 4-bit built-in RAM, mirrored across A000-BFFF.
    u8 read_ram(u16 addr) const override {
        if (!ram_enabled_ || ram_.empty()) return 0xFF;
        return 0xF0 | (ram_[addr & 0x01FF] & 0x0F);
    }

    void write_ram(u16 addr, u8 value) override {
        if (!ram_enabled_ || ram_.empty()) return;
        u8& slot = ram_[addr & 0x01FF];
        u8 v = 0xF0 | (value & 0x0F);
        if (slot != v) {
            slot = v;
            ram_dirty = true;
        }
    }

    void serialize(StateIO& s) override {
        s(ram_enabled_);
        s(rom_bank_);
    }

private:
    bool ram_enabled_ = false;
    u8 rom_bank_ = 1;
};

class Mbc3 final : public Mbc {
public:
    Mbc3(const std::vector<u8>& rom, std::vector<u8>& ram, bool rtc) : Mbc(rom, ram), has_rtc_(rtc) {}

    u8 read_rom(u16 addr) const override {
        if (addr < 0x4000) return rom_byte(0, addr);
        return rom_byte(rom_bank_, addr);
    }

    void write_rom(u16 addr, u8 value) override {
        switch (addr >> 13) {
            case 0: ram_enabled_ = (value & 0x0F) == 0x0A; break;
            case 1:
                // MBC30 (4 MiB carts) decodes 8 bank bits.
                rom_bank_ = value & (rom_banks() > 128 ? 0xFF : 0x7F);
                if (rom_bank_ == 0) rom_bank_ = 1;
                break;
            case 2: ram_bank_ = value & 0x0F; break;
            case 3:
                if (has_rtc_ && latch_prev_ == 0x00 && value == 0x01) latched_ = live_;
                latch_prev_ = value;
                break;
        }
    }

    u8 read_ram(u16 addr) const override {
        if (!ram_enabled_) return 0xFF;
        if (ram_bank_ <= 0x07) return ram_byte(ram_bank_, addr);
        if (has_rtc_ && ram_bank_ <= 0x0C) return latched_.reg(ram_bank_ - 0x08);
        return 0xFF;
    }

    void write_ram(u16 addr, u8 value) override {
        if (!ram_enabled_) return;
        if (ram_bank_ <= 0x07) {
            set_ram_byte(ram_bank_, addr, value);
        } else if (has_rtc_ && ram_bank_ <= 0x0C) {
            int idx = ram_bank_ - 0x08;
            if (idx == 0) subsecond_ = 0;
            live_.set_reg(idx, value);
            latched_.set_reg(idx, value);
            ram_dirty = true;
        }
    }

    void tick(u32 cycles) override {
        if (!has_rtc_ || live_.halted()) return;
        subsecond_ += cycles;
        while (subsecond_ >= kCpuHz) {
            subsecond_ -= kCpuHz;
            live_.advance_second();
        }
    }

    void serialize(StateIO& s) override {
        s(ram_enabled_);
        s(rom_bank_);
        s(ram_bank_);
        s(latch_prev_);
        s(live_);
        s(latched_);
        s(subsecond_);
    }

    bool has_rtc() const override { return has_rtc_; }

    // VBA-M / BGB compatible 48 byte footer.
    void save_rtc(std::vector<u8>& out) const override {
        auto put32 = [&](u32 v) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<u8>(v >> (8 * i)));
        };
        for (int i = 0; i < 5; ++i) put32(live_.reg(i));
        for (int i = 0; i < 5; ++i) put32(latched_.reg(i));
        u64 now = static_cast<u64>(std::time(nullptr));
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<u8>(now >> (8 * i)));
    }

    void load_rtc(const u8* data, size_t size) override {
        if (size < 44) return;
        auto get32 = [&](size_t off) {
            return u32(data[off]) | (u32(data[off + 1]) << 8) | (u32(data[off + 2]) << 16) | (u32(data[off + 3]) << 24);
        };
        for (int i = 0; i < 5; ++i) live_.set_reg(i, static_cast<u8>(get32(i * 4)));
        for (int i = 0; i < 5; ++i) latched_.set_reg(i, static_cast<u8>(get32(20 + i * 4)));
        u64 saved = get32(40);
        if (size >= 48) saved |= u64(get32(44)) << 32;
        u64 now = static_cast<u64>(std::time(nullptr));
        // Catch the clock up with the real time that passed while the game was closed.
        if (!live_.halted() && now > saved && now - saved < 10ull * 365 * 24 * 3600) {
            live_.advance_seconds(now - saved);
        }
    }

private:
    struct Rtc {
        u8 s = 0, m = 0, h = 0, dl = 0, dh = 0;  // dh: bit0 day MSB, bit6 halt, bit7 day carry

        bool halted() const { return dh & 0x40; }
        u8 reg(int i) const {
            switch (i) {
                case 0: return s;
                case 1: return m;
                case 2: return h;
                case 3: return dl;
                default: return dh;
            }
        }
        void set_reg(int i, u8 v) {
            switch (i) {
                case 0: s = v & 0x3F; break;
                case 1: m = v & 0x3F; break;
                case 2: h = v & 0x1F; break;
                case 3: dl = v; break;
                default: dh = v & 0xC1; break;
            }
        }
        void advance_second() {
            s = (s + 1) & 0x3F;
            if (s != 60) return;
            s = 0;
            m = (m + 1) & 0x3F;
            if (m != 60) return;
            m = 0;
            h = (h + 1) & 0x1F;
            if (h != 24) return;
            h = 0;
            u16 day = static_cast<u16>(((dh & 1) << 8) | dl) + 1;
            if (day > 0x1FF) {
                day = 0;
                dh |= 0x80;
            }
            dl = day & 0xFF;
            dh = (dh & 0xFE) | ((day >> 8) & 1);
        }
        void advance_seconds(u64 n) {
            // Whole days first so a long absence doesn't loop millions of times.
            while (n >= 86400 && s < 60 && m < 60 && h < 24) {
                u32 day = static_cast<u32>(((dh & 1) << 8) | dl) + 1;
                if (day > 0x1FF) {
                    day &= 0x1FF;
                    dh |= 0x80;
                }
                dl = day & 0xFF;
                dh = (dh & 0xFE) | ((day >> 8) & 1);
                n -= 86400;
            }
            while (n--) advance_second();
        }
    };

    bool has_rtc_;
    bool ram_enabled_ = false;
    u8 rom_bank_ = 1;
    u8 ram_bank_ = 0;
    u8 latch_prev_ = 0xFF;
    Rtc live_;
    Rtc latched_;
    u32 subsecond_ = 0;
};

class Mbc5 final : public Mbc {
public:
    Mbc5(const std::vector<u8>& rom, std::vector<u8>& ram, bool rumble) : Mbc(rom, ram), rumble_(rumble) {}

    u8 read_rom(u16 addr) const override {
        if (addr < 0x4000) return rom_byte(0, addr);
        return rom_byte(rom_bank_, addr);
    }

    void write_rom(u16 addr, u8 value) override {
        if (addr < 0x2000) {
            ram_enabled_ = (value & 0x0F) == 0x0A;
        } else if (addr < 0x3000) {
            rom_bank_ = (rom_bank_ & 0x100) | value;
        } else if (addr < 0x4000) {
            rom_bank_ = static_cast<u16>((rom_bank_ & 0xFF) | ((value & 1) << 8));
        } else if (addr < 0x6000) {
            ram_bank_ = value & (rumble_ ? 0x07 : 0x0F);
        }
    }

    u8 read_ram(u16 addr) const override { return ram_enabled_ ? ram_byte(ram_bank_, addr) : 0xFF; }
    void write_ram(u16 addr, u8 value) override {
        if (ram_enabled_) set_ram_byte(ram_bank_, addr, value);
    }

    void serialize(StateIO& s) override {
        s(ram_enabled_);
        s(rom_bank_);
        s(ram_bank_);
    }

private:
    bool rumble_;
    bool ram_enabled_ = false;
    u16 rom_bank_ = 1;
    u8 ram_bank_ = 0;
};

}  // namespace

CartTypeInfo cart_type_info(u8 type) {
    using K = MbcKind;
    switch (type) {
        case 0x00: return {K::None, false, false, false, false, "ROM ONLY"};
        case 0x01: return {K::Mbc1, false, false, false, false, "MBC1"};
        case 0x02: return {K::Mbc1, true, false, false, false, "MBC1+RAM"};
        case 0x03: return {K::Mbc1, true, true, false, false, "MBC1+RAM+BATTERY"};
        case 0x05: return {K::Mbc2, true, false, false, false, "MBC2"};
        case 0x06: return {K::Mbc2, true, true, false, false, "MBC2+BATTERY"};
        case 0x08: return {K::None, true, false, false, false, "ROM+RAM"};
        case 0x09: return {K::None, true, true, false, false, "ROM+RAM+BATTERY"};
        case 0x0B: return {K::Unsupported, false, false, false, false, "MMM01"};
        case 0x0C: return {K::Unsupported, true, false, false, false, "MMM01+RAM"};
        case 0x0D: return {K::Unsupported, true, true, false, false, "MMM01+RAM+BATTERY"};
        case 0x0F: return {K::Mbc3, false, true, true, false, "MBC3+TIMER+BATTERY"};
        case 0x10: return {K::Mbc3, true, true, true, false, "MBC3+TIMER+RAM+BATTERY"};
        case 0x11: return {K::Mbc3, false, false, false, false, "MBC3"};
        case 0x12: return {K::Mbc3, true, false, false, false, "MBC3+RAM"};
        case 0x13: return {K::Mbc3, true, true, false, false, "MBC3+RAM+BATTERY"};
        case 0x19: return {K::Mbc5, false, false, false, false, "MBC5"};
        case 0x1A: return {K::Mbc5, true, false, false, false, "MBC5+RAM"};
        case 0x1B: return {K::Mbc5, true, true, false, false, "MBC5+RAM+BATTERY"};
        case 0x1C: return {K::Mbc5, false, false, false, true, "MBC5+RUMBLE"};
        case 0x1D: return {K::Mbc5, true, false, false, true, "MBC5+RUMBLE+RAM"};
        case 0x1E: return {K::Mbc5, true, true, false, true, "MBC5+RUMBLE+RAM+BATTERY"};
        case 0x20: return {K::Unsupported, true, true, false, false, "MBC6"};
        case 0x22: return {K::Unsupported, true, true, false, true, "MBC7+SENSOR+RUMBLE+RAM+BATTERY"};
        case 0xFC: return {K::Unsupported, true, true, false, false, "POCKET CAMERA"};
        case 0xFD: return {K::Unsupported, true, true, false, false, "BANDAI TAMA5"};
        case 0xFE: return {K::Unsupported, true, true, false, false, "HuC3"};
        case 0xFF: return {K::Unsupported, true, true, false, false, "HuC1+RAM+BATTERY"};
        default: return {K::Unsupported, false, false, false, false, "unknown"};
    }
}

std::unique_ptr<Mbc> create_mbc(const CartTypeInfo& info, const std::vector<u8>& rom, std::vector<u8>& ram,
                                bool mbc1_multicart) {
    switch (info.kind) {
        case MbcKind::None: return std::make_unique<NoMbc>(rom, ram);
        case MbcKind::Mbc1: return std::make_unique<Mbc1>(rom, ram, mbc1_multicart);
        case MbcKind::Mbc2: return std::make_unique<Mbc2>(rom, ram);
        case MbcKind::Mbc3: return std::make_unique<Mbc3>(rom, ram, info.rtc);
        case MbcKind::Mbc5: return std::make_unique<Mbc5>(rom, ram, info.rumble);
        case MbcKind::Unsupported: break;
    }
    return nullptr;
}

}  // namespace gb
