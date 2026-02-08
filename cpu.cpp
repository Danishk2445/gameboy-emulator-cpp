#include "cpu.h"
#include "memory.h"

CPU::CPU(Memory& mem) : memory(mem) {
    reset();
}

void CPU::reset() {
    // After boot ROM, these are the initial values
    af = 0x01B0;
    bc = 0x0013;
    de = 0x00D8;
    hl = 0x014D;
    sp = 0xFFFE;
    pc = 0x0100;  // Skip boot ROM
    
    ime = false;
    imeScheduled = false;
    halted = false;
    stopped = false;
}

uint8_t CPU::read8(uint16_t addr) {
    return memory.read(addr);
}

void CPU::write8(uint16_t addr, uint8_t val) {
    memory.write(addr, val);
}

uint16_t CPU::read16(uint16_t addr) {
    return read8(addr) | (read8(addr + 1) << 8);
}

void CPU::write16(uint16_t addr, uint16_t val) {
    write8(addr, val & 0xFF);
    write8(addr + 1, val >> 8);
}

uint8_t CPU::fetch8() {
    return read8(pc++);
}

uint16_t CPU::fetch16() {
    uint16_t val = read16(pc);
    pc += 2;
    return val;
}

void CPU::push16(uint16_t val) {
    sp -= 2;
    write16(sp, val);
}

uint16_t CPU::pop16() {
    uint16_t val = read16(sp);
    sp += 2;
    return val;
}

void CPU::requestInterrupt(uint8_t interrupt) {
    memory.setIF(memory.getIF() | interrupt);
}

int CPU::handleInterrupts() {
    uint8_t pending = memory.getIF() & memory.getIE() & 0x1F;
    
    if (pending) {
        halted = false;
        
        if (ime) {
            ime = false;
            
            // Find highest priority interrupt
            uint16_t handler = 0;
            uint8_t bit = 0;
            
            if (pending & INT_VBLANK) { handler = 0x0040; bit = INT_VBLANK; }
            else if (pending & INT_STAT) { handler = 0x0048; bit = INT_STAT; }
            else if (pending & INT_TIMER) { handler = 0x0050; bit = INT_TIMER; }
            else if (pending & INT_SERIAL) { handler = 0x0058; bit = INT_SERIAL; }
            else if (pending & INT_JOYPAD) { handler = 0x0060; bit = INT_JOYPAD; }
            
            // Clear interrupt flag
            memory.setIF(memory.getIF() & ~bit);
            
            // Call interrupt handler
            push16(pc);
            pc = handler;
            
            return 20;  // Interrupt handling takes 20 cycles
        }
    }
    
    return 0;
}

// ALU Operations
void CPU::add8(uint8_t val) {
    int result = a + val;
    setFlags((result & 0xFF) == 0, false, (a & 0xF) + (val & 0xF) > 0xF, result > 0xFF);
    a = result & 0xFF;
}

void CPU::adc8(uint8_t val) {
    int carry = getC() ? 1 : 0;
    int result = a + val + carry;
    setFlags((result & 0xFF) == 0, false, (a & 0xF) + (val & 0xF) + carry > 0xF, result > 0xFF);
    a = result & 0xFF;
}

void CPU::sub8(uint8_t val) {
    int result = a - val;
    setFlags((result & 0xFF) == 0, true, (a & 0xF) < (val & 0xF), a < val);
    a = result & 0xFF;
}

void CPU::sbc8(uint8_t val) {
    int carry = getC() ? 1 : 0;
    int result = a - val - carry;
    setFlags((result & 0xFF) == 0, true, (a & 0xF) < (val & 0xF) + carry, result < 0);
    a = result & 0xFF;
}

void CPU::and8(uint8_t val) {
    a &= val;
    setFlags(a == 0, false, true, false);
}

void CPU::or8(uint8_t val) {
    a |= val;
    setFlags(a == 0, false, false, false);
}

void CPU::xor8(uint8_t val) {
    a ^= val;
    setFlags(a == 0, false, false, false);
}

