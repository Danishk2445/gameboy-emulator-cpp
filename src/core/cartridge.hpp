#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "mbc.hpp"
#include "state.hpp"

namespace gb {

struct CartHeader {
    std::string title;
    u8 cgb_flag = 0;
    u8 sgb_flag = 0;
    u8 cart_type = 0;
    u8 rom_size_code = 0;
    u8 ram_size_code = 0;
    u8 header_checksum = 0;
    u16 global_checksum = 0;
    bool header_checksum_valid = false;
    bool logo_valid = false;
};

class Cartridge {
public:
    // Both return nullptr and fill `error` with a user-facing message on failure.
    static std::unique_ptr<Cartridge> from_file(const std::string& path, std::string& error);
    static std::unique_ptr<Cartridge> from_bytes(std::vector<u8> data, std::string& error);

    u8 read_rom(u16 addr) const { return mbc_->read_rom(addr); }
    void write_rom(u16 addr, u8 value) { mbc_->write_rom(addr, value); }
    u8 read_ram(u16 addr) const { return mbc_->read_ram(addr); }
    void write_ram(u16 addr, u8 value) { mbc_->write_ram(addr, value); }
    void tick(u32 cycles) {
        if (info_.rtc) mbc_->tick(cycles);
    }

    const CartHeader& header() const { return header_; }
    const CartTypeInfo& type_info() const { return info_; }
    bool has_battery() const { return info_.battery; }
    const std::vector<u8>& ram() const { return ram_; }
    size_t rom_size() const { return rom_.size(); }

    // Battery file contents: external RAM followed by the RTC footer (if any).
    std::vector<u8> battery_data() const;
    bool load_battery_data(const std::vector<u8>& data);
    bool battery_dirty() const { return mbc_->ram_dirty; }
    void clear_battery_dirty() { mbc_->ram_dirty = false; }

    void serialize(StateIO& s);

private:
    Cartridge() = default;

    CartHeader header_;
    CartTypeInfo info_{};
    std::vector<u8> rom_;
    std::vector<u8> ram_;
    std::unique_ptr<Mbc> mbc_;
};

}  // namespace gb
