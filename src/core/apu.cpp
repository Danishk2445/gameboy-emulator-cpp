#include "apu.hpp"

#include <algorithm>
#include <cmath>

namespace gb {

namespace {

// Bits that always read back as 1, for FF10-FF26.
constexpr u8 kReadMasks[0x17] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,  // NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,  // (FF15) NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF,  // (FF1F) NR41-NR44
    0x00, 0x00, 0x70,              // NR50-NR52
};

constexpr u8 kDutyTable[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};

constexpr size_t kMaxBufferedSamples = 48000 * 2;  // 1 s stereo; older samples are dropped

}  // namespace

void Apu::reset(bool post_boot) {
    powered_ = false;
    fs_step_ = 0;
    regs_.fill(0);
    ch1_ = {};
    ch2_ = {};
    ch3_ = {};
    ch4_ = {};
    wave_just_read_ = false;
    sample_phase_ = 0;
    acc_l_ = acc_r_ = 0;
    acc_n_ = 0;
    cap_l_ = cap_r_ = 0;
    samples_.clear();
    // DMG wave RAM powers up with a semi-random pattern; use a common one.
    static constexpr u8 kWaveInit[16] = {0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C,
                                         0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA};
    std::copy(std::begin(kWaveInit), std::end(kWaveInit), wave_ram_.begin());

    if (post_boot) {
        // The boot ROM leaves CH1 enabled (its "ding" has decayed to silence).
        powered_ = true;
        regs_[0x01] = 0x80;  // NR11: duty 50%
        regs_[0x02] = 0xF3;  // NR12
        regs_[0x14] = 0x77;  // NR50
        regs_[0x15] = 0xF3;  // NR51
        ch1_.enabled = true;
        ch1_.dac = true;
        ch1_.duty = 2;
        ch1_.env.write(0xF3);
        ch1_.env.volume = 0;
        ch1_.freq = 0x7C1;
        ch1_.timer = (2048 - ch1_.freq) * 4;
    }
}

void Apu::set_sample_rate(u32 hz) {
    sample_rate_ = hz;
    hp_charge_ = static_cast<float>(std::pow(0.999958, double(kCpuHz) / hz));
}

u8 Apu::channel_mask() const {
    return static_cast<u8>((ch1_.enabled ? 1 : 0) | (ch2_.enabled ? 2 : 0) | (ch3_.enabled ? 4 : 0) |
                           (ch4_.enabled ? 8 : 0));
}

void Apu::tick() {
    wave_just_read_ = false;
    if (powered_) {
        for (Square* sq : {&ch1_, &ch2_}) {
            if (!sq->enabled) continue;
            sq->timer -= 4;
            while (sq->timer <= 0) {
                sq->timer += (2048 - sq->freq) * 4;
                sq->duty_pos = (sq->duty_pos + 1) & 7;
            }
        }
        if (ch3_.enabled) {
            ch3_.timer -= 4;
            while (ch3_.timer <= 0) {
                ch3_.timer += (2048 - ch3_.freq) * 2;
                ch3_.pos = (ch3_.pos + 1) & 31;
                ch3_.buffer = wave_ram_[ch3_.pos >> 1];
                wave_just_read_ = true;
            }
        }
        if (ch4_.enabled) {
            ch4_.timer -= 4;
            while (ch4_.timer <= 0) {
                ch4_.timer += noise_period();
                if (ch4_.shift < 14) {
                    u16 bit = (ch4_.lfsr ^ (ch4_.lfsr >> 1)) & 1;
                    ch4_.lfsr = static_cast<u16>((ch4_.lfsr >> 1) | (bit << 14));
                    if (ch4_.narrow) ch4_.lfsr = static_cast<u16>((ch4_.lfsr & ~0x40u) | (bit << 6));
                }
            }
        }
    }
    if (output_enabled_) mix();
}