void CPU::cp8(uint8_t val) {
    setFlags(a == val, true, (a & 0xF) < (val & 0xF), a < val);
}

void CPU::inc8(uint8_t& reg) {
    reg++;
    setZ(reg == 0);
    setN(false);
    setH((reg & 0xF) == 0);
}

void CPU::dec8(uint8_t& reg) {
    reg--;
    setZ(reg == 0);
    setN(true);
    setH((reg & 0xF) == 0xF);
}

void CPU::add16hl(uint16_t val) {
    int result = hl + val;
    setN(false);
    setH((hl & 0xFFF) + (val & 0xFFF) > 0xFFF);
    setC(result > 0xFFFF);
    hl = result & 0xFFFF;
}

void CPU::addSP(int8_t val) {
    int result = sp + val;
    setFlags(false, false, 
             (sp & 0xF) + (val & 0xF) > 0xF,
             (sp & 0xFF) + (val & 0xFF) > 0xFF);
    sp = result & 0xFFFF;
}

// Rotate/Shift Operations
uint8_t CPU::rlc(uint8_t val) {
    uint8_t result = (val << 1) | (val >> 7);
    setFlags(result == 0, false, false, (val & 0x80) != 0);
    return result;
}

uint8_t CPU::rrc(uint8_t val) {
    uint8_t result = (val >> 1) | (val << 7);
    setFlags(result == 0, false, false, (val & 0x01) != 0);
    return result;
}

uint8_t CPU::rl(uint8_t val) {
    uint8_t result = (val << 1) | (getC() ? 1 : 0);
    setFlags(result == 0, false, false, (val & 0x80) != 0);
    return result;
}

uint8_t CPU::rr(uint8_t val) {
    uint8_t result = (val >> 1) | (getC() ? 0x80 : 0);
    setFlags(result == 0, false, false, (val & 0x01) != 0);
    return result;
}

uint8_t CPU::sla(uint8_t val) {
    uint8_t result = val << 1;
    setFlags(result == 0, false, false, (val & 0x80) != 0);
    return result;
}

uint8_t CPU::sra(uint8_t val) {
    uint8_t result = (val >> 1) | (val & 0x80);
    setFlags(result == 0, false, false, (val & 0x01) != 0);
    return result;
}

uint8_t CPU::swap(uint8_t val) {
    uint8_t result = ((val & 0xF) << 4) | ((val & 0xF0) >> 4);
    setFlags(result == 0, false, false, false);
    return result;
}

uint8_t CPU::srl(uint8_t val) {
    uint8_t result = val >> 1;
    setFlags(result == 0, false, false, (val & 0x01) != 0);
    return result;
}

void CPU::bit(uint8_t b, uint8_t val) {
    setZ((val & (1 << b)) == 0);
    setN(false);
    setH(true);
}

uint8_t CPU::res(uint8_t b, uint8_t val) {
    return val & ~(1 << b);
}

uint8_t CPU::set(uint8_t b, uint8_t val) {
    return val | (1 << b);
}

int CPU::executeCB() {
    uint8_t opcode = fetch8();
    uint8_t* regs[] = { &b, &c, &d, &e, &h, &l, nullptr, &a };
    
    int reg = opcode & 0x07;
    int op = opcode >> 3;
    
    uint8_t val;
    if (reg == 6) {
        val = read8(hl);
    } else {
        val = *regs[reg];
    }
    
    uint8_t result = val;
    
    switch (op) {
        case 0: result = rlc(val); break;
        case 1: result = rrc(val); break;
        case 2: result = rl(val); break;
        case 3: result = rr(val); break;
        case 4: result = sla(val); break;
        case 5: result = sra(val); break;
        case 6: result = swap(val); break;
        case 7: result = srl(val); break;
        default:
            int bitNum = (op - 8) % 8;
            if (op < 16) {
                bit(bitNum, val);
                return reg == 6 ? 12 : 8;
            } else if (op < 24) {
                result = res(bitNum, val);
            } else {
                result = set(bitNum, val);
            }
            break;
    }
    
    if (reg == 6) {
        write8(hl, result);
        return 16;
    } else {
        *regs[reg] = result;
        return 8;
    }
}

