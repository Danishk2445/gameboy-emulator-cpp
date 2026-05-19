#pragma once

#include <cstdint>
#include <iosfwd>

class Memory;

constexpr uint8_t INT_VBLANK = 0x01;
constexpr uint8_t INT_STAT   = 0x02;
constexpr uint8_t INT_TIMER  = 0x04;
constexpr uint8_t INT_SERIAL = 0x08;
constexpr uint8_t INT_JOYPAD = 0x10;

class CPU {
public:
    explicit CPU(Memory& mem);

    void reset();
    int  step();
    void requestInterrupt(uint8_t mask);
    bool isHalted() const { return halted; }

    void saveState(std::ostream& out) const;
    bool loadState(std::istream& in);

private:
    Memory& memory;

    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };
    uint16_t sp = 0;
    uint16_t pc = 0;

    bool ime = false;
    bool imeScheduled = false;
    bool halted = false;
    bool stopped = false;

    bool getZ() const { return (f & 0x80) != 0; }
    bool getN() const { return (f & 0x40) != 0; }
    bool getH() const { return (f & 0x20) != 0; }
    bool getC() const { return (f & 0x10) != 0; }
    void setZ(bool v) { f = v ? (f | 0x80) : (f & ~0x80); }
    void setN(bool v) { f = v ? (f | 0x40) : (f & ~0x40); }
    void setH(bool v) { f = v ? (f | 0x20) : (f & ~0x20); }
    void setC(bool v) { f = v ? (f | 0x10) : (f & ~0x10); }
    void setFlags(bool z, bool n, bool hf, bool cf) {
        f = (z ? 0x80 : 0) | (n ? 0x40 : 0) | (hf ? 0x20 : 0) | (cf ? 0x10 : 0);
    }

    uint8_t  read8(uint16_t addr);
    void     write8(uint16_t addr, uint8_t val);
    uint16_t read16(uint16_t addr);
    void     write16(uint16_t addr, uint16_t val);
    uint8_t  fetch8();
    uint16_t fetch16();
    void     push16(uint16_t val);
    uint16_t pop16();

    int  handleInterrupts();
    int  execute(uint8_t op);
    int  executeCB();

    void add8(uint8_t v);
    void adc8(uint8_t v);
    void sub8(uint8_t v);
    void sbc8(uint8_t v);
    void and8(uint8_t v);
    void or8(uint8_t v);
    void xor8(uint8_t v);
    void cp8(uint8_t v);
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    void    add16hl(uint16_t v);
    uint16_t addSP(int8_t v);
    void    daa();

    uint8_t rlc(uint8_t v);
    uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);
    uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v);
    uint8_t sra(uint8_t v);
    uint8_t swap(uint8_t v);
    uint8_t srl(uint8_t v);
    void    bit(uint8_t b, uint8_t v);
};
