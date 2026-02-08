#pragma once

#include <cstdint>

class Memory;

class CPU {
public:
    CPU(Memory& mem);
    
    // Execute one instruction and return cycles consumed
    int step();
    
    // Reset CPU to initial state
    void reset();
    
    // Request interrupt
    void requestInterrupt(uint8_t interrupt);
    
    // Check if CPU is halted
    bool isHalted() const { return halted; }
    
private:
    Memory& memory;
    
    // Registers
    union {
        struct {
            uint8_t f;  // Flags: Z N H C 0 0 0 0
            uint8_t a;
        };
        uint16_t af;
    };
    union {
        struct {
            uint8_t c;
            uint8_t b;
        };
        uint16_t bc;
    };
    union {
        struct {
            uint8_t e;
            uint8_t d;
        };
        uint16_t de;
    };
    union {
        struct {
            uint8_t l;
            uint8_t h;
        };
        uint16_t hl;
    };
    
    uint16_t sp;  // Stack pointer
    uint16_t pc;  // Program counter
    
    bool ime;     // Interrupt master enable
    bool imeScheduled;  // IME to be enabled after next instruction
    bool halted;
    bool stopped;
    
    // Flag helpers
    bool getZ() const { return (f & 0x80) != 0; }
    bool getN() const { return (f & 0x40) != 0; }
    bool getH() const { return (f & 0x20) != 0; }
    bool getC() const { return (f & 0x10) != 0; }
    
    void setZ(bool v) { f = v ? (f | 0x80) : (f & ~0x80); }
    void setN(bool v) { f = v ? (f | 0x40) : (f & ~0x40); }
    void setH(bool v) { f = v ? (f | 0x20) : (f & ~0x20); }
    void setC(bool v) { f = v ? (f | 0x10) : (f & ~0x10); }
    
    void setFlags(bool z, bool n, bool h, bool c) {
        f = (z ? 0x80 : 0) | (n ? 0x40 : 0) | (h ? 0x20 : 0) | (c ? 0x10 : 0);
    }
    
    // Memory access helpers
    uint8_t read8(uint16_t addr);
    void write8(uint16_t addr, uint8_t val);
    uint16_t read16(uint16_t addr);
    void write16(uint16_t addr, uint16_t val);
    
    uint8_t fetch8();
    uint16_t fetch16();
    
    void push16(uint16_t val);
    uint16_t pop16();
    
    // Handle interrupts
    int handleInterrupts();
    
    // Execute CB-prefixed instruction
    int executeCB();
    
    // ALU operations
    void add8(uint8_t val);
    void adc8(uint8_t val);
    void sub8(uint8_t val);
    void sbc8(uint8_t val);
    void and8(uint8_t val);
    void or8(uint8_t val);
    void xor8(uint8_t val);
    void cp8(uint8_t val);
    void inc8(uint8_t& reg);
    void dec8(uint8_t& reg);
    void add16hl(uint16_t val);
    void addSP(int8_t val);
    
    // Rotate/Shift operations
    uint8_t rlc(uint8_t val);
    uint8_t rrc(uint8_t val);
    uint8_t rl(uint8_t val);
    uint8_t rr(uint8_t val);
    uint8_t sla(uint8_t val);
    uint8_t sra(uint8_t val);
    uint8_t swap(uint8_t val);
    uint8_t srl(uint8_t val);
    void bit(uint8_t b, uint8_t val);
    uint8_t res(uint8_t b, uint8_t val);
    uint8_t set(uint8_t b, uint8_t val);
};

// Interrupt bits
constexpr uint8_t INT_VBLANK = 0x01;
constexpr uint8_t INT_STAT   = 0x02;
constexpr uint8_t INT_TIMER  = 0x04;
constexpr uint8_t INT_SERIAL = 0x08;
constexpr uint8_t INT_JOYPAD = 0x10;
