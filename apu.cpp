#include "apu.h"
#include <cstring>
#include <algorithm>
#include <cmath>

APU::APU() : audioDevice(0), writePos(0), readPos(0), masterEnable(true) {
    reset();
}

APU::~APU() {
    if (audioDevice) {
        SDL_CloseAudioDevice(audioDevice);
    }
}

bool APU::init() {
    SDL_AudioSpec want, have;
    
    SDL_memset(&want, 0, sizeof(want));
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = audioCallback;
    want.userdata = this;
    
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDevice == 0) {
        return false;
    }
    
    SDL_PauseAudioDevice(audioDevice, 0);
    return true;
}

void APU::reset() {
    registers.fill(0);
    waveRam.fill(0);
    ringBuffer.fill(0);
    
    memset(&ch1, 0, sizeof(ch1));
    memset(&ch2, 0, sizeof(ch2));
    memset(&ch3, 0, sizeof(ch3));
    memset(&ch4, 0, sizeof(ch4));
    
    ch4.lfsr = 0x7FFF;
    
    frameSequencerCycles = 0;
    frameSequencerStep = 0;
    sampleAccumulator = 0;
    
    writePos = 0;
    readPos = 0;
    
    masterEnable = true;
    masterVolume = 0x77;
    channelPan = 0xFF;
}

void APU::writeRegister(uint8_t reg, uint8_t val) {
    if (reg >= 0x30 && reg <= 0x3F) {
        waveRam[reg - 0x30] = val;
        return;
    }
    
    if (reg < 0x10 || reg > 0x26) return;
    
    registers[reg - 0x10] = val;
    
    switch (reg) {
        // Channel 1 - Square with sweep
        case 0x10:  // NR10 - Sweep
            ch1.sweepPeriod = (val >> 4) & 0x07;
            ch1.sweepNegate = (val & 0x08) != 0;
            ch1.sweepShift = val & 0x07;
            break;
            
        case 0x11:  // NR11 - Length/Duty
            ch1.duty = (val >> 6) & 0x03;
            ch1.lengthCounter = 64 - (val & 0x3F);
            break;
            
        case 0x12:  // NR12 - Volume envelope
            ch1.volumeInit = (val >> 4) & 0x0F;
            ch1.envelopeAdd = (val & 0x08) != 0;
            ch1.envelopePeriod = val & 0x07;
            if ((val & 0xF8) == 0) ch1.enabled = false;
            break;
            
        case 0x13:  // NR13 - Frequency low
            ch1.frequency = (ch1.frequency & 0x700) | val;
            break;
            
        case 0x14:  // NR14 - Frequency high + trigger
            ch1.frequency = (ch1.frequency & 0xFF) | ((val & 0x07) << 8);
            ch1.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerChannel1();
            break;
            
        // Channel 2 - Square
        case 0x16:  // NR21 - Length/Duty
            ch2.duty = (val >> 6) & 0x03;
            ch2.lengthCounter = 64 - (val & 0x3F);
            break;
            
        case 0x17:  // NR22 - Volume envelope
            ch2.volumeInit = (val >> 4) & 0x0F;
            ch2.envelopeAdd = (val & 0x08) != 0;
            ch2.envelopePeriod = val & 0x07;
            if ((val & 0xF8) == 0) ch2.enabled = false;
            break;
            
        case 0x18:  // NR23 - Frequency low
            ch2.frequency = (ch2.frequency & 0x700) | val;
            break;
            
        case 0x19:  // NR24 - Frequency high + trigger
            ch2.frequency = (ch2.frequency & 0xFF) | ((val & 0x07) << 8);
            ch2.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerChannel2();
            break;
            
        // Channel 3 - Wave
        case 0x1A:  // NR30 - DAC enable
            ch3.dacEnabled = (val & 0x80) != 0;
            if (!ch3.dacEnabled) ch3.enabled = false;
            break;
            
        case 0x1B:  // NR31 - Length
            ch3.lengthCounter = 256 - val;
            break;
            
        case 0x1C:  // NR32 - Volume
            ch3.volume = (val >> 5) & 0x03;
            break;
            
        case 0x1D:  // NR33 - Frequency low
            ch3.frequency = (ch3.frequency & 0x700) | val;
            break;
            
        case 0x1E:  // NR34 - Frequency high + trigger
            ch3.frequency = (ch3.frequency & 0xFF) | ((val & 0x07) << 8);
            ch3.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerChannel3();
            break;
            
        // Channel 4 - Noise
        case 0x20:  // NR41 - Length
            ch4.lengthCounter = 64 - (val & 0x3F);
            break;
            
        case 0x21:  // NR42 - Volume envelope
            ch4.volumeInit = (val >> 4) & 0x0F;
            ch4.envelopeAdd = (val & 0x08) != 0;
            ch4.envelopePeriod = val & 0x07;
            if ((val & 0xF8) == 0) ch4.enabled = false;
            break;
            
        case 0x22:  // NR43 - Polynomial counter
            ch4.shift = (val >> 4) & 0x0F;
            ch4.widthMode = (val & 0x08) != 0;
            ch4.divisor = val & 0x07;
            break;
            
        case 0x23:  // NR44 - Trigger
            ch4.lengthEnabled = (val & 0x40) != 0;
            if (val & 0x80) triggerChannel4();
            break;
            
        // Master control
        case 0x24:  // NR50 - Master volume
            masterVolume = val;
            break;
            
        case 0x25:  // NR51 - Channel panning
            channelPan = val;
            break;
            
        case 0x26:  // NR52 - Sound on/off
            masterEnable = (val & 0x80) != 0;
            if (!masterEnable) {
                // Don't call reset() as it would reset the audio device
                ch1.enabled = ch2.enabled = ch3.enabled = ch4.enabled = false;
            }
            break;
    }
}

