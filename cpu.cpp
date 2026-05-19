#include "cpu.h"
#include "memory.h"

#include <cstdint>
#include <ostream>
#include <istream>

namespace {
struct CpuStateBlob {
    uint16_t af, bc, de, hl, sp, pc;
    uint8_t ime, imeScheduled, halted, stopped;
};
}

void CPU::saveState(std::ostream& out) const {
    CpuStateBlob s{};
    s.af = af; s.bc = bc; s.de = de; s.hl = hl; s.sp = sp; s.pc = pc;
    s.ime = ime ? 1 : 0;
    s.imeScheduled = imeScheduled ? 1 : 0;
    s.halted = halted ? 1 : 0;
    s.stopped = stopped ? 1 : 0;
    out.write(reinterpret_cast<const char*>(&s), sizeof(s));
}

bool CPU::loadState(std::istream& in) {
    CpuStateBlob s{};
    if (!in.read(reinterpret_cast<char*>(&s), sizeof(s))) return false;
    af = s.af; bc = s.bc; de = s.de; hl = s.hl; sp = s.sp; pc = s.pc;
    ime = s.ime != 0;
    imeScheduled = s.imeScheduled != 0;
    halted = s.halted != 0;
    stopped = s.stopped != 0;
    return true;
}

CPU::CPU(Memory& mem) : memory(mem) { reset(); }

void CPU::reset() {
    af = 0x01B0;
    bc = 0x0013;
    de = 0x00D8;
    hl = 0x014D;
    sp = 0xFFFE;
    pc = 0x0100;
    ime = false;
    imeScheduled = false;
    halted = false;
    stopped = false;
}

void CPU::requestInterrupt(uint8_t mask) {
    memory.setIF(memory.getIF() | mask);
}

uint8_t  CPU::read8(uint16_t a)        { return memory.read(a); }
void     CPU::write8(uint16_t a, uint8_t v) { memory.write(a, v); }
uint16_t CPU::read16(uint16_t a)       { return read8(a) | (uint16_t(read8(a + 1)) << 8); }
void     CPU::write16(uint16_t a, uint16_t v) { write8(a, v & 0xFF); write8(a + 1, v >> 8); }
uint8_t  CPU::fetch8()                 { return read8(pc++); }
uint16_t CPU::fetch16()                { uint16_t v = read16(pc); pc += 2; return v; }
void     CPU::push16(uint16_t v)       { sp -= 2; write16(sp, v); }
uint16_t CPU::pop16()                  { uint16_t v = read16(sp); sp += 2; return v; }

