#include "apu.h"
#include "memory.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <ostream>
#include <istream>

void APU::saveState(std::ostream& out) const {
    auto W = [&](const auto& x) {
        out.write(reinterpret_cast<const char*>(&x), sizeof(x));
    };
    W(ch1); W(ch2); W(ch3); W(ch4);
    W(nr50); W(nr51);
    uint8_t pwr = powered ? 1 : 0;
    W(pwr);
    W(frameSeqCounter);
    W(frameSeqStep);
    W(sampleAccum);
}

bool APU::loadState(std::istream& in) {
    auto R = [&](auto& x) {
        return static_cast<bool>(
            in.read(reinterpret_cast<char*>(&x), sizeof(x)));
    };
    if (!R(ch1) || !R(ch2) || !R(ch3) || !R(ch4)) return false;
    if (!R(nr50) || !R(nr51)) return false;
    uint8_t pwr = 0;
    if (!R(pwr)) return false;
    powered = pwr != 0;
    if (!R(frameSeqCounter) || !R(frameSeqStep) || !R(sampleAccum)) return false;
    // Drain audio ring buffer so we don't play stale samples.
    if (device) SDL_LockAudioDevice(device);
    writeIdx.store(0);
    readIdx.store(0);
    std::memset(ring, 0, sizeof(ring));
    if (device) SDL_UnlockAudioDevice(device);
    return true;
}

namespace {
constexpr uint8_t DUTY_TABLE[4] = { 0b00000001, 0b10000001, 0b10000111, 0b01111110 };
constexpr int NOISE_DIVISORS[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
}

APU::APU(Memory& mem) : memory(mem) {
    reset();
}

APU::~APU() {
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
}

bool APU::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = &APU::audioCallback;
    want.userdata = this;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device == 0) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void APU::reset() {
    ch1 = Square{};
    ch2 = Square{};
    ch3 = Wave{};
    ch4 = Noise{};
    ch4.lfsr = 0x7FFF;
    nr50 = 0x77;
    nr51 = 0xF3;
    powered = true;
    frameSeqCounter = 0;
    frameSeqStep = 0;
    sampleAccum = 0.0;
    writeIdx = 0;
    readIdx = 0;
    std::memset(ring, 0, sizeof(ring));
}

void APU::audioCallback(void* userdata, Uint8* stream, int len) {
    APU* apu = static_cast<APU*>(userdata);
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    int frames = len / (2 * sizeof(int16_t));
    int r = apu->readIdx.load(std::memory_order_acquire);
    int w = apu->writeIdx.load(std::memory_order_acquire);
    int available = (w - r + BUFFER_FRAMES) % BUFFER_FRAMES;
    int take = std::min(frames, available);
    for (int i = 0; i < take; ++i) {
        out[i * 2]     = apu->ring[r * 2];
        out[i * 2 + 1] = apu->ring[r * 2 + 1];
        r = (r + 1) % BUFFER_FRAMES;
    }
    for (int i = take; i < frames; ++i) {
        out[i * 2]     = 0;
        out[i * 2 + 1] = 0;
    }
    apu->readIdx.store(r, std::memory_order_release);
}

void APU::pushSample(int16_t l, int16_t r) {
    int w = writeIdx.load(std::memory_order_relaxed);
    int rd = readIdx.load(std::memory_order_acquire);
    int next = (w + 1) % BUFFER_FRAMES;
    if (next == rd) {
        // buffer full — drop sample
        return;
    }
    ring[w * 2]     = l;
    ring[w * 2 + 1] = r;
    writeIdx.store(next, std::memory_order_release);
}