uint8_t APU::readRegister(uint8_t reg) const {
    if (reg >= 0x30 && reg <= 0x3F) {
        return waveRam[reg - 0x30];
    }
    
    if (reg < 0x10 || reg > 0x26) return 0xFF;
    
    // NR52 - Sound on/off status
    if (reg == 0x26) {
        uint8_t status = masterEnable ? 0x80 : 0x00;
        if (ch1.enabled) status |= 0x01;
        if (ch2.enabled) status |= 0x02;
        if (ch3.enabled) status |= 0x04;
        if (ch4.enabled) status |= 0x08;
        return status | 0x70;
    }
    
    return registers[reg - 0x10];
}

void APU::triggerChannel1() {
    ch1.enabled = true;
    if (ch1.lengthCounter == 0) ch1.lengthCounter = 64;
    ch1.frequencyTimer = (2048 - ch1.frequency) * 4;
    ch1.envelopeTimer = ch1.envelopePeriod;
    ch1.volume = ch1.volumeInit;
    ch1.shadowFreq = ch1.frequency;
    ch1.sweepTimer = ch1.sweepPeriod > 0 ? ch1.sweepPeriod : 8;
    ch1.sweepEnabled = ch1.sweepPeriod > 0 || ch1.sweepShift > 0;
    ch1.dutyPos = 0;
}

void APU::triggerChannel2() {
    ch2.enabled = true;
    if (ch2.lengthCounter == 0) ch2.lengthCounter = 64;
    ch2.frequencyTimer = (2048 - ch2.frequency) * 4;
    ch2.envelopeTimer = ch2.envelopePeriod;
    ch2.volume = ch2.volumeInit;
    ch2.dutyPos = 0;
}

void APU::triggerChannel3() {
    ch3.enabled = ch3.dacEnabled;
    if (ch3.lengthCounter == 0) ch3.lengthCounter = 256;
    ch3.frequencyTimer = (2048 - ch3.frequency) * 2;
    ch3.samplePos = 0;
}

void APU::triggerChannel4() {
    ch4.enabled = true;
    if (ch4.lengthCounter == 0) ch4.lengthCounter = 64;
    ch4.envelopeTimer = ch4.envelopePeriod;
    ch4.volume = ch4.volumeInit;
    ch4.lfsr = 0x7FFF;
    
    int divisors[] = { 8, 16, 32, 48, 64, 80, 96, 112 };
    ch4.frequencyTimer = divisors[ch4.divisor] << ch4.shift;
}

float APU::getChannel1Output() {
    if (!ch1.enabled) return 0.0f;
    
    uint8_t duty = dutyPatterns[ch1.duty];
    int bit = (duty >> (7 - ch1.dutyPos)) & 1;
    return bit ? (ch1.volume / 15.0f) : -(ch1.volume / 15.0f);
}

float APU::getChannel2Output() {
    if (!ch2.enabled) return 0.0f;
    
    uint8_t duty = dutyPatterns[ch2.duty];
    int bit = (duty >> (7 - ch2.dutyPos)) & 1;
    return bit ? (ch2.volume / 15.0f) : -(ch2.volume / 15.0f);
}