void CPU::add8(uint8_t v) {
    uint16_t r = a + v;
    setFlags((r & 0xFF) == 0, false, ((a & 0xF) + (v & 0xF)) > 0xF, r > 0xFF);
    a = r & 0xFF;
}
void CPU::adc8(uint8_t v) {
    uint8_t cf = getC() ? 1 : 0;
    uint16_t r = a + v + cf;
    setFlags((r & 0xFF) == 0, false, ((a & 0xF) + (v & 0xF) + cf) > 0xF, r > 0xFF);
    a = r & 0xFF;
}
void CPU::sub8(uint8_t v) {
    int r = int(a) - int(v);
    setFlags((r & 0xFF) == 0, true, (int(a & 0xF) - int(v & 0xF)) < 0, r < 0);
    a = r & 0xFF;
}
void CPU::sbc8(uint8_t v) {
    int cf = getC() ? 1 : 0;
    int r = int(a) - int(v) - cf;
    setFlags((r & 0xFF) == 0, true, (int(a & 0xF) - int(v & 0xF) - cf) < 0, r < 0);
    a = r & 0xFF;
}
void CPU::and8(uint8_t v) { a &= v; setFlags(a == 0, false, true,  false); }
void CPU::or8 (uint8_t v) { a |= v; setFlags(a == 0, false, false, false); }
void CPU::xor8(uint8_t v) { a ^= v; setFlags(a == 0, false, false, false); }
void CPU::cp8 (uint8_t v) {
    int r = int(a) - int(v);
    setFlags((r & 0xFF) == 0, true, (int(a & 0xF) - int(v & 0xF)) < 0, r < 0);
}
uint8_t CPU::inc8(uint8_t v) {
    uint8_t r = v + 1;
    setZ(r == 0); setN(false); setH((v & 0xF) == 0xF);
    return r;
}
uint8_t CPU::dec8(uint8_t v) {
    uint8_t r = v - 1;
    setZ(r == 0); setN(true); setH((v & 0xF) == 0);
    return r;
}
void CPU::add16hl(uint16_t v) {
    uint32_t r = uint32_t(hl) + v;
    setN(false);
    setH(((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF);
    setC(r > 0xFFFF);
    hl = r & 0xFFFF;
}
uint16_t CPU::addSP(int8_t v) {
    uint16_t s = sp;
    uint16_t r = s + v;
    uint16_t check = s ^ uint16_t(v) ^ r;
    setFlags(false, false, (check & 0x10) != 0, (check & 0x100) != 0);
    return r;
}
void CPU::daa() {
    int aval = a;
    if (!getN()) {
        if (getC() || aval > 0x99) { aval += 0x60; setC(true); }
        if (getH() || (aval & 0x0F) > 0x09) aval += 0x06;
    } else {
        if (getC()) aval -= 0x60;
        if (getH()) aval -= 0x06;
    }
    a = aval & 0xFF;
    setZ(a == 0);
    setH(false);
}

uint8_t CPU::rlc(uint8_t v) {
    uint8_t r = uint8_t((v << 1) | (v >> 7));
    setFlags(r == 0, false, false, (v & 0x80) != 0);
    return r;
}
uint8_t CPU::rrc(uint8_t v) {
    uint8_t r = uint8_t((v >> 1) | (v << 7));
    setFlags(r == 0, false, false, (v & 0x01) != 0);
    return r;
}
uint8_t CPU::rl(uint8_t v) {
    uint8_t cf = getC() ? 1 : 0;
    uint8_t r = uint8_t((v << 1) | cf);
    setFlags(r == 0, false, false, (v & 0x80) != 0);
    return r;
}
uint8_t CPU::rr(uint8_t v) {
    uint8_t cf = getC() ? 0x80 : 0;
    uint8_t r = uint8_t((v >> 1) | cf);
    setFlags(r == 0, false, false, (v & 0x01) != 0);
    return r;
}
uint8_t CPU::sla(uint8_t v) {
    uint8_t r = uint8_t(v << 1);
    setFlags(r == 0, false, false, (v & 0x80) != 0);
    return r;
}
uint8_t CPU::sra(uint8_t v) {
    uint8_t r = uint8_t((v >> 1) | (v & 0x80));
    setFlags(r == 0, false, false, (v & 0x01) != 0);
    return r;
}
uint8_t CPU::swap(uint8_t v) {
    uint8_t r = uint8_t((v >> 4) | (v << 4));
    setFlags(r == 0, false, false, false);
    return r;
}
uint8_t CPU::srl(uint8_t v) {
    uint8_t r = v >> 1;
    setFlags(r == 0, false, false, (v & 0x01) != 0);
    return r;
}
void CPU::bit(uint8_t b, uint8_t v) {
    setZ((v & (1u << b)) == 0);
    setN(false);
    setH(true);
}

int CPU::handleInterrupts() {
    uint8_t pending = memory.getIF() & memory.getIE() & 0x1F;
    if (pending == 0) return 0;
    halted = false;
    if (!ime) return 0;
    for (int i = 0; i < 5; ++i) {
        uint8_t mask = 1u << i;
        if (pending & mask) {
            ime = false;
            memory.setIF(memory.getIF() & ~mask);
            push16(pc);
            pc = 0x0040 + i * 0x08;
            return 20;
        }
    }
    return 0;
}

int CPU::step() {
    if (halted) {
        if ((memory.getIF() & memory.getIE() & 0x1F) != 0) {
            halted = false;
        } else {
            int c = handleInterrupts();
            return c > 0 ? c : 4;
        }
    }

    int ic = handleInterrupts();
    if (ic > 0) return ic;

    bool wasImeScheduled = imeScheduled;
    uint8_t op = fetch8();
    int cycles = execute(op);
    if (wasImeScheduled && imeScheduled) {
        ime = true;
        imeScheduled = false;
    }
    return cycles;
}

int CPU::execute(uint8_t op) {
    // Lambdas to read/write the standard 8-bit operand index.
    auto rdR = [&](int i) -> uint8_t {
        switch (i) {
            case 0: return b;
            case 1: return c;
            case 2: return d;
            case 3: return e;
            case 4: return h;
            case 5: return l;
            case 6: return read8(hl);
            case 7: return a;
        }
        return 0;
    };
    auto wrR = [&](int i, uint8_t v) {
        switch (i) {
            case 0: b = v; return;
            case 1: c = v; return;
            case 2: d = v; return;
            case 3: e = v; return;
            case 4: h = v; return;
            case 5: l = v; return;
            case 6: write8(hl, v); return;
            case 7: a = v; return;
        }
    };

    // 0x40-0x7F: LD r,r (0x76 = HALT)
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) { halted = true; return 4; }
        int dst = (op >> 3) & 0x07;
        int src = op & 0x07;
        wrR(dst, rdR(src));
        return ((dst == 6) || (src == 6)) ? 8 : 4;
    }

    // 0x80-0xBF: ALU r
    if (op >= 0x80 && op <= 0xBF) {
        int src = op & 0x07;
        uint8_t v = rdR(src);
        int alu = (op >> 3) & 0x07;
        switch (alu) {
            case 0: add8(v); break;
            case 1: adc8(v); break;
            case 2: sub8(v); break;
            case 3: sbc8(v); break;
            case 4: and8(v); break;
            case 5: xor8(v); break;
            case 6: or8(v);  break;
            case 7: cp8(v);  break;
        }
        return (src == 6) ? 8 : 4;
    }

    switch (op) {
        case 0x00: return 4; // NOP
        case 0x10: pc++; stopped = true; return 4; // STOP (skip following byte)

        // 16-bit immediate loads
        case 0x01: bc = fetch16(); return 12;
        case 0x11: de = fetch16(); return 12;
        case 0x21: hl = fetch16(); return 12;
        case 0x31: sp = fetch16(); return 12;

        // LD (rr),A / LD A,(rr) / LD (HL+/-),A / LD A,(HL+/-)
        case 0x02: write8(bc, a); return 8;
        case 0x12: write8(de, a); return 8;
        case 0x22: write8(hl++, a); return 8;
        case 0x32: write8(hl--, a); return 8;
        case 0x0A: a = read8(bc); return 8;
        case 0x1A: a = read8(de); return 8;
        case 0x2A: a = read8(hl++); return 8;
        case 0x3A: a = read8(hl--); return 8;

        // LD (nn),SP
        case 0x08: { uint16_t addr = fetch16(); write16(addr, sp); return 20; }

        // INC rr / DEC rr (no flags)
        case 0x03: bc++; return 8;
        case 0x13: de++; return 8;
        case 0x23: hl++; return 8;
        case 0x33: sp++; return 8;
        case 0x0B: bc--; return 8;
        case 0x1B: de--; return 8;
        case 0x2B: hl--; return 8;
        case 0x3B: sp--; return 8;

        // INC r / DEC r and immediates handled by pattern:
        case 0x04: b = inc8(b); return 4;
        case 0x14: d = inc8(d); return 4;
        case 0x24: h = inc8(h); return 4;
        case 0x34: { uint8_t v = read8(hl); write8(hl, inc8(v)); return 12; }
        case 0x0C: c = inc8(c); return 4;
        case 0x1C: e = inc8(e); return 4;
        case 0x2C: l = inc8(l); return 4;
        case 0x3C: a = inc8(a); return 4;
        case 0x05: b = dec8(b); return 4;
        case 0x15: d = dec8(d); return 4;
        case 0x25: h = dec8(h); return 4;
        case 0x35: { uint8_t v = read8(hl); write8(hl, dec8(v)); return 12; }
        case 0x0D: c = dec8(c); return 4;
        case 0x1D: e = dec8(e); return 4;
        case 0x2D: l = dec8(l); return 4;
        case 0x3D: a = dec8(a); return 4;

        // LD r,n
        case 0x06: b = fetch8(); return 8;
        case 0x16: d = fetch8(); return 8;
        case 0x26: h = fetch8(); return 8;
        case 0x36: write8(hl, fetch8()); return 12;
        case 0x0E: c = fetch8(); return 8;
        case 0x1E: e = fetch8(); return 8;
        case 0x2E: l = fetch8(); return 8;
        case 0x3E: a = fetch8(); return 8;

        // ADD HL,rr
        case 0x09: add16hl(bc); return 8;
        case 0x19: add16hl(de); return 8;
        case 0x29: add16hl(hl); return 8;
        case 0x39: add16hl(sp); return 8;

        // Rotates on A (clear Z)
        case 0x07: a = rlc(a); setZ(false); return 4;
        case 0x17: a = rl(a);  setZ(false); return 4;
        case 0x0F: a = rrc(a); setZ(false); return 4;
        case 0x1F: a = rr(a);  setZ(false); return 4;

        // DAA / CPL / SCF / CCF
        case 0x27: daa(); return 4;
        case 0x2F: a = ~a; setN(true); setH(true); return 4;
        case 0x37: setN(false); setH(false); setC(true); return 4;
        case 0x3F: setN(false); setH(false); setC(!getC()); return 4;

        // JR n / JR cc,n
        case 0x18: { int8_t off = int8_t(fetch8()); pc += off; return 12; }
        case 0x20: { int8_t off = int8_t(fetch8()); if (!getZ()) { pc += off; return 12; } return 8; }
        case 0x28: { int8_t off = int8_t(fetch8()); if ( getZ()) { pc += off; return 12; } return 8; }
        case 0x30: { int8_t off = int8_t(fetch8()); if (!getC()) { pc += off; return 12; } return 8; }
        case 0x38: { int8_t off = int8_t(fetch8()); if ( getC()) { pc += off; return 12; } return 8; }

        // ALU A,n
        case 0xC6: add8(fetch8()); return 8;
        case 0xCE: adc8(fetch8()); return 8;
        case 0xD6: sub8(fetch8()); return 8;
        case 0xDE: sbc8(fetch8()); return 8;
        case 0xE6: and8(fetch8()); return 8;
        case 0xEE: xor8(fetch8()); return 8;
        case 0xF6: or8(fetch8());  return 8;
        case 0xFE: cp8(fetch8());  return 8;

        // RET / RET cc
        case 0xC9: pc = pop16(); return 16;
        case 0xC0: if (!getZ()) { pc = pop16(); return 20; } return 8;
        case 0xC8: if ( getZ()) { pc = pop16(); return 20; } return 8;
        case 0xD0: if (!getC()) { pc = pop16(); return 20; } return 8;
        case 0xD8: if ( getC()) { pc = pop16(); return 20; } return 8;
        case 0xD9: pc = pop16(); ime = true; return 16; // RETI

        // POP / PUSH
        case 0xC1: bc = pop16(); return 12;
        case 0xD1: de = pop16(); return 12;
        case 0xE1: hl = pop16(); return 12;
        case 0xF1: af = pop16() & 0xFFF0; return 12;
        case 0xC5: push16(bc); return 16;
        case 0xD5: push16(de); return 16;
        case 0xE5: push16(hl); return 16;
        case 0xF5: push16(af & 0xFFF0); return 16;

        // JP / JP cc / JP (HL)
        case 0xC3: pc = fetch16(); return 16;
        case 0xC2: { uint16_t t = fetch16(); if (!getZ()) { pc = t; return 16; } return 12; }
        case 0xCA: { uint16_t t = fetch16(); if ( getZ()) { pc = t; return 16; } return 12; }
        case 0xD2: { uint16_t t = fetch16(); if (!getC()) { pc = t; return 16; } return 12; }
        case 0xDA: { uint16_t t = fetch16(); if ( getC()) { pc = t; return 16; } return 12; }
        case 0xE9: pc = hl; return 4;

        // CALL / CALL cc
        case 0xCD: { uint16_t t = fetch16(); push16(pc); pc = t; return 24; }
        case 0xC4: { uint16_t t = fetch16(); if (!getZ()) { push16(pc); pc = t; return 24; } return 12; }
        case 0xCC: { uint16_t t = fetch16(); if ( getZ()) { push16(pc); pc = t; return 24; } return 12; }
        case 0xD4: { uint16_t t = fetch16(); if (!getC()) { push16(pc); pc = t; return 24; } return 12; }
        case 0xDC: { uint16_t t = fetch16(); if ( getC()) { push16(pc); pc = t; return 24; } return 12; }

        // RST
        case 0xC7: push16(pc); pc = 0x00; return 16;
        case 0xCF: push16(pc); pc = 0x08; return 16;
        case 0xD7: push16(pc); pc = 0x10; return 16;
        case 0xDF: push16(pc); pc = 0x18; return 16;
        case 0xE7: push16(pc); pc = 0x20; return 16;
        case 0xEF: push16(pc); pc = 0x28; return 16;
        case 0xF7: push16(pc); pc = 0x30; return 16;
        case 0xFF: push16(pc); pc = 0x38; return 16;

        // LD (FF00+n),A and LD A,(FF00+n)
        case 0xE0: write8(0xFF00 + fetch8(), a); return 12;
        case 0xF0: a = read8(0xFF00 + fetch8()); return 12;
        // LD (FF00+C),A and LD A,(FF00+C)
        case 0xE2: write8(0xFF00 + c, a); return 8;
        case 0xF2: a = read8(0xFF00 + c); return 8;
        // LD (nn),A and LD A,(nn)
        case 0xEA: { uint16_t addr = fetch16(); write8(addr, a); return 16; }
        case 0xFA: { uint16_t addr = fetch16(); a = read8(addr); return 16; }

        // ADD SP,n / LD HL,SP+n / LD SP,HL
        case 0xE8: { int8_t off = int8_t(fetch8()); sp = addSP(off); return 16; }
        case 0xF8: { int8_t off = int8_t(fetch8()); hl = addSP(off); return 12; }
        case 0xF9: sp = hl; return 8;

        // DI / EI
        case 0xF3: ime = false; imeScheduled = false; return 4;
        case 0xFB: imeScheduled = true; return 4;

        // CB prefix
        case 0xCB: return executeCB();

        default:
            // Illegal opcodes (D3, DB, DD, E3, E4, EB, EC, ED, F4, FC, FD)
            return 4;
    }
}

