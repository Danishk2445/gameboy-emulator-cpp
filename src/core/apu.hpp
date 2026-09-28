#pragma once

#include <array>
#include <vector>

#include "common.hpp"
#include "state.hpp"

namespace gb {

// Audio processing unit: two square channels (CH1 with sweep), a wave channel
// and a noise channel. Output is box-filtered down to the host sample rate and
// passed through a DC-blocking high-pass filter, like the real capacitor.
class Apu {
public:
    static constexpr u32 kDefaultSampleRate = 48000;

    Apu() { set_sample_rate(kDefaultSampleRate); }

    void reset(bool post_boot);
    void tick();                   // one M-cycle
    void frame_sequencer_clock();  // 512 Hz DIV-APU event from the timer

    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);

    void set_sample_rate(u32 hz);
    u32 sample_rate() const { return sample_rate_; }
    void set_output_enabled(bool enabled) { output_enabled_ = enabled; }
    // Interleaved stereo float samples produced since the last clear.
    std::vector<float>& samples() { return samples_; }

    bool powered() const { return powered_; }
    u8 channel_mask() const;  // bit n = channel n+1 enabled

    void serialize(StateIO& s);

private:
    struct Envelope {
        u8 initial = 0;
        bool up = false;
        u8 period = 0;
        u8 volume = 0;
        u8 timer = 0;

        void write(u8 v) {
            initial = v >> 4;
            up = v & 0x08;
            period = v & 0x07;
        }
        void trigger() {
            volume = initial;
            timer = period ? period : 8;
        }
        void clock() {
            if (period == 0) return;
            if (timer > 0) --timer;
            if (timer == 0) {
                timer = period;
                if (up && volume < 15) ++volume;
                if (!up && volume > 0) --volume;
            }
        }
    };

    struct Square {
        bool enabled = false;
        bool dac = false;
        u16 length = 0;
        bool length_enabled = false;
        u8 duty = 0;
        u8 duty_pos = 0;
        u16 freq = 0;
        i32 timer = 0;
        Envelope env;
        // Sweep (CH1 only)
        u8 sweep_period = 0;
        u8 sweep_shift = 0;
        bool sweep_negate = false;
        u8 sweep_timer = 0;
        bool sweep_enabled = false;
        bool negate_used = false;
        u16 shadow = 0;
    };

    struct Wave {
        bool enabled = false;
        bool dac = false;
        u16 length = 0;
        bool length_enabled = false;
        u16 freq = 0;
        i32 timer = 0;
        u8 volume_code = 0;
        u8 pos = 0;
        u8 buffer = 0;
    };

    struct Noise {
        bool enabled = false;
        bool dac = false;
        u16 length = 0;
        bool length_enabled = false;
        Envelope env;
        u8 shift = 0;
        bool narrow = false;
        u8 divisor = 0;
        i32 timer = 0;
        u16 lfsr = 0x7FFF;
    };

    template <class Ch>
    void write_length_enable(Ch& ch, u8 value, u16 max_length);
    u16 sweep_calc();
    void clock_length();
    void clock_sweep();
    void power_off();
    i32 noise_period() const { return (ch4_.divisor ? ch4_.divisor * 16 : 8) << ch4_.shift; }
    void mix();

    bool powered_ = false;
    u8 fs_step_ = 0;  // next frame sequencer step
    std::array<u8, 0x17> regs_{};  // FF10-FF26 as written
    std::array<u8, 16> wave_ram_{};
    bool wave_just_read_ = false;

    Square ch1_, ch2_;
    Wave ch3_;
    Noise ch4_;

    // Output
    bool output_enabled_ = true;
    u32 sample_rate_ = kDefaultSampleRate;
    u64 sample_phase_ = 0;
    float acc_l_ = 0, acc_r_ = 0;
    u32 acc_n_ = 0;
    float cap_l_ = 0, cap_r_ = 0;
    float hp_charge_ = 0.996f;
    std::vector<float> samples_;
};

}  // namespace gb