float APU::getChannel3Output() {
    if (!ch3.enabled || !ch3.dacEnabled) return 0.0f;
    
    uint8_t sample = waveRam[ch3.samplePos / 2];
    if ((ch3.samplePos & 1) == 0) {
        sample = (sample >> 4) & 0x0F;
    } else {
        sample = sample & 0x0F;
    }
    
    int shift = 0;
    switch (ch3.volume) {
        case 0: return 0.0f;
        case 1: shift = 0; break;
        case 2: shift = 1; break;
        case 3: shift = 2; break;
    }
    
    float out = ((sample >> shift) - 7.5f) / 7.5f;
    return out * 0.5f;  // Wave channel is quieter
}

float APU::getChannel4Output() {
    if (!ch4.enabled) return 0.0f;
    // LFSR bit 0 inverted gives the output
    return (ch4.lfsr & 1) ? -(ch4.volume / 15.0f) : (ch4.volume / 15.0f);
}

void APU::stepFrameSequencer() {
    // Length counter on steps 0, 2, 4, 6
    if ((frameSequencerStep & 1) == 0) {
        if (ch1.lengthEnabled && ch1.lengthCounter > 0) {
            if (--ch1.lengthCounter == 0) ch1.enabled = false;
        }
        if (ch2.lengthEnabled && ch2.lengthCounter > 0) {
            if (--ch2.lengthCounter == 0) ch2.enabled = false;
        }
        if (ch3.lengthEnabled && ch3.lengthCounter > 0) {
            if (--ch3.lengthCounter == 0) ch3.enabled = false;
        }
        if (ch4.lengthEnabled && ch4.lengthCounter > 0) {
            if (--ch4.lengthCounter == 0) ch4.enabled = false;
        }
    }
    
    // Sweep on steps 2, 6
    if (frameSequencerStep == 2 || frameSequencerStep == 6) {
        if (ch1.sweepEnabled && ch1.sweepPeriod > 0) {
            if (--ch1.sweepTimer <= 0) {
                ch1.sweepTimer = ch1.sweepPeriod > 0 ? ch1.sweepPeriod : 8;
                
                int newFreq = ch1.shadowFreq >> ch1.sweepShift;
                if (ch1.sweepNegate) {
                    newFreq = ch1.shadowFreq - newFreq;
                } else {
                    newFreq = ch1.shadowFreq + newFreq;
                }
                
                if (newFreq > 2047) {
                    ch1.enabled = false;
                } else if (ch1.sweepShift > 0) {
                    ch1.shadowFreq = newFreq;
                    ch1.frequency = newFreq;
                }
            }
        }
    }
    
    // Envelope on step 7
    if (frameSequencerStep == 7) {
        if (ch1.envelopePeriod > 0) {
            if (--ch1.envelopeTimer <= 0) {
                ch1.envelopeTimer = ch1.envelopePeriod;
                if (ch1.envelopeAdd && ch1.volume < 15) ch1.volume++;
                else if (!ch1.envelopeAdd && ch1.volume > 0) ch1.volume--;
            }
        }
        if (ch2.envelopePeriod > 0) {
            if (--ch2.envelopeTimer <= 0) {
                ch2.envelopeTimer = ch2.envelopePeriod;
                if (ch2.envelopeAdd && ch2.volume < 15) ch2.volume++;
                else if (!ch2.envelopeAdd && ch2.volume > 0) ch2.volume--;
            }
        }
        if (ch4.envelopePeriod > 0) {
            if (--ch4.envelopeTimer <= 0) {
                ch4.envelopeTimer = ch4.envelopePeriod;
                if (ch4.envelopeAdd && ch4.volume < 15) ch4.volume++;
                else if (!ch4.envelopeAdd && ch4.volume > 0) ch4.volume--;
            }
        }
    }
    
    frameSequencerStep = (frameSequencerStep + 1) & 7;
}