void Apu::mix() {
    // Digital 0-15 per channel -> DAC output in [-1, 1] (0 when the DAC is off).
    auto dac = [](bool on, u8 digital) { return on ? static_cast<float>(digital) / 7.5f - 1.0f : 0.0f; };

    u8 d1 = ch1_.enabled && ((kDutyTable[ch1_.duty] >> (7 - ch1_.duty_pos)) & 1) ? ch1_.env.volume : 0;
    u8 d2 = ch2_.enabled && ((kDutyTable[ch2_.duty] >> (7 - ch2_.duty_pos)) & 1) ? ch2_.env.volume : 0;
    u8 d3 = 0;
    if (ch3_.enabled && ch3_.volume_code) {
        u8 nibble = (ch3_.pos & 1) ? (ch3_.buffer & 0x0F) : (ch3_.buffer >> 4);
        d3 = nibble >> (ch3_.volume_code - 1);
    }
    u8 d4 = ch4_.enabled && !(ch4_.lfsr & 1) ? ch4_.env.volume : 0;

    float out[4] = {dac(ch1_.dac, d1), dac(ch2_.dac, d2), dac(ch3_.dac, d3), dac(ch4_.dac, d4)};
    u8 nr50 = regs_[0x14];
    u8 nr51 = regs_[0x15];
    float l = 0, r = 0;
    for (int i = 0; i < 4; ++i) {
        if (nr51 & (0x10 << i)) l += out[i];
        if (nr51 & (0x01 << i)) r += out[i];
    }
    acc_l_ += l * static_cast<float>(((nr50 >> 4) & 7) + 1);
    acc_r_ += r * static_cast<float>((nr50 & 7) + 1);
    ++acc_n_;

    sample_phase_ += u64(sample_rate_) * 4;
    if (sample_phase_ < kCpuHz) return;
    sample_phase_ -= kCpuHz;

    // Average over the sample period, scale 4 channels x volume 8 into [-1, 1], then high-pass.
    float in_l = acc_l_ / static_cast<float>(acc_n_) / 32.0f;
    float in_r = acc_r_ / static_cast<float>(acc_n_) / 32.0f;
    acc_l_ = acc_r_ = 0;
    acc_n_ = 0;
    float out_l = in_l - cap_l_;
    float out_r = in_r - cap_r_;
    cap_l_ = in_l - out_l * hp_charge_;
    cap_r_ = in_r - out_r * hp_charge_;

    if (samples_.size() >= kMaxBufferedSamples) samples_.erase(samples_.begin(), samples_.begin() + kMaxBufferedSamples / 2);
    samples_.push_back(std::clamp(out_l, -1.0f, 1.0f));
    samples_.push_back(std::clamp(out_r, -1.0f, 1.0f));
}

void Apu::frame_sequencer_clock() {
    if (!powered_) return;
    switch (fs_step_) {
        case 0:
        case 4: clock_length(); break;
        case 2:
        case 6:
            clock_length();
            clock_sweep();
            break;
        case 7:
            if (ch1_.enabled) ch1_.env.clock();
            if (ch2_.enabled) ch2_.env.clock();
            if (ch4_.enabled) ch4_.env.clock();
            break;
        default: break;
    }
    fs_step_ = (fs_step_ + 1) & 7;
}

void Apu::clock_length() {
    auto clock = [](auto& ch) {
        if (ch.length_enabled && ch.length > 0 && --ch.length == 0) ch.enabled = false;
    };
    clock(ch1_);
    clock(ch2_);
    clock(ch3_);
    clock(ch4_);
}

u16 Apu::sweep_calc() {
    u16 delta = ch1_.shadow >> ch1_.sweep_shift;
    u16 next;
    if (ch1_.sweep_negate) {
        next = static_cast<u16>(ch1_.shadow - delta);
        ch1_.negate_used = true;
    } else {
        next = static_cast<u16>(ch1_.shadow + delta);
    }
    if (next > 2047) ch1_.enabled = false;
    return next;
}

void Apu::clock_sweep() {
    if (ch1_.sweep_timer > 0) --ch1_.sweep_timer;
    if (ch1_.sweep_timer != 0) return;
    ch1_.sweep_timer = ch1_.sweep_period ? ch1_.sweep_period : 8;
    if (!ch1_.sweep_enabled || ch1_.sweep_period == 0) return;
    u16 next = sweep_calc();
    if (next <= 2047 && ch1_.sweep_shift != 0) {
        ch1_.shadow = next;
        ch1_.freq = next;
        regs_[0x03] = next & 0xFF;
        regs_[0x04] = static_cast<u8>((regs_[0x04] & 0xF8) | (next >> 8));
        sweep_calc();  // overflow check with the new frequency
    }
}