int CPU::executeCB() {
    uint8_t op = fetch8();
    int reg = op & 0x07;
    int bitIdx = (op >> 3) & 0x07;

    auto rdR = [&](int i) -> uint8_t {
        switch (i) {
            case 0: return b; case 1: return c; case 2: return d; case 3: return e;
            case 4: return h; case 5: return l; case 6: return read8(hl); case 7: return a;
        }
        return 0;
    };
    auto wrR = [&](int i, uint8_t v) {
        switch (i) {
            case 0: b = v; return;  case 1: c = v; return;
            case 2: d = v; return;  case 3: e = v; return;
            case 4: h = v; return;  case 5: l = v; return;
            case 6: write8(hl, v); return;
            case 7: a = v; return;
        }
    };

    int memCycles = (reg == 6) ? 16 : 8;
    int bitCycles = (reg == 6) ? 12 : 8;

    if (op < 0x40) {
        uint8_t v = rdR(reg);
        uint8_t r;
        switch ((op >> 3) & 0x07) {
            case 0: r = rlc(v); break;
            case 1: r = rrc(v); break;
            case 2: r = rl(v);  break;
            case 3: r = rr(v);  break;
            case 4: r = sla(v); break;
            case 5: r = sra(v); break;
            case 6: r = swap(v); break;
            case 7: r = srl(v); break;
            default: r = v;
        }
        wrR(reg, r);
        return memCycles;
    }
    if (op < 0x80) {
        bit(bitIdx, rdR(reg));
        return bitCycles;
    }
    if (op < 0xC0) {
        wrR(reg, rdR(reg) & ~(1u << bitIdx));
        return memCycles;
    }
    wrR(reg, rdR(reg) | (1u << bitIdx));
    return memCycles;
}
