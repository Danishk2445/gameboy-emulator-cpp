#pragma once

#include <cstdint>
#include <array>
#include <atomic>
#include <SDL.h>

class Memory;

class APU {
public:
    APU();
    ~APU();
    
    // Initialize SDL audio
    bool init();
    
    // Step APU by given number of CPU cycles
    void step(int cycles);
    
    // Register write
    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg) const;
    
    // Reset APU state
    void reset();
    
private:
    // Audio device
    SDL_AudioDeviceID audioDevice;
    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int CPU_CLOCK = 4194304;
    
    // Sound registers (0xFF10-0xFF3F)
    std::array<uint8_t, 0x30> registers;
    
    // Channel state
    struct Channel1 {
        bool enabled;
        int frequencyTimer;
        int frequency;
        int duty;
        int dutyPos;
        int volume;
        int volumeInit;
        int envelopeTimer;
        int envelopePeriod;
        bool envelopeAdd;
        int lengthCounter;
        bool lengthEnabled;
        int sweepTimer;
        int sweepPeriod;
        bool sweepNegate;
        int sweepShift;
        int shadowFreq;
        bool sweepEnabled;
    } ch1;
    
    struct Channel2 {
        bool enabled;
        int frequencyTimer;
        int frequency;
        int duty;
        int dutyPos;
        int volume;
        int volumeInit;
        int envelopeTimer;
        int envelopePeriod;
        bool envelopeAdd;
        int lengthCounter;
        bool lengthEnabled;
    } ch2;
    
    struct Channel3 {
        bool enabled;
        bool dacEnabled;
        int frequencyTimer;
        int frequency;
        int volume;
        int lengthCounter;
        bool lengthEnabled;
        int samplePos;
    } ch3;
    
    struct Channel4 {
        bool enabled;
        int frequencyTimer;
        int volume;
        int volumeInit;
        int envelopeTimer;
        int envelopePeriod;
        bool envelopeAdd;
        int lengthCounter;
        bool lengthEnabled;
        uint16_t lfsr;
        int divisor;
        int shift;
        bool widthMode;
    } ch4;
    
    // Wave RAM (0xFF30-0xFF3F)
    std::array<uint8_t, 16> waveRam;
    
    // Frame sequencer
    int frameSequencerCycles;
    int frameSequencerStep;
    
    // Ring buffer for audio samples
    std::array<float, BUFFER_SIZE * 2> ringBuffer;
    std::atomic<int> writePos;
    std::atomic<int> readPos;
    
    // Sample accumulator for proper timing
    int sampleAccumulator;
    static constexpr int SAMPLES_PER_FRAME = CPU_CLOCK / SAMPLE_RATE;
    
    // Master control
    bool masterEnable;
    uint8_t masterVolume;
    uint8_t channelPan;  // NR51
    
    // Duty cycle patterns
    static constexpr uint8_t dutyPatterns[4] = {
        0b00000001,  // 12.5%
        0b10000001,  // 25%
        0b10000111,  // 50%
        0b01111110   // 75%
    };
    
    void stepFrameSequencer();
    void stepChannels();
    void generateSample();
    
    void triggerChannel1();
    void triggerChannel2();
    void triggerChannel3();
    void triggerChannel4();
    
    float getChannel1Output();
    float getChannel2Output();
    float getChannel3Output();
    float getChannel4Output();
    
    static void audioCallback(void* userdata, Uint8* stream, int len);
};
