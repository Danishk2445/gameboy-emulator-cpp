#include "joypad.hpp"

namespace gb {

u8 Joypad::lines() const {
    u8 low = 0x0F;
    if (!(select_ & 0x10)) low &= static_cast<u8>(~(pressed_ & 0x0F));
    if (!(select_ & 0x20)) low &= static_cast<u8>(~(pressed_ >> 4));
    return low;
}

u8 Joypad::read() const { return 0xC0 | select_ | lines(); }

void Joypad::write(u8 value) {
    u8 before = lines();
    select_ = value & 0x30;
    if (before & ~lines()) irq_.request(kIntJoypad);
}

void Joypad::set_button(Button button, bool pressed) {
    u8 before = lines();
    u8 bit = static_cast<u8>(1u << static_cast<u8>(button));
    if (pressed) {
        pressed_ |= bit;
    } else {
        pressed_ &= static_cast<u8>(~bit);
    }
    // The interrupt fires on a high-to-low transition of any selected line.
    if (before & ~lines()) irq_.request(kIntJoypad);
}

}  // namespace gb
