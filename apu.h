#pragma once

#include <cstdint>
#include <atomic>
#include <iosfwd>
#include <SDL.h>

class Memory;

class APU {
public:
    explicit APU(Memory& mem);
    ~APU();

    bool init();
    void reset();
    void step(int cycles);

    uint8_t readRegister(uint8_t reg) const;
    void    writeRegister(uint8_t reg, uint8_t val);

    void setMuted(bool m) { muted = m; }
    bool isMuted() const  { return muted; }

    void saveState(std::ostream& out) const;
    bool loadState(std::istream& in);

private:
    Memory& memory;

    SDL_AudioDeviceID device = 0;

    static constexpr int  SAMPLE_RATE   = 44100;
    static constexpr int  CPU_FREQ      = 4194304;
    static constexpr int  BUFFER_FRAMES = 4096;

    int16_t ring[BUFFER_FRAMES * 2]{};
    std::atomic<int> writeIdx{0};
    std::atomic<int> readIdx{0};

    int   frameSeqCounter = 0;
    int   frameSeqStep    = 0;
    double sampleAccum    = 0.0;

    bool powered = true;

    struct Square {
        bool    enabled       = false;
        bool    dacOn         = false;
        uint8_t duty          = 0;
        int     dutyPos       = 0;
        int     freqTimer     = 0;
        int     frequency     = 0;   // 11-bit
        int     lengthCounter = 0;
        bool    lengthEnabled = false;
        int     volume        = 0;
        int     envInitVol    = 0;
        bool    envIncreasing = false;
        int     envPeriod     = 0;
        int     envCounter    = 0;
        // sweep (ch1 only)
        int     sweepPeriod   = 0;
        int     sweepShift    = 0;
        bool    sweepNegate   = false;
        int     sweepCounter  = 0;
        int     sweepShadow   = 0;
        bool    sweepEnabled  = false;
        bool    sweepNegCalcd = false;
    };

    struct Wave {
        bool    enabled       = false;
        bool    dacOn         = false;
        int     lengthCounter = 0;
        bool    lengthEnabled = false;
        int     freqTimer     = 0;
        int     frequency     = 0;
        int     volumeCode    = 0;
        int     wavePos       = 0;
        uint8_t sampleBuffer  = 0;
        uint8_t waveRAM[16]{};
    };

    struct Noise {
        bool    enabled       = false;
        bool    dacOn         = false;
        int     lengthCounter = 0;
        bool    lengthEnabled = false;
        int     freqTimer     = 0;
        int     divisorCode   = 0;
        int     shift         = 0;
        bool    widthMode     = false;
        uint16_t lfsr         = 0x7FFF;
        int     volume        = 0;
        int     envInitVol    = 0;
        bool    envIncreasing = false;
        int     envPeriod     = 0;
        int     envCounter    = 0;
    };

    Square ch1{};
    Square ch2{};
    Wave   ch3{};
    Noise  ch4{};

    uint8_t nr50 = 0;
    uint8_t nr51 = 0;
    bool muted = false;

    void stepFrameSequencer();
    void clockLength();
    void clockEnvelope();
    void clockSweep();

    void tickSquare(Square& c, int cycles);
    void tickWave(Wave& c, int cycles);
    void tickNoise(Noise& c, int cycles);

    int  sweepCalc(bool& overflow);
    void triggerCh1();
    void triggerCh2();
    void triggerCh3();
    void triggerCh4();

    int squareOutput(const Square& c) const;
    int waveOutput(const Wave& c) const;
    int noiseOutput(const Noise& c) const;

    void produceSample();
    void pushSample(int16_t l, int16_t r);

    static void audioCallback(void* userdata, Uint8* stream, int len);
};