void APU::step(int cycles) {
    if (!powered) {
        // Still produce silent samples to keep audio flowing.
        sampleAccum += cycles;
        const double period = static_cast<double>(CPU_FREQ) / SAMPLE_RATE;
        while (sampleAccum >= period) {
            sampleAccum -= period;
            pushSample(0, 0);
        }
        return;
    }

    // Channel timers
    tickSquare(ch1, cycles);
    tickSquare(ch2, cycles);
    tickWave(ch3, cycles);
    tickNoise(ch4, cycles);

    // Frame sequencer @ 512 Hz: every 8192 CPU cycles.
    frameSeqCounter += cycles;
    while (frameSeqCounter >= 8192) {
        frameSeqCounter -= 8192;
        stepFrameSequencer();
    }

    // Sampling
    sampleAccum += cycles;
    const double period = static_cast<double>(CPU_FREQ) / SAMPLE_RATE;
    while (sampleAccum >= period) {
        sampleAccum -= period;
        produceSample();
    }
}

void APU::stepFrameSequencer() {
    switch (frameSeqStep) {
        case 0: clockLength(); break;
        case 1: break;
        case 2: clockLength(); clockSweep(); break;
        case 3: break;
        case 4: clockLength(); break;
        case 5: break;
        case 6: clockLength(); clockSweep(); break;
        case 7: clockEnvelope(); break;
    }
    frameSeqStep = (frameSeqStep + 1) & 7;
}

void APU::clockLength() {
    auto clk = [](int& len, bool enabled, bool& ch) {
        if (enabled && len > 0) {
            if (--len == 0) ch = false;
        }
    };
    clk(ch1.lengthCounter, ch1.lengthEnabled, ch1.enabled);
    clk(ch2.lengthCounter, ch2.lengthEnabled, ch2.enabled);
    clk(ch3.lengthCounter, ch3.lengthEnabled, ch3.enabled);
    clk(ch4.lengthCounter, ch4.lengthEnabled, ch4.enabled);
}

void APU::clockEnvelope() {
    auto env = [](int& counter, int period, int& vol, bool inc, bool enabled) {
        if (!enabled || period == 0) return;
        if (--counter <= 0) {
            counter = period;
            if (inc && vol < 15) vol++;
            else if (!inc && vol > 0) vol--;
        }
    };
    env(ch1.envCounter, ch1.envPeriod, ch1.volume, ch1.envIncreasing, ch1.enabled);
    env(ch2.envCounter, ch2.envPeriod, ch2.volume, ch2.envIncreasing, ch2.enabled);
    env(ch4.envCounter, ch4.envPeriod, ch4.volume, ch4.envIncreasing, ch4.enabled);
}

int APU::sweepCalc(bool& overflow) {
    int shadow = ch1.sweepShadow;
    int delta = shadow >> ch1.sweepShift;
    int newF = ch1.sweepNegate ? (shadow - delta) : (shadow + delta);
    if (ch1.sweepNegate) ch1.sweepNegCalcd = true;
    overflow = (newF > 2047);
    return newF;
}

void APU::clockSweep() {
    if (!ch1.enabled) return;
    if (--ch1.sweepCounter <= 0) {
        ch1.sweepCounter = ch1.sweepPeriod ? ch1.sweepPeriod : 8;
        if (ch1.sweepEnabled && ch1.sweepPeriod > 0) {
            bool ov;
            int newF = sweepCalc(ov);
            if (ov) {
                ch1.enabled = false;
            } else if (ch1.sweepShift > 0) {
                ch1.sweepShadow = newF;
                ch1.frequency = newF;
                bool ov2;
                sweepCalc(ov2);
                if (ov2) ch1.enabled = false;
            }
        }
    }
}

void APU::tickSquare(Square& c, int cycles) {
    if (!c.enabled) return;
    c.freqTimer -= cycles;
    while (c.freqTimer <= 0) {
        int period = (2048 - c.frequency) * 4;
        if (period <= 0) period = 1;
        c.freqTimer += period;
        c.dutyPos = (c.dutyPos + 1) & 7;
    }
}

void APU::tickWave(Wave& c, int cycles) {
    if (!c.enabled) return;
    c.freqTimer -= cycles;
    while (c.freqTimer <= 0) {
        int period = (2048 - c.frequency) * 2;
        if (period <= 0) period = 1;
        c.freqTimer += period;
        c.wavePos = (c.wavePos + 1) & 31;
        uint8_t byte = c.waveRAM[c.wavePos >> 1];
        c.sampleBuffer = (c.wavePos & 1) ? (byte & 0x0F) : (byte >> 4);
    }
}

