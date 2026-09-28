#pragma once

#include "common.hpp"
#include "state.hpp"

namespace gb {

class Apu;
class Serial;

// DIV/TIMA/TMA/TAC. The 16-bit system counter also drives the APU frame
// sequencer (bit 12) and the serial clock (bit 8) on their falling edges.
class Timer {
public:
    Timer(Interrupts& irq, Apu& apu, Serial& serial) : irq_(irq), apu_(apu), serial_(serial) {}

    void reset(u16 counter);
    void tick();  // one M-cycle (4 T-cycles)

    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);
    void reset_div() { set_counter(0); }

    u16 counter() const { return counter_; }
    void serialize(StateIO& s);

private:
    static u16 tac_mask(u8 tac);
    void set_counter(u16 value);
    void increment_tima();

    Interrupts& irq_;
    Apu& apu_;
    Serial& serial_;

    u16 counter_ = 0;
    u8 tima_ = 0;
    u8 tma_ = 0;
    u8 tac_ = 0;
    bool overflow_pending_ = false;  // TIMA overflowed this cycle; reload next cycle
    bool reloading_ = false;         // TMA was copied into TIMA during this cycle
};

}  // namespace gb
