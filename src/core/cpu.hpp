#pragma once

#include "bus.hpp"
#include "common.hpp"
#include "state.hpp"

namespace gb {

// Sharp SM83. Every memory access goes through the Bus, which advances the
// rest of the system by one M-cycle, so instruction timing falls out naturally.
class Cpu {
public:
    Cpu(Bus& bus, Interrupts& irq) : bus_(bus), irq_(irq) {}

    void reset(bool post_boot, bool header_checksum_nonzero);
    void step();  // one instruction, one halted M-cycle, or one interrupt dispatch

    u8 a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    u16 sp = 0, pc = 0;

    u16 af() const { return static_cast<u16>((a << 8) | f); }
    u16 bc() const { return static_cast<u16>((b << 8) | c); }
    u16 de() const { return static_cast<u16>((d << 8) | e); }
    u16 hl() const { return static_cast<u16>((h << 8) | l); }

    bool ime() const { return ime_; }
    bool halted() const { return halted_; }
    bool stopped() const { return stopped_; }
    bool locked() const { return locked_; }

    // Set whenever LD B,B (0x40) executes: the Mooneye / dmg-acid2 "test finished" signal.
    bool ld_b_b_hit = false;

    void serialize(StateIO& s);

private:
    enum Flag : u8 { kZ = 0x80, kN = 0x40, kH = 0x20, kC = 0x10 };

    u8 read(u16 addr) { return bus_.read(addr); }
    void write(u16 addr, u8 v) { bus_.write(addr, v); }
    void tick() { bus_.tick(); }
    u8 fetch8() { return read(pc++); }
    u16 fetch16() {
        u8 lo = fetch8();
        u8 hi = fetch8();
        return static_cast<u16>((hi << 8) | lo);
    }
    void push16(u16 v);
    u16 pop16();

    void set_bc(u16 v) { b = v >> 8, c = v & 0xFF; }
    void set_de(u16 v) { d = v >> 8, e = v & 0xFF; }
    void set_hl(u16 v) { h = v >> 8, l = v & 0xFF; }
    void set_af(u16 v) { a = v >> 8, f = v & 0xF0; }
    u16 rp(int idx) const;  // BC, DE, HL, SP
    void set_rp(int idx, u16 v);

    bool flag(Flag fl) const { return f & fl; }
    void set_flags(bool z, bool n, bool hc, bool cy) {
        f = static_cast<u8>((z ? kZ : 0) | (n ? kN : 0) | (hc ? kH : 0) | (cy ? kC : 0));
    }
    bool condition(int cc) const;

    u8 r8(int idx);  // B C D E H L (HL) A
    void set_r8(int idx, u8 v);

    void execute(u8 op);
    void execute_cb();
    void service_interrupt();

    void alu(int op, u8 v);
    u8 inc8(u8 v);
    u8 dec8(u8 v);
    void add_hl(u16 v);
    u16 add_sp_e(u8 e);
    void daa();
    u8 rotate_shift(int op, u8 v);

    Bus& bus_;
    Interrupts& irq_;
    bool ime_ = false;
    bool ei_pending_ = false;
    bool halted_ = false;
    bool halt_bug_ = false;
    bool stopped_ = false;
    bool locked_ = false;  // executed an illegal opcode
};

}  // namespace gb