void APU::tickNoise(Noise& c, int cycles) {
    if (!c.enabled) return;
    c.freqTimer -= cycles;
    while (c.freqTimer <= 0) {
        int period = NOISE_DIVISORS[c.divisorCode] << c.shift;
        if (period <= 0) period = 1;
        c.freqTimer += period;
        uint16_t bit = ((c.lfsr ^ (c.lfsr >> 1)) & 1);
        c.lfsr >>= 1;
        c.lfsr |= bit << 14;
        if (c.widthMode) {
            c.lfsr = (c.lfsr & ~(1 << 6)) | (bit << 6);
        }
    }
}

int APU::squareOutput(const Square& c) const {
    if (!c.enabled || !c.dacOn) return 0;
    int sample = (DUTY_TABLE[c.duty] >> (7 - c.dutyPos)) & 1;
    return sample * c.volume;
}

int APU::waveOutput(const Wave& c) const {
    if (!c.enabled || !c.dacOn) return 0;
    int sample = c.sampleBuffer;
    switch (c.volumeCode) {
        case 0: return 0;
        case 1: return sample;
        case 2: return sample >> 1;
        case 3: return sample >> 2;
    }
    return 0;
}

int APU::noiseOutput(const Noise& c) const {
    if (!c.enabled || !c.dacOn) return 0;
    int sample = (~c.lfsr) & 1;
    return sample * c.volume;
}

void APU::produceSample() {
    int s1 = squareOutput(ch1);
    int s2 = squareOutput(ch2);
    int s3 = waveOutput(ch3);
    int s4 = noiseOutput(ch4);

    int left = 0, right = 0;
    if (nr51 & 0x10) left  += s1;
    if (nr51 & 0x20) left  += s2;
    if (nr51 & 0x40) left  += s3;
    if (nr51 & 0x80) left  += s4;
    if (nr51 & 0x01) right += s1;
    if (nr51 & 0x02) right += s2;
    if (nr51 & 0x04) right += s3;
    if (nr51 & 0x08) right += s4;

    int leftVol  = ((nr50 >> 4) & 0x07) + 1;
    int rightVol = (nr50 & 0x07) + 1;

    // Each channel: 0..15. Sum 0..60. Center on 0, scale.
    // Convert to signed centered around 0: subtract midline only if any channel is on?
    // Standard approach: treat each sample as 0..15 (unsigned DAC), mix unsigned, then convert.
    // We'll center: ((sample/15)*2 - 1) * volume * masterVol
    // Approximate with fixed-point: amp ~= 2200 per channel max.
    int amp = 380; // per-channel scale; 4 channels * 15 * 380 * 8 ~ 182400, fits in 16-bit signed if /4
    int outL = left  * leftVol  * amp / 32;
    int outR = right * rightVol * amp / 32;
    if (muted) { outL = 0; outR = 0; }
    // Clamp
    if (outL >  32000) outL =  32000;
    if (outL < -32000) outL = -32000;
    if (outR >  32000) outR =  32000;
    if (outR < -32000) outR = -32000;

    pushSample(static_cast<int16_t>(outL), static_cast<int16_t>(outR));
}

void APU::triggerCh1() {
    ch1.enabled = ch1.dacOn;
    if (ch1.lengthCounter == 0) ch1.lengthCounter = 64;
    ch1.freqTimer = (2048 - ch1.frequency) * 4;
    ch1.envCounter = ch1.envPeriod ? ch1.envPeriod : 8;
    ch1.volume = ch1.envInitVol;
    ch1.sweepShadow = ch1.frequency;
    ch1.sweepCounter = ch1.sweepPeriod ? ch1.sweepPeriod : 8;
    ch1.sweepEnabled = (ch1.sweepPeriod != 0) || (ch1.sweepShift != 0);
    ch1.sweepNegCalcd = false;
    if (ch1.sweepShift > 0) {
        bool ov;
        sweepCalc(ov);
        if (ov) ch1.enabled = false;
    }
}