void APU::stepChannels() {
    // Channel 1
    if (ch1.frequencyTimer > 0) {
        ch1.frequencyTimer--;
    }
    if (ch1.frequencyTimer <= 0) {
        ch1.frequencyTimer = (2048 - ch1.frequency) * 4;
        ch1.dutyPos = (ch1.dutyPos + 1) & 7;
    }
    
    // Channel 2
    if (ch2.frequencyTimer > 0) {
        ch2.frequencyTimer--;
    }
    if (ch2.frequencyTimer <= 0) {
        ch2.frequencyTimer = (2048 - ch2.frequency) * 4;
        ch2.dutyPos = (ch2.dutyPos + 1) & 7;
    }
    
    // Channel 3
    if (ch3.frequencyTimer > 0) {
        ch3.frequencyTimer--;
    }
    if (ch3.frequencyTimer <= 0) {
        ch3.frequencyTimer = (2048 - ch3.frequency) * 2;
        ch3.samplePos = (ch3.samplePos + 1) & 31;
    }
    
    // Channel 4
    if (ch4.frequencyTimer > 0) {
        ch4.frequencyTimer--;
    }
    if (ch4.frequencyTimer <= 0) {
        int divisors[] = { 8, 16, 32, 48, 64, 80, 96, 112 };
        ch4.frequencyTimer = divisors[ch4.divisor] << ch4.shift;
        
        int xorResult = (ch4.lfsr & 1) ^ ((ch4.lfsr >> 1) & 1);
        ch4.lfsr = (ch4.lfsr >> 1) | (xorResult << 14);
        if (ch4.widthMode) {
            ch4.lfsr &= ~(1 << 6);
            ch4.lfsr |= xorResult << 6;
        }
    }
}

void APU::generateSample() {
    if (!masterEnable) {
        // Write silence
        int wp = writePos.load();
        int nextWp = (wp + 2) % (BUFFER_SIZE * 2);
        if (nextWp != readPos.load()) {
            ringBuffer[wp] = 0.0f;
            ringBuffer[wp + 1] = 0.0f;
            writePos.store(nextWp);
        }
        return;
    }
    
    float left = 0.0f, right = 0.0f;
    
    float ch1Out = getChannel1Output();
    float ch2Out = getChannel2Output();
    float ch3Out = getChannel3Output();
    float ch4Out = getChannel4Output();
    
    // Panning
    if (channelPan & 0x10) left += ch1Out;
    if (channelPan & 0x01) right += ch1Out;
    if (channelPan & 0x20) left += ch2Out;
    if (channelPan & 0x02) right += ch2Out;
    if (channelPan & 0x40) left += ch3Out;
    if (channelPan & 0x04) right += ch3Out;
    if (channelPan & 0x80) left += ch4Out;
    if (channelPan & 0x08) right += ch4Out;
    
    // Master volume (0-7, normalized)
    float leftVol = (((masterVolume >> 4) & 0x07) + 1) / 8.0f;
    float rightVol = ((masterVolume & 0x07) + 1) / 8.0f;
    
    left *= leftVol * 0.25f;  // Divide by 4 channels
    right *= rightVol * 0.25f;
    
    // Clamp
    left = std::max(-1.0f, std::min(1.0f, left));
    right = std::max(-1.0f, std::min(1.0f, right));
    
    // Write to ring buffer (stereo)
    int wp = writePos.load();
    int rp = readPos.load();
    int nextWp = (wp + 2) % (BUFFER_SIZE * 2);
    
    // Check if buffer has space
    if (nextWp != rp) {
        ringBuffer[wp] = left;
        ringBuffer[wp + 1] = right;
        writePos.store(nextWp);
    }
}

void APU::step(int cycles) {
    // Frame sequencer at 512 Hz (every 8192 cycles)
    frameSequencerCycles += cycles;
    while (frameSequencerCycles >= 8192) {
        frameSequencerCycles -= 8192;
        stepFrameSequencer();
    }
    
    // Step channels and generate samples
    for (int i = 0; i < cycles; i++) {
        stepChannels();
        
        // Generate sample at the right rate
        sampleAccumulator += SAMPLE_RATE;
        while (sampleAccumulator >= CPU_CLOCK) {
            sampleAccumulator -= CPU_CLOCK;
            generateSample();
        }
    }
}

void APU::audioCallback(void* userdata, Uint8* stream, int len) {
    APU* apu = static_cast<APU*>(userdata);
    float* output = reinterpret_cast<float*>(stream);
    int samples = len / sizeof(float);  // Total floats needed (stereo pairs)
    
    for (int i = 0; i < samples; i += 2) {
        int rp = apu->readPos.load();
        int wp = apu->writePos.load();
        
        if (rp != wp) {
            // Read from ring buffer
            output[i] = apu->ringBuffer[rp];
            output[i + 1] = apu->ringBuffer[rp + 1];
            apu->readPos.store((rp + 2) % (BUFFER_SIZE * 2));
        } else {
            // Buffer underrun - output silence
            output[i] = 0.0f;
            output[i + 1] = 0.0f;
        }
    }
}