// NRx4 length-enable write, including the extra length clock that happens
// when enabling in the half of the frame sequencer period that doesn't clock length.
template <class Ch>
void Apu::write_length_enable(Ch& ch, u8 value, u16 max_length) {
    bool was_enabled = ch.length_enabled;
    ch.length_enabled = value & 0x40;
    bool trigger = value & 0x80;
    bool extra_clock = fs_step_ & 1;
    if (!was_enabled && ch.length_enabled && extra_clock && ch.length > 0) {
        if (--ch.length == 0 && !trigger) ch.enabled = false;
    }
    if (trigger && ch.length == 0) {
        ch.length = max_length;
        if (ch.length_enabled && extra_clock) --ch.length;
    }
}

void Apu::power_off() {
    // DMG keeps the length counters when the APU is powered off.
    u16 l1 = ch1_.length, l2 = ch2_.length, l3 = ch3_.length, l4 = ch4_.length;
    ch1_ = {};
    ch2_ = {};
    ch3_ = {};
    ch4_ = {};
    ch1_.length = l1;
    ch2_.length = l2;
    ch3_.length = l3;
    ch4_.length = l4;
    regs_.fill(0);
    powered_ = false;
}

u8 Apu::read(u16 addr) const {
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        // While CH3 plays, the CPU only sees the byte CH3 is reading at that exact moment.
        if (ch3_.enabled) return wave_just_read_ ? wave_ram_[ch3_.pos >> 1] : 0xFF;
        return wave_ram_[addr - 0xFF30];
    }
    if (addr == 0xFF26) return static_cast<u8>(0x70 | (powered_ ? 0x80 : 0) | channel_mask());
    if (addr >= 0xFF10 && addr < 0xFF26) return regs_[addr - 0xFF10] | kReadMasks[addr - 0xFF10];
    return 0xFF;
}