void APU::triggerCh2() {
    ch2.enabled = ch2.dacOn;
    if (ch2.lengthCounter == 0) ch2.lengthCounter = 64;
    ch2.freqTimer = (2048 - ch2.frequency) * 4;
    ch2.envCounter = ch2.envPeriod ? ch2.envPeriod : 8;
    ch2.volume = ch2.envInitVol;
}

void APU::triggerCh3() {
    ch3.enabled = ch3.dacOn;
    if (ch3.lengthCounter == 0) ch3.lengthCounter = 256;
    ch3.freqTimer = (2048 - ch3.frequency) * 2;
    ch3.wavePos = 0;
    ch3.sampleBuffer = 0;
}

void APU::triggerCh4() {
    ch4.enabled = ch4.dacOn;
    if (ch4.lengthCounter == 0) ch4.lengthCounter = 64;
    ch4.lfsr = 0x7FFF;
    int period = NOISE_DIVISORS[ch4.divisorCode] << ch4.shift;
    ch4.freqTimer = period > 0 ? period : 1;
    ch4.envCounter = ch4.envPeriod ? ch4.envPeriod : 8;
    ch4.volume = ch4.envInitVol;
}

uint8_t APU::readRegister(uint8_t reg) const {
    // Wave RAM is always readable.
    if (reg >= 0x30 && reg <= 0x3F) {
        return ch3.waveRAM[reg - 0x30];
    }
    switch (reg) {
        case 0x10: { // NR10
            uint8_t v = (ch1.sweepPeriod << 4) | (ch1.sweepNegate ? 0x08 : 0) | ch1.sweepShift;
            return v | 0x80;
        }
        case 0x11: return (ch1.duty << 6) | 0x3F;
        case 0x12: return (ch1.envInitVol << 4) | (ch1.envIncreasing ? 0x08 : 0) | ch1.envPeriod;
        case 0x13: return 0xFF;
        case 0x14: return (ch1.lengthEnabled ? 0x40 : 0) | 0xBF;

        case 0x16: return (ch2.duty << 6) | 0x3F;
        case 0x17: return (ch2.envInitVol << 4) | (ch2.envIncreasing ? 0x08 : 0) | ch2.envPeriod;
        case 0x18: return 0xFF;
        case 0x19: return (ch2.lengthEnabled ? 0x40 : 0) | 0xBF;

        case 0x1A: return (ch3.dacOn ? 0x80 : 0) | 0x7F;
        case 0x1B: return 0xFF;
        case 0x1C: return (ch3.volumeCode << 5) | 0x9F;
        case 0x1D: return 0xFF;
        case 0x1E: return (ch3.lengthEnabled ? 0x40 : 0) | 0xBF;

        case 0x20: return 0xFF;
        case 0x21: return (ch4.envInitVol << 4) | (ch4.envIncreasing ? 0x08 : 0) | ch4.envPeriod;
        case 0x22: return (ch4.shift << 4) | (ch4.widthMode ? 0x08 : 0) | ch4.divisorCode;
        case 0x23: return (ch4.lengthEnabled ? 0x40 : 0) | 0xBF;

        case 0x24: return nr50;
        case 0x25: return nr51;
        case 0x26: {
            uint8_t v = 0x70;
            if (powered)       v |= 0x80;
            if (ch1.enabled)   v |= 0x01;
            if (ch2.enabled)   v |= 0x02;
            if (ch3.enabled)   v |= 0x04;
            if (ch4.enabled)   v |= 0x08;
            return v;
        }
    }
    return 0xFF;
}