int CPU::step() {
    // Handle scheduled IME enable
    if (imeScheduled) {
        ime = true;
        imeScheduled = false;
    }
    
    // Handle interrupts
    int intCycles = handleInterrupts();
    if (intCycles > 0) return intCycles;
    
    // If halted, just wait
    if (halted) return 4;
    
    uint8_t opcode = fetch8();
    
    switch (opcode) {
        // NOP
        case 0x00: return 4;
        
        // LD r16, imm16
        case 0x01: bc = fetch16(); return 12;
        case 0x11: de = fetch16(); return 12;
        case 0x21: hl = fetch16(); return 12;
        case 0x31: sp = fetch16(); return 12;
        
        // LD (r16), A
        case 0x02: write8(bc, a); return 8;
        case 0x12: write8(de, a); return 8;
        case 0x22: write8(hl++, a); return 8;
        case 0x32: write8(hl--, a); return 8;
        
        // LD A, (r16)
        case 0x0A: a = read8(bc); return 8;
        case 0x1A: a = read8(de); return 8;
        case 0x2A: a = read8(hl++); return 8;
        case 0x3A: a = read8(hl--); return 8;
        
        // INC r16
        case 0x03: bc++; return 8;
        case 0x13: de++; return 8;
        case 0x23: hl++; return 8;
        case 0x33: sp++; return 8;
        
        // DEC r16
        case 0x0B: bc--; return 8;
        case 0x1B: de--; return 8;
        case 0x2B: hl--; return 8;
        case 0x3B: sp--; return 8;
        
        // INC r8
        case 0x04: inc8(b); return 4;
        case 0x0C: inc8(c); return 4;
        case 0x14: inc8(d); return 4;
        case 0x1C: inc8(e); return 4;
        case 0x24: inc8(h); return 4;
        case 0x2C: inc8(l); return 4;
        case 0x34: { uint8_t v = read8(hl); inc8(v); write8(hl, v); return 12; }
        case 0x3C: inc8(a); return 4;
        
        // DEC r8
        case 0x05: dec8(b); return 4;
        case 0x0D: dec8(c); return 4;
        case 0x15: dec8(d); return 4;
        case 0x1D: dec8(e); return 4;
        case 0x25: dec8(h); return 4;
        case 0x2D: dec8(l); return 4;
        case 0x35: { uint8_t v = read8(hl); dec8(v); write8(hl, v); return 12; }
        case 0x3D: dec8(a); return 4;
        
        // LD r8, imm8
        case 0x06: b = fetch8(); return 8;
        case 0x0E: c = fetch8(); return 8;
        case 0x16: d = fetch8(); return 8;
        case 0x1E: e = fetch8(); return 8;
        case 0x26: h = fetch8(); return 8;
        case 0x2E: l = fetch8(); return 8;
        case 0x36: write8(hl, fetch8()); return 12;
        case 0x3E: a = fetch8(); return 8;
        
        // Rotate A
        case 0x07: a = rlc(a); setZ(false); return 4;  // RLCA
        case 0x0F: a = rrc(a); setZ(false); return 4;  // RRCA
        case 0x17: a = rl(a); setZ(false); return 4;   // RLA
        case 0x1F: a = rr(a); setZ(false); return 4;   // RRA
        
        // LD (imm16), SP
        case 0x08: write16(fetch16(), sp); return 20;
        
        // ADD HL, r16
        case 0x09: add16hl(bc); return 8;
        case 0x19: add16hl(de); return 8;
        case 0x29: add16hl(hl); return 8;
        case 0x39: add16hl(sp); return 8;
        
        // JR
        case 0x18: { int8_t e8 = (int8_t)fetch8(); pc += e8; return 12; }
        case 0x20: { int8_t e8 = (int8_t)fetch8(); if (!getZ()) { pc += e8; return 12; } return 8; }
        case 0x28: { int8_t e8 = (int8_t)fetch8(); if (getZ()) { pc += e8; return 12; } return 8; }
        case 0x30: { int8_t e8 = (int8_t)fetch8(); if (!getC()) { pc += e8; return 12; } return 8; }
        case 0x38: { int8_t e8 = (int8_t)fetch8(); if (getC()) { pc += e8; return 12; } return 8; }
        
        // DAA
        case 0x27: {
            int result = a;
            if (getN()) {
                if (getC()) result -= 0x60;
                if (getH()) result -= 0x06;
            } else {
                if (getC() || result > 0x99) { result += 0x60; setC(true); }
                if (getH() || (result & 0x0F) > 0x09) result += 0x06;
            }
            a = result & 0xFF;
            setZ(a == 0);
            setH(false);
            return 4;
        }
        
        // CPL
        case 0x2F: a = ~a; setN(true); setH(true); return 4;
        
        // SCF
        case 0x37: setN(false); setH(false); setC(true); return 4;
        
        // CCF
        case 0x3F: setN(false); setH(false); setC(!getC()); return 4;
        
        // HALT
        case 0x76: halted = true; return 4;
        
        // LD r8, r8 (0x40-0x7F except 0x76)
        case 0x40: b = b; return 4;
        case 0x41: b = c; return 4;
        case 0x42: b = d; return 4;
        case 0x43: b = e; return 4;
        case 0x44: b = h; return 4;
        case 0x45: b = l; return 4;
        case 0x46: b = read8(hl); return 8;
        case 0x47: b = a; return 4;
        case 0x48: c = b; return 4;
        case 0x49: c = c; return 4;
        case 0x4A: c = d; return 4;
        case 0x4B: c = e; return 4;
        case 0x4C: c = h; return 4;
        case 0x4D: c = l; return 4;
        case 0x4E: c = read8(hl); return 8;
        case 0x4F: c = a; return 4;
        case 0x50: d = b; return 4;
        case 0x51: d = c; return 4;
        case 0x52: d = d; return 4;
        case 0x53: d = e; return 4;
        case 0x54: d = h; return 4;
        case 0x55: d = l; return 4;
        case 0x56: d = read8(hl); return 8;
        case 0x57: d = a; return 4;
        case 0x58: e = b; return 4;
        case 0x59: e = c; return 4;
        case 0x5A: e = d; return 4;
        case 0x5B: e = e; return 4;
        case 0x5C: e = h; return 4;
        case 0x5D: e = l; return 4;
        case 0x5E: e = read8(hl); return 8;
        case 0x5F: e = a; return 4;
        case 0x60: h = b; return 4;
        case 0x61: h = c; return 4;
        case 0x62: h = d; return 4;
        case 0x63: h = e; return 4;
        case 0x64: h = h; return 4;
        case 0x65: h = l; return 4;
        case 0x66: h = read8(hl); return 8;
        case 0x67: h = a; return 4;
        case 0x68: l = b; return 4;
        case 0x69: l = c; return 4;
        case 0x6A: l = d; return 4;
        case 0x6B: l = e; return 4;
        case 0x6C: l = h; return 4;
        case 0x6D: l = l; return 4;
        case 0x6E: l = read8(hl); return 8;
        case 0x6F: l = a; return 4;
        case 0x70: write8(hl, b); return 8;
        case 0x71: write8(hl, c); return 8;
        case 0x72: write8(hl, d); return 8;
        case 0x73: write8(hl, e); return 8;
        case 0x74: write8(hl, h); return 8;
        case 0x75: write8(hl, l); return 8;
        case 0x77: write8(hl, a); return 8;
        case 0x78: a = b; return 4;
        case 0x79: a = c; return 4;
        case 0x7A: a = d; return 4;
        case 0x7B: a = e; return 4;
        case 0x7C: a = h; return 4;
        case 0x7D: a = l; return 4;
        case 0x7E: a = read8(hl); return 8;
        case 0x7F: a = a; return 4;
        
        // ADD A, r8
        case 0x80: add8(b); return 4;
        case 0x81: add8(c); return 4;
        case 0x82: add8(d); return 4;
        case 0x83: add8(e); return 4;
        case 0x84: add8(h); return 4;
        case 0x85: add8(l); return 4;
        case 0x86: add8(read8(hl)); return 8;
        case 0x87: add8(a); return 4;
        
        // ADC A, r8
        case 0x88: adc8(b); return 4;
        case 0x89: adc8(c); return 4;
        case 0x8A: adc8(d); return 4;
        case 0x8B: adc8(e); return 4;
        case 0x8C: adc8(h); return 4;
        case 0x8D: adc8(l); return 4;
        case 0x8E: adc8(read8(hl)); return 8;
        case 0x8F: adc8(a); return 4;
        
        // SUB A, r8
        case 0x90: sub8(b); return 4;
        case 0x91: sub8(c); return 4;
        case 0x92: sub8(d); return 4;
        case 0x93: sub8(e); return 4;
        case 0x94: sub8(h); return 4;
        case 0x95: sub8(l); return 4;
        case 0x96: sub8(read8(hl)); return 8;
        case 0x97: sub8(a); return 4;
        
        // SBC A, r8
        case 0x98: sbc8(b); return 4;
        case 0x99: sbc8(c); return 4;
        case 0x9A: sbc8(d); return 4;
        case 0x9B: sbc8(e); return 4;
        case 0x9C: sbc8(h); return 4;
        case 0x9D: sbc8(l); return 4;
        case 0x9E: sbc8(read8(hl)); return 8;
        case 0x9F: sbc8(a); return 4;
        
        // AND A, r8
        case 0xA0: and8(b); return 4;
        case 0xA1: and8(c); return 4;
        case 0xA2: and8(d); return 4;
        case 0xA3: and8(e); return 4;
        case 0xA4: and8(h); return 4;
        case 0xA5: and8(l); return 4;
        case 0xA6: and8(read8(hl)); return 8;
        case 0xA7: and8(a); return 4;
        
        // XOR A, r8
        case 0xA8: xor8(b); return 4;
        case 0xA9: xor8(c); return 4;
        case 0xAA: xor8(d); return 4;
        case 0xAB: xor8(e); return 4;
        case 0xAC: xor8(h); return 4;
        case 0xAD: xor8(l); return 4;
        case 0xAE: xor8(read8(hl)); return 8;
        case 0xAF: xor8(a); return 4;
        
        // OR A, r8
        case 0xB0: or8(b); return 4;
        case 0xB1: or8(c); return 4;
        case 0xB2: or8(d); return 4;
        case 0xB3: or8(e); return 4;
        case 0xB4: or8(h); return 4;
        case 0xB5: or8(l); return 4;
        case 0xB6: or8(read8(hl)); return 8;
        case 0xB7: or8(a); return 4;
        
        // CP A, r8
        case 0xB8: cp8(b); return 4;
        case 0xB9: cp8(c); return 4;
        case 0xBA: cp8(d); return 4;
        case 0xBB: cp8(e); return 4;
        case 0xBC: cp8(h); return 4;
        case 0xBD: cp8(l); return 4;
        case 0xBE: cp8(read8(hl)); return 8;
        case 0xBF: cp8(a); return 4;
        
        // RET cc
        case 0xC0: if (!getZ()) { pc = pop16(); return 20; } return 8;
        case 0xC8: if (getZ()) { pc = pop16(); return 20; } return 8;
        case 0xD0: if (!getC()) { pc = pop16(); return 20; } return 8;
        case 0xD8: if (getC()) { pc = pop16(); return 20; } return 8;
        
        // POP r16
        case 0xC1: bc = pop16(); return 12;
        case 0xD1: de = pop16(); return 12;
        case 0xE1: hl = pop16(); return 12;
        case 0xF1: af = pop16() & 0xFFF0; return 12;  // Lower 4 bits of F are always 0
        
        // JP cc, imm16
        case 0xC2: { uint16_t addr = fetch16(); if (!getZ()) { pc = addr; return 16; } return 12; }
        case 0xCA: { uint16_t addr = fetch16(); if (getZ()) { pc = addr; return 16; } return 12; }
        case 0xD2: { uint16_t addr = fetch16(); if (!getC()) { pc = addr; return 16; } return 12; }
        case 0xDA: { uint16_t addr = fetch16(); if (getC()) { pc = addr; return 16; } return 12; }
        
        // JP imm16
        case 0xC3: pc = fetch16(); return 16;
        
        // CALL cc, imm16
        case 0xC4: { uint16_t addr = fetch16(); if (!getZ()) { push16(pc); pc = addr; return 24; } return 12; }
        case 0xCC: { uint16_t addr = fetch16(); if (getZ()) { push16(pc); pc = addr; return 24; } return 12; }
        case 0xD4: { uint16_t addr = fetch16(); if (!getC()) { push16(pc); pc = addr; return 24; } return 12; }
        case 0xDC: { uint16_t addr = fetch16(); if (getC()) { push16(pc); pc = addr; return 24; } return 12; }
        
        // PUSH r16
        case 0xC5: push16(bc); return 16;
        case 0xD5: push16(de); return 16;
        case 0xE5: push16(hl); return 16;
        case 0xF5: push16(af); return 16;
        
        // ALU A, imm8
        case 0xC6: add8(fetch8()); return 8;
        case 0xCE: adc8(fetch8()); return 8;
        case 0xD6: sub8(fetch8()); return 8;
        case 0xDE: sbc8(fetch8()); return 8;
        case 0xE6: and8(fetch8()); return 8;
        case 0xEE: xor8(fetch8()); return 8;
        case 0xF6: or8(fetch8()); return 8;
        case 0xFE: cp8(fetch8()); return 8;
        
        // RST
        case 0xC7: push16(pc); pc = 0x00; return 16;
        case 0xCF: push16(pc); pc = 0x08; return 16;
        case 0xD7: push16(pc); pc = 0x10; return 16;
        case 0xDF: push16(pc); pc = 0x18; return 16;
        case 0xE7: push16(pc); pc = 0x20; return 16;
        case 0xEF: push16(pc); pc = 0x28; return 16;
        case 0xF7: push16(pc); pc = 0x30; return 16;
        case 0xFF: push16(pc); pc = 0x38; return 16;
        
        // RET
        case 0xC9: pc = pop16(); return 16;
        
        // RETI
        case 0xD9: pc = pop16(); ime = true; return 16;
        
        // CALL imm16
        case 0xCD: { uint16_t addr = fetch16(); push16(pc); pc = addr; return 24; }
        
        // CB prefix
        case 0xCB: return executeCB() + 4;
        
        // LDH (imm8), A
        case 0xE0: write8(0xFF00 + fetch8(), a); return 12;
        
        // LDH A, (imm8)
        case 0xF0: a = read8(0xFF00 + fetch8()); return 12;
        
        // LDH (C), A
        case 0xE2: write8(0xFF00 + c, a); return 8;
        
        // LDH A, (C)
        case 0xF2: a = read8(0xFF00 + c); return 8;
        
        // LD (imm16), A
        case 0xEA: write8(fetch16(), a); return 16;
        
        // LD A, (imm16)
        case 0xFA: a = read8(fetch16()); return 16;
        
        // JP HL
        case 0xE9: pc = hl; return 4;
        
        // LD SP, HL
        case 0xF9: sp = hl; return 8;
        
        // ADD SP, imm8
        case 0xE8: addSP((int8_t)fetch8()); return 16;
        
        // LD HL, SP+imm8
        case 0xF8: {
            int8_t e8 = (int8_t)fetch8();
            int result = sp + e8;
            setFlags(false, false,
                     (sp & 0xF) + (e8 & 0xF) > 0xF,
                     (sp & 0xFF) + (e8 & 0xFF) > 0xFF);
            hl = result & 0xFFFF;
            return 12;
        }
        
        // DI
        case 0xF3: ime = false; return 4;
        
        // EI
        case 0xFB: imeScheduled = true; return 4;
        
        // STOP
        case 0x10: stopped = true; fetch8(); return 4;
        
        // Undefined opcodes
        default: return 4;
    }
}
