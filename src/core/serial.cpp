#include "serial.hpp"

namespace gb {

void Serial::reset() {
    sb_ = 0;
    sc_ = 0;
    bits_left_ = 0;
    output_.clear();
}

u8 Serial::read(u16 addr) const {
    if (addr == 0xFF01) return sb_;
    if (addr == 0xFF02) return sc_ | 0x7E;
    return 0xFF;
}

void Serial::write(u16 addr, u8 value) {
    if (addr == 0xFF01) {
        sb_ = value;
    } else if (addr == 0xFF02) {
        sc_ = value & 0x81;
        if ((sc_ & 0x81) == 0x81) {
            bits_left_ = 8;
            if (output_.size() < (1u << 16)) output_.push_back(static_cast<char>(sb_));
        } else {
            bits_left_ = 0;
        }
    }
}

void Serial::clock() {
    if (bits_left_ == 0) return;
    sb_ = static_cast<u8>((sb_ << 1) | 1);
    if (--bits_left_ == 0) {
        sc_ &= 0x7F;
        irq_.request(kIntSerial);
    }
}

void Serial::serialize(StateIO& s) {
    s(sb_);
    s(sc_);
    s(bits_left_);
}

}  // namespace gb