void Apu::write(u16 addr, u8 value) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        if (ch3_.enabled) {
            if (wave_just_read_) wave_ram_[ch3_.pos >> 1] = value;
        } else {
            wave_ram_[addr - 0xFF30] = value;
        }
        return;
    }
    if (addr == 0xFF26) {
        bool on = value & 0x80;
        if (powered_ && !on) {
            power_off();
        } else if (!powered_ && on) {
            powered_ = true;
            fs_step_ = 0;
            ch1_.duty_pos = ch2_.duty_pos = 0;
            ch3_.buffer = 0;
        }
        return;
    }
    if (addr < 0xFF10 || addr > 0xFF25) return;

    if (!powered_) {
        // DMG: length counters stay writable while powered off.
        switch (addr) {
            case 0xFF11: ch1_.length = 64 - (value & 0x3F); break;
            case 0xFF16: ch2_.length = 64 - (value & 0x3F); break;
            case 0xFF1B: ch3_.length = 256 - value; break;
            case 0xFF20: ch4_.length = 64 - (value & 0x3F); break;
            default: break;
        }
        return;
    }

    regs_[addr - 0xFF10] = value;
    switch (addr) {
        // CH1
        case 0xFF10:
            ch1_.sweep_period = (value >> 4) & 7;
            ch1_.sweep_shift = value & 7;
            // Leaving negate mode after a negate calculation disables the channel.
            if (ch1_.sweep_negate && !(value & 0x08) && ch1_.negate_used) ch1_.enabled = false;
            ch1_.sweep_negate = value & 0x08;
            break;
        case 0xFF11:
            ch1_.duty = value >> 6;
            ch1_.length = 64 - (value & 0x3F);
            break;
        case 0xFF12:
            ch1_.env.write(value);
            ch1_.dac = (value & 0xF8) != 0;
            if (!ch1_.dac) ch1_.enabled = false;
            break;
        case 0xFF13: ch1_.freq = static_cast<u16>((ch1_.freq & 0x700) | value); break;
        case 0xFF14:
            ch1_.freq = static_cast<u16>((ch1_.freq & 0xFF) | ((value & 7) << 8));
            write_length_enable(ch1_, value, 64);
            if (value & 0x80) {
                ch1_.enabled = ch1_.dac;
                ch1_.timer = (2048 - ch1_.freq) * 4;
                ch1_.env.trigger();
                ch1_.shadow = ch1_.freq;
                ch1_.sweep_timer = ch1_.sweep_period ? ch1_.sweep_period : 8;
                ch1_.sweep_enabled = ch1_.sweep_period != 0 || ch1_.sweep_shift != 0;
                ch1_.negate_used = false;
                if (ch1_.sweep_shift != 0) sweep_calc();
            }
            break;

        // CH2
        case 0xFF16:
            ch2_.duty = value >> 6;
            ch2_.length = 64 - (value & 0x3F);
            break;
        case 0xFF17:
            ch2_.env.write(value);
            ch2_.dac = (value & 0xF8) != 0;
            if (!ch2_.dac) ch2_.enabled = false;
            break;
        case 0xFF18: ch2_.freq = static_cast<u16>((ch2_.freq & 0x700) | value); break;
        case 0xFF19:
            ch2_.freq = static_cast<u16>((ch2_.freq & 0xFF) | ((value & 7) << 8));
            write_length_enable(ch2_, value, 64);
            if (value & 0x80) {
                ch2_.enabled = ch2_.dac;
                ch2_.timer = (2048 - ch2_.freq) * 4;
                ch2_.env.trigger();
            }
            break;

        // CH3
        case 0xFF1A:
            ch3_.dac = value & 0x80;
            if (!ch3_.dac) ch3_.enabled = false;
            break;
        case 0xFF1B: ch3_.length = 256 - value; break;
        case 0xFF1C: ch3_.volume_code = (value >> 5) & 3; break;
        case 0xFF1D: ch3_.freq = static_cast<u16>((ch3_.freq & 0x700) | value); break;
        case 0xFF1E:
            ch3_.freq = static_cast<u16>((ch3_.freq & 0xFF) | ((value & 7) << 8));
            write_length_enable(ch3_, value, 256);
            if (value & 0x80) {
                // DMG quirk: retriggering right as CH3 reads a sample corrupts the first bytes of wave RAM.
                if (ch3_.enabled && ch3_.timer <= 4) {
                    u8 next = static_cast<u8>(((ch3_.pos + 1) & 31) >> 1);
                    if (next < 4) {
                        wave_ram_[0] = wave_ram_[next];
                    } else {
                        std::copy_n(wave_ram_.begin() + (next & 0x0C), 4, wave_ram_.begin());
                    }
                }
                ch3_.enabled = ch3_.dac;
                ch3_.timer = (2048 - ch3_.freq) * 2 + 6;
                ch3_.pos = 0;
            }
            break;

        // CH4
        case 0xFF20: ch4_.length = 64 - (value & 0x3F); break;
        case 0xFF21:
            ch4_.env.write(value);
            ch4_.dac = (value & 0xF8) != 0;
            if (!ch4_.dac) ch4_.enabled = false;
            break;
        case 0xFF22:
            ch4_.shift = value >> 4;
            ch4_.narrow = value & 0x08;
            ch4_.divisor = value & 0x07;
            break;
        case 0xFF23:
            write_length_enable(ch4_, value, 64);
            if (value & 0x80) {
                ch4_.enabled = ch4_.dac;
                ch4_.timer = noise_period();
                ch4_.env.trigger();
                ch4_.lfsr = 0x7FFF;
            }
            break;

        default: break;  // NR50 / NR51 are only read back from regs_
    }
}

void Apu::serialize(StateIO& s) {
    s(powered_);
    s(fs_step_);
    s(regs_);
    s(wave_ram_);
    s(wave_just_read_);
    s(ch1_);
    s(ch2_);
    s(ch3_);
    s(ch4_);
    s(sample_phase_);
    s(acc_l_);
    s(acc_r_);
    s(acc_n_);
    s(cap_l_);
    s(cap_r_);
}

}  // namespace gb
