#include "cartridge.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace gb {

namespace {

constexpr u8 kNintendoLogo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

bool logo_at(const std::vector<u8>& data, size_t offset) {
    if (data.size() < offset + sizeof(kNintendoLogo)) return false;
    return std::equal(std::begin(kNintendoLogo), std::end(kNintendoLogo), data.begin() + static_cast<long>(offset));
}

size_t ram_size_from_code(u8 code) {
    switch (code) {
        case 0x01: return 0x800;
        case 0x02: return 0x2000;
        case 0x03: return 0x8000;
        case 0x04: return 0x20000;
        case 0x05: return 0x10000;
        default: return 0;
    }
}

std::string hex8(u8 v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", v);
    return buf;
}

}  // namespace

std::unique_ptr<Cartridge> Cartridge::from_file(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Could not open ROM file: " + path;
        return nullptr;
    }
    std::vector<u8> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return from_bytes(std::move(data), error);
}

std::unique_ptr<Cartridge> Cartridge::from_bytes(std::vector<u8> data, std::string& error) {
    if (data.size() < 0x150) {
        error = "File is too small to be a Game Boy ROM.";
        return nullptr;
    }

    std::unique_ptr<Cartridge> cart(new Cartridge());
    CartHeader& h = cart->header_;
    h.logo_valid = logo_at(data, 0x104);

    // GBA carts carry the fixed value 0x96 at 0xB2 and have no GB logo at 0x104.
    if (!h.logo_valid && data[0xB2] == 0x96) {
        error = "This is a Game Boy Advance ROM. This emulator only supports original Game Boy (DMG) games.";
        return nullptr;
    }

    h.cgb_flag = data[0x143];
    h.sgb_flag = data[0x146];
    h.cart_type = data[0x147];
    h.rom_size_code = data[0x148];
    h.ram_size_code = data[0x149];
    h.header_checksum = data[0x14D];
    h.global_checksum = static_cast<u16>((data[0x14E] << 8) | data[0x14F]);

    u8 sum = 0;
    for (size_t i = 0x134; i <= 0x14C; ++i) sum = static_cast<u8>(sum - data[i] - 1);
    h.header_checksum_valid = sum == h.header_checksum;

    // Title is up to 16 bytes, shorter on CGB-aware carts where the tail holds the manufacturer code.
    size_t title_len = (h.cgb_flag & 0x80) ? 15 : 16;
    for (size_t i = 0; i < title_len; ++i) {
        u8 c = data[0x134 + i];
        if (c == 0) break;
        h.title.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
    }
    while (!h.title.empty() && h.title.back() == ' ') h.title.pop_back();

    if (h.cgb_flag == 0xC0) {
        error = "\"" + h.title +
                "\" is a Game Boy Color-only game. This emulator only supports original Game Boy (DMG) games.";
        return nullptr;
    }

    cart->info_ = cart_type_info(h.cart_type);
    if (cart->info_.kind == MbcKind::Unsupported) {
        error = "Unsupported cartridge type " + hex8(h.cart_type) + " (" + cart->info_.name + ").";
        return nullptr;
    }

    // Pad ROM to a power-of-two number of banks (minimum 32 KiB) so bank numbers can be masked.
    size_t rom_size = 0x8000;
    while (rom_size < data.size()) rom_size <<= 1;
    data.resize(rom_size, 0xFF);
    cart->rom_ = std::move(data);

    size_t ram_size = 0;
    if (cart->info_.kind == MbcKind::Mbc2) {
        ram_size = 512;
    } else if (cart->info_.ram) {
        ram_size = ram_size_from_code(h.ram_size_code);
        if (ram_size == 0) ram_size = 0x2000;  // header says RAM but no size: assume one bank
    }
    cart->ram_.assign(ram_size, 0xFF);

    // MBC1 multicarts (e.g. Bomberman Collection) are 8 Mbit with a second header in bank 0x10.
    bool multicart = cart->info_.kind == MbcKind::Mbc1 && cart->rom_.size() == 0x100000 && logo_at(cart->rom_, 0x40104);

    cart->mbc_ = create_mbc(cart->info_, cart->rom_, cart->ram_, multicart);
    return cart;
}

std::vector<u8> Cartridge::battery_data() const {
    std::vector<u8> out = ram_;
    if (mbc_->has_rtc()) mbc_->save_rtc(out);
    return out;
}

bool Cartridge::load_battery_data(const std::vector<u8>& data) {
    if (data.size() < ram_.size()) return false;
    std::copy_n(data.begin(), ram_.size(), ram_.begin());
    if (mbc_->has_rtc() && data.size() > ram_.size()) {
        mbc_->load_rtc(data.data() + ram_.size(), data.size() - ram_.size());
    }
    return true;
}

void Cartridge::serialize(StateIO& s) {
    s.vec(ram_);
    mbc_->serialize(s);
}

}  // namespace gb
