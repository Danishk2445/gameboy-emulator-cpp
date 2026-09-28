#include "cpu.hpp"

#include <bit>

#include "joypad.hpp"
#include "timer.hpp"

namespace gb {

void Cpu::reset(bool post_boot, bool header_checksum_nonzero) {
    ime_ = false;
    ei_pending_ = false;
    halted_ = false;
    halt_bug_ = false;
    stopped_ = false;
    locked_ = false;
    ld_b_b_hit = false;
    if (post_boot) {
        // DMG register state when the boot ROM hands over at 0x0100.
        a = 0x01;
        f = header_checksum_nonzero ? 0xB0 : 0x80;
        b = 0x00;
        c = 0x13;
        d = 0x00;
        e = 0xD8;
        h = 0x01;
        l = 0x4D;
        sp = 0xFFFE;
        pc = 0x0100;
    } else {
        a = f = b = c = d = e = h = l = 0;
        sp = 0;
        pc = 0;
    }
}

void Cpu::step() {
    if (locked_) {
        tick();
        return;
    }
    if (halted_) {
        tick();
        if (irq_.pending()) halted_ = false;  // wakes even with IME=0
        return;
    }
    if (stopped_) {
        tick();
        if (bus_.joypad().any_pressed()) stopped_ = false;
        return;
    }
    // EI takes effect after the instruction following it.
    bool ime_now = ime_;
    if (ei_pending_) {
        ime_ = true;
        ei_pending_ = false;
    }

    // Interrupts are sampled at the end of the opcode fetch cycle; if one is taken,
    // the fetched opcode is discarded and the fetch counts as the dispatch's first M-cycle.
    u8 op = read(pc);
    if (ime_now && irq_.pending_at_fetch()) {
        service_interrupt();
        return;
    }
    if (halt_bug_) {
        halt_bug_ = false;  // PC fails to increment: the byte after HALT is read twice
    } else {
        ++pc;
    }
    execute(op);
}

void Cpu::service_interrupt() {
    // M1 was the discarded opcode fetch.
    tick();
    --sp;
    write(sp, pc >> 8);
    // Sampled after the high byte push: overwriting IE with that push can cancel the dispatch.
    u8 pending = irq_.pending();
    --sp;
    write(sp, pc & 0xFF);
    if (pending == 0) {
        pc = 0x0000;
    } else {
        int bit = std::countr_zero(pending);
        irq_.flags &= static_cast<u8>(~(1u << bit));
        pc = static_cast<u16>(0x40 + bit * 8);
    }
    ime_ = false;
    tick();
}

void Cpu::push16(u16 v) {
    --sp;
    write(sp, v >> 8);
    --sp;
    write(sp, v & 0xFF);
}

u16 Cpu::pop16() {
    u8 lo = read(sp++);
    u8 hi = read(sp++);
    return static_cast<u16>((hi << 8) | lo);
}

u16 Cpu::rp(int idx) const {
    switch (idx) {
        case 0: return bc();
        case 1: return de();
        case 2: return hl();
        default: return sp;
    }
}

void Cpu::set_rp(int idx, u16 v) {
    switch (idx) {
        case 0: set_bc(v); break;
        case 1: set_de(v); break;
        case 2: set_hl(v); break;
        default: sp = v; break;
    }
}

bool Cpu::condition(int cc) const {
    switch (cc) {
        case 0: return !flag(kZ);
        case 1: return flag(kZ);
        case 2: return !flag(kC);
        default: return flag(kC);
    }
}

u8 Cpu::r8(int idx) {
    switch (idx) {
        case 0: return b;
        case 1: return c;
        case 2: return d;
        case 3: return e;
        case 4: return h;
        case 5: return l;
        case 6: return read(hl());
        default: return a;
    }
}

void Cpu::set_r8(int idx, u8 v) {
    switch (idx) {
        case 0: b = v; break;
        case 1: c = v; break;
        case 2: d = v; break;
        case 3: e = v; break;
        case 4: h = v; break;
        case 5: l = v; break;
        case 6: write(hl(), v); break;
        default: a = v; break;
    }
}

void Cpu::alu(int op, u8 v) {
    switch (op) {
        case 0:    // ADD
        case 1: {  // ADC
            int carry = (op == 1 && flag(kC)) ? 1 : 0;
            int r = a + v + carry;
            set_flags((r & 0xFF) == 0, false, ((a & 0x0F) + (v & 0x0F) + carry) > 0x0F, r > 0xFF);
            a = static_cast<u8>(r);
            break;
        }
        case 2:    // SUB
        case 3:    // SBC
        case 7: {  // CP
            int carry = (op == 3 && flag(kC)) ? 1 : 0;
            int r = a - v - carry;
            set_flags((r & 0xFF) == 0, true, ((a & 0x0F) - (v & 0x0F) - carry) < 0, r < 0);
            if (op != 7) a = static_cast<u8>(r);
            break;
        }
        case 4:
            a &= v;
            set_flags(a == 0, false, true, false);
            break;
        case 5:
            a ^= v;
            set_flags(a == 0, false, false, false);
            break;
        case 6:
            a |= v;
            set_flags(a == 0, false, false, false);
            break;
    }
}

u8 Cpu::inc8(u8 v) {
    u8 r = static_cast<u8>(v + 1);
    set_flags(r == 0, false, (r & 0x0F) == 0, flag(kC));
    return r;
}

u8 Cpu::dec8(u8 v) {
    u8 r = static_cast<u8>(v - 1);
    set_flags(r == 0, true, (r & 0x0F) == 0x0F, flag(kC));
    return r;
}

void Cpu::add_hl(u16 v) {
    u32 r = hl() + v;
    set_flags(flag(kZ), false, ((hl() & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF, r > 0xFFFF);
    set_hl(static_cast<u16>(r));
}

u16 Cpu::add_sp_e(u8 e8) {
    u16 r = static_cast<u16>(sp + static_cast<i8>(e8));
    set_flags(false, false, ((sp & 0x0F) + (e8 & 0x0F)) > 0x0F, ((sp & 0xFF) + e8) > 0xFF);
    return r;
}

void Cpu::daa() {
    u8 correction = 0;
    bool carry = flag(kC);
    if (flag(kH) || (!flag(kN) && (a & 0x0F) > 0x09)) correction |= 0x06;
    if (flag(kC) || (!flag(kN) && a > 0x99)) {
        correction |= 0x60;
        carry = true;
    }
    a = flag(kN) ? static_cast<u8>(a - correction) : static_cast<u8>(a + correction);
    set_flags(a == 0, flag(kN), false, carry);
}

u8 Cpu::rotate_shift(int op, u8 v) {
    u8 r = 0;
    bool carry = false;
    switch (op) {
        case 0:  // RLC
            carry = v & 0x80;
            r = static_cast<u8>((v << 1) | (v >> 7));
            break;
        case 1:  // RRC
            carry = v & 0x01;
            r = static_cast<u8>((v >> 1) | (v << 7));
            break;
        case 2:  // RL
            carry = v & 0x80;
            r = static_cast<u8>((v << 1) | (flag(kC) ? 1 : 0));
            break;
        case 3:  // RR
            carry = v & 0x01;
            r = static_cast<u8>((v >> 1) | (flag(kC) ? 0x80 : 0));
            break;
        case 4:  // SLA
            carry = v & 0x80;
            r = static_cast<u8>(v << 1);
            break;
        case 5:  // SRA
            carry = v & 0x01;
            r = static_cast<u8>((v >> 1) | (v & 0x80));
            break;
        case 6:  // SWAP
            r = static_cast<u8>((v << 4) | (v >> 4));
            break;
        case 7:  // SRL
            carry = v & 0x01;
            r = v >> 1;
            break;
    }
    set_flags(r == 0, false, false, carry);
    return r;
}

void Cpu::execute_cb() {
    u8 op = fetch8();
    int reg = op & 7;
    int bit = (op >> 3) & 7;
    switch (op >> 6) {
        case 0: set_r8(reg, rotate_shift(bit, r8(reg))); break;
        case 1: {
            u8 v = r8(reg);
            set_flags(!(v & (1u << bit)), false, true, flag(kC));
            break;
        }
        case 2: set_r8(reg, static_cast<u8>(r8(reg) & ~(1u << bit))); break;
        case 3: set_r8(reg, static_cast<u8>(r8(reg) | (1u << bit))); break;
    }
}

void Cpu::execute(u8 op) {
    // 0x40-0x7F: LD r, r'   (0x76 is HALT)
    if (op >= 0x40 && op < 0x80) {
        if (op == 0x76) {
            if (!ime_ && irq_.pending()) {
                halt_bug_ = true;
            } else {
                halted_ = true;
            }
            return;
        }
        if (op == 0x40) ld_b_b_hit = true;
        set_r8((op >> 3) & 7, r8(op & 7));
        return;
    }
    // 0x80-0xBF: ALU A, r
    if (op >= 0x80 && op < 0xC0) {
        alu((op >> 3) & 7, r8(op & 7));
        return;
    }
    // Regular patterns in 0x00-0x3F
    if (op < 0x40) {
        int y = (op >> 3) & 7;
        switch (op & 0x0F) {
            case 0x01: set_rp(op >> 4, fetch16()); return;  // LD rr, d16
            case 0x03:                                      // INC rr
                tick();
                set_rp(op >> 4, static_cast<u16>(rp(op >> 4) + 1));
                return;
            case 0x09:  // ADD HL, rr
                tick();
                add_hl(rp(op >> 4));
                return;
            case 0x0B:  // DEC rr
                tick();
                set_rp(op >> 4, static_cast<u16>(rp(op >> 4) - 1));
                return;
            default: break;
        }
        switch (op & 0x07) {
            case 0x04: set_r8(y, inc8(r8(y))); return;  // INC r
            case 0x05: set_r8(y, dec8(r8(y))); return;  // DEC r
            case 0x06: set_r8(y, fetch8()); return;     // LD r, d8
            default: break;
        }
    }

    switch (op) {
        case 0x00: break;  // NOP
        case 0x02: write(bc(), a); break;
        case 0x12: write(de(), a); break;
        case 0x22:
            write(hl(), a);
            set_hl(static_cast<u16>(hl() + 1));
            break;
        case 0x32:
            write(hl(), a);
            set_hl(static_cast<u16>(hl() - 1));
            break;
        case 0x0A: a = read(bc()); break;
        case 0x1A: a = read(de()); break;
        case 0x2A:
            a = read(hl());
            set_hl(static_cast<u16>(hl() + 1));
            break;
        case 0x3A:
            a = read(hl());
            set_hl(static_cast<u16>(hl() - 1));
            break;

        case 0x07:  // RLCA
            a = rotate_shift(0, a);
            f &= ~kZ;
            break;
        case 0x0F:  // RRCA
            a = rotate_shift(1, a);
            f &= ~kZ;
            break;
        case 0x17:  // RLA
            a = rotate_shift(2, a);
            f &= ~kZ;
            break;
        case 0x1F:  // RRA
            a = rotate_shift(3, a);
            f &= ~kZ;
            break;

        case 0x08: {  // LD (a16), SP
            u16 addr = fetch16();
            write(addr, sp & 0xFF);
            write(static_cast<u16>(addr + 1), sp >> 8);
            break;
        }
        case 0x10:  // STOP
            fetch8();
            bus_.timer().reset_div();
            if (!bus_.joypad().any_pressed()) stopped_ = true;
            break;

        case 0x18: {  // JR e
            i8 off = static_cast<i8>(fetch8());
            tick();
            pc = static_cast<u16>(pc + off);
            break;
        }
        case 0x20:
        case 0x28:
        case 0x30:
        case 0x38: {  // JR cc, e
            i8 off = static_cast<i8>(fetch8());
            if (condition((op >> 3) & 3)) {
                tick();
                pc = static_cast<u16>(pc + off);
            }
            break;
        }

        case 0x27: daa(); break;
        case 0x2F:  // CPL
            a = ~a;
            f |= kN | kH;
            break;
        case 0x37: set_flags(flag(kZ), false, false, true); break;        // SCF
        case 0x3F: set_flags(flag(kZ), false, false, !flag(kC)); break;   // CCF

        case 0xC0:
        case 0xC8:
        case 0xD0:
        case 0xD8:  // RET cc
            tick();
            if (condition((op >> 3) & 3)) {
                pc = pop16();
                tick();
            }
            break;
        case 0xC9:  // RET
            pc = pop16();
            tick();
            break;
        case 0xD9:  // RETI
            pc = pop16();
            tick();
            ime_ = true;
            break;

        case 0xC1: set_bc(pop16()); break;
        case 0xD1: set_de(pop16()); break;
        case 0xE1: set_hl(pop16()); break;
        case 0xF1: set_af(pop16()); break;
        case 0xC5:
            tick();
            push16(bc());
            break;
        case 0xD5:
            tick();
            push16(de());
            break;
        case 0xE5:
            tick();
            push16(hl());
            break;
        case 0xF5:
            tick();
            push16(af());
            break;

        case 0xC2:
        case 0xCA:
        case 0xD2:
        case 0xDA: {  // JP cc, a16
            u16 addr = fetch16();
            if (condition((op >> 3) & 3)) {
                tick();
                pc = addr;
            }
            break;
        }
        case 0xC3: {  // JP a16
            u16 addr = fetch16();
            tick();
            pc = addr;
            break;
        }
        case 0xE9: pc = hl(); break;  // JP HL

        case 0xC4:
        case 0xCC:
        case 0xD4:
        case 0xDC: {  // CALL cc, a16
            u16 addr = fetch16();
            if (condition((op >> 3) & 3)) {
                tick();
                push16(pc);
                pc = addr;
            }
            break;
        }
        case 0xCD: {  // CALL a16
            u16 addr = fetch16();
            tick();
            push16(pc);
            pc = addr;
            break;
        }

        case 0xC7:
        case 0xCF:
        case 0xD7:
        case 0xDF:
        case 0xE7:
        case 0xEF:
        case 0xF7:
        case 0xFF:  // RST
            tick();
            push16(pc);
            pc = op & 0x38;
            break;

        case 0xC6:
        case 0xCE:
        case 0xD6:
        case 0xDE:
        case 0xE6:
        case 0xEE:
        case 0xF6:
        case 0xFE: alu((op >> 3) & 7, fetch8()); break;  // ALU A, d8

        case 0xCB: execute_cb(); break;

        case 0xE0: write(static_cast<u16>(0xFF00 | fetch8()), a); break;
        case 0xF0: a = read(static_cast<u16>(0xFF00 | fetch8())); break;
        case 0xE2: write(static_cast<u16>(0xFF00 | c), a); break;
        case 0xF2: a = read(static_cast<u16>(0xFF00 | c)); break;
        case 0xEA: write(fetch16(), a); break;
        case 0xFA: a = read(fetch16()); break;

        case 0xE8: {  // ADD SP, e
            u8 off = fetch8();
            tick();
            tick();
            sp = add_sp_e(off);
            break;
        }
        case 0xF8: {  // LD HL, SP+e
            u8 off = fetch8();
            tick();
            set_hl(add_sp_e(off));
            break;
        }
        case 0xF9:  // LD SP, HL
            tick();
            sp = hl();
            break;

        case 0xF3:  // DI
            ime_ = false;
            ei_pending_ = false;
            break;
        case 0xFB: ei_pending_ = true; break;  // EI

        default:
            // D3 DB DD E3 E4 EB EC ED F4 FC FD: the CPU hangs.
            locked_ = true;
            break;
    }
}

void Cpu::serialize(StateIO& s) {
    s(a);
    s(f);
    s(b);
    s(c);
    s(d);
    s(e);
    s(h);
    s(l);
    s(sp);
    s(pc);
    s(ime_);
    s(ei_pending_);
    s(halted_);
    s(halt_bug_);
    s(stopped_);
    s(locked_);
}

}  // namespace gb
