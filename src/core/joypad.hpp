#pragma once

#include "common.hpp"
#include "state.hpp"

namespace gb {

enum class Button : u8 { Right = 0, Left, Up, Down, A, B, Select, Start };

class Joypad {
public:
    explicit Joypad(Interrupts& irq) : irq_(irq) {}

    // The boot ROM leaves both button groups selected (P1 reads 0xCF).
    void reset(bool post_boot) {
        select_ = post_boot ? 0x00 : 0x30;
        pressed_ = 0;
    }
    u8 read() const;
    void write(u8 value);
    void set_button(Button button, bool pressed);
    bool any_pressed() const { return pressed_ != 0; }
    u8 pressed_mask() const { return pressed_; }

    void serialize(StateIO& s) {
        s(select_);
        s(pressed_);
    }

private:
    u8 lines() const;  // active-low input lines P10-P13 for the current selection

    Interrupts& irq_;
    u8 select_ = 0x30;
    u8 pressed_ = 0;  // bit = Button index
};

}  // namespace gb