void APU::writeRegister(uint8_t reg, uint8_t val) {
    if (reg >= 0x30 && reg <= 0x3F) {
        ch3.waveRAM[reg - 0x30] = val;
        return;
    }
    if (!powered && reg != 0x26 && !(reg >= 0x11 && reg <= 0x11) /* allow length on DMG */) {
        // When powered off, only NR52 and length-load registers writable on DMG.
        // For simplicity we still ignore most writes.
        if (reg != 0x11 && reg != 0x16 && reg != 0x1B && reg != 0x20) return;
    }

    switch (reg) {
        case 0x10:
            ch1.sweepPeriod = (val >> 4) & 0x07;
            ch1.sweepNegate = (val & 0x08) != 0;
            ch1.sweepShift  = val & 0x07;
            // If sweep was negate-calc'd and we clear negate, disable channel.
            if (ch1.sweepNegCalcd && !ch1.sweepNegate) ch1.enabled = false;
            break;
        case 0x11:
            ch1.duty = (val >> 6) & 0x03;
            ch1.lengthCounter = 64 - (val & 0x3F);
            break;
        case 0x12:
            ch1.envInitVol    = (val >> 4) & 0x0F;
            ch1.envIncreasing = (val & 0x08) != 0;
            ch1.envPeriod     = val & 0x07;
            ch1.dacOn = (val & 0xF8) != 0;
            if (!ch1.dacOn) ch1.enabled = false;
            break;
        case 0x13:
            ch1.frequency = (ch1.frequency & 0x700) | val;
            break;
        case 0x14:
            ch1.frequency = (ch1.frequency & 0xFF) | ((val & 0x07) << 8);
            ch1.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerCh1();
            break;

        case 0x16:
            ch2.duty = (val >> 6) & 0x03;
            ch2.lengthCounter = 64 - (val & 0x3F);
            break;
        case 0x17:
            ch2.envInitVol    = (val >> 4) & 0x0F;
            ch2.envIncreasing = (val & 0x08) != 0;
            ch2.envPeriod     = val & 0x07;
            ch2.dacOn = (val & 0xF8) != 0;
            if (!ch2.dacOn) ch2.enabled = false;
            break;
        case 0x18:
            ch2.frequency = (ch2.frequency & 0x700) | val;
            break;
        case 0x19:
            ch2.frequency = (ch2.frequency & 0xFF) | ((val & 0x07) << 8);
            ch2.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerCh2();
            break;

        case 0x1A:
            ch3.dacOn = (val & 0x80) != 0;
            if (!ch3.dacOn) ch3.enabled = false;
            break;
        case 0x1B:
            ch3.lengthCounter = 256 - val;
            break;
        case 0x1C:
            ch3.volumeCode = (val >> 5) & 0x03;
            break;
        case 0x1D:
            ch3.frequency = (ch3.frequency & 0x700) | val;
            break;
        case 0x1E:
            ch3.frequency = (ch3.frequency & 0xFF) | ((val & 0x07) << 8);
            ch3.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerCh3();
            break;

        case 0x20:
            ch4.lengthCounter = 64 - (val & 0x3F);
            break;
        case 0x21:
            ch4.envInitVol    = (val >> 4) & 0x0F;
            ch4.envIncreasing = (val & 0x08) != 0;
            ch4.envPeriod     = val & 0x07;
            ch4.dacOn = (val & 0xF8) != 0;
            if (!ch4.dacOn) ch4.enabled = false;
            break;
        case 0x22:
            ch4.shift       = (val >> 4) & 0x0F;
            ch4.widthMode   = (val & 0x08) != 0;
            ch4.divisorCode = val & 0x07;
            break;
        case 0x23:
            ch4.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerCh4();
            break;

        case 0x24: nr50 = val; break;
        case 0x25: nr51 = val; break;
        case 0x26: {
            bool wasPowered = powered;
            powered = (val & 0x80) != 0;
            if (wasPowered && !powered) {
                // Clear all registers (DMG behavior: clear all except wave RAM, length on DMG).
                for (uint8_t r = 0x10; r <= 0x25; ++r) {
                    writeRegister(r, 0);
                }
                ch1 = Square{};
                ch2 = Square{};
                ch3.enabled = false;
                ch3.dacOn = false;
                ch3.volumeCode = 0;
                ch3.lengthEnabled = false;
                ch3.frequency = 0;
                ch4 = Noise{};
                ch4.lfsr = 0x7FFF;
                nr50 = 0;
                nr51 = 0;
            } else if (!wasPowered && powered) {
                frameSeqStep = 0;
                ch1.dutyPos = 0;
                ch2.dutyPos = 0;
                ch3.wavePos = 0;
            }
            break;
        }
    }
}
