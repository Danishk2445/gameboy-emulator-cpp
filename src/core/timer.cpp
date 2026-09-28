#include "timer.hpp"

#include "apu.hpp"
#include "serial.hpp"

namespace gb {

u16 Timer::tac_mask(u8 tac) {
    static constexpr u16 kMasks[4] = {1u << 9, 1u << 3, 1u << 5, 1u << 7};
    return kMasks[tac & 3];
}

void Timer::reset(u16 counter) {
    counter_ = counter;
    tima_ = 0;
    tma_ = 0;
    tac_ = 0;
    overflow_pending_ = false;
    reloading_ = false;
}

void Timer::tick() {
    reloading_ = false;
    if (overflow_pending_) {
        overflow_pending_ = false;
        tima_ = tma_;
        irq_.request(kIntTimer);
        reloading_ = true;
    }
    set_counter(static_cast<u16>(counter_ + 4));
}

void Timer::set_counter(u16 value) {
    u16 fell = counter_ & static_cast<u16>(~value);
    counter_ = value;
    if ((tac_ & 0x04) && (fell & tac_mask(tac_))) increment_tima();
    if (fell & 0x1000) apu_.frame_sequencer_clock();
    if (fell & 0x0100) serial_.clock();
}

void Timer::increment_tima() {
    if (++tima_ == 0) overflow_pending_ = true;  // reads 0x00 for one cycle before the reload
}

u8 Timer::read(u16 addr) const {
    switch (addr) {
        case 0xFF04: return static_cast<u8>(counter_ >> 8);
        case 0xFF05: return tima_;
        case 0xFF06: return tma_;
        case 0xFF07: return tac_ | 0xF8;
        default: return 0xFF;
    }
}

void Timer::write(u16 addr, u8 value) {
    switch (addr) {
        case 0xFF04: set_counter(0); break;
        case 0xFF05:
            if (reloading_) break;  // the reload wins
            tima_ = value;
            overflow_pending_ = false;  // writing during the overflow cycle cancels the reload
            break;
        case 0xFF06:
            tma_ = value;
            if (reloading_) tima_ = value;
            break;
        case 0xFF07: {
            // Changing TAC can produce a falling edge on the multiplexer output.
            bool old_signal = (tac_ & 0x04) && (counter_ & tac_mask(tac_));
            tac_ = value & 0x07;
            bool new_signal = (tac_ & 0x04) && (counter_ & tac_mask(tac_));
            if (old_signal && !new_signal) increment_tima();
            break;
        }
    }
}

void Timer::serialize(StateIO& s) {
    s(counter_);
    s(tima_);
    s(tma_);
    s(tac_);
    s(overflow_pending_);
    s(reloading_);
}

}  // namespace gb
