#pragma once

#include <string>

#include "common.hpp"
#include "state.hpp"

namespace gb {

// Serial port with no link partner: internally clocked transfers complete
// after 8 bits and shift in 1s. Sent bytes are captured (Blargg's tests print
// their results this way).
class Serial {
public:
    explicit Serial(Interrupts& irq) : irq_(irq) {}

    void reset();
    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);
    void clock();  // 8192 Hz internal clock edge, driven by the timer

    const std::string& output() const { return output_; }
    void clear_output() { output_.clear(); }
    void serialize(StateIO& s);

private:
    Interrupts& irq_;
    u8 sb_ = 0;
    u8 sc_ = 0;
    u8 bits_left_ = 0;
    std::string output_;
};

}  // namespace gb
