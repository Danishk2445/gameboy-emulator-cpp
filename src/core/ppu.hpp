#pragma once

#include <array>

#include "common.hpp"
#include "state.hpp"

namespace gb {

// DMG pixel processing unit. Timing (modes, STAT/LY/LYC, interrupts,
// VRAM/OAM locking) is emulated per dot; each scanline is rendered in one go
// when mode 3 ends, using the register values at that moment.
class Ppu {
public:
    using Framebuffer = std::array<u8, kScreenWidth * kScreenHeight>;  // shades 0 (white) - 3 (black)

    explicit Ppu(Interrupts& irq) : irq_(irq) {}

    void reset(bool post_boot);
    void tick();  // one M-cycle = 4 dots

    u8 read_reg(u16 addr) const;
    void write_reg(u16 addr, u8 value);

    // CPU accesses land early in the M-cycle, so they see the mode as it was when the cycle began.
    bool vram_blocked() const { return lcd_on() && cpu_mode_ == 3; }
    bool oam_blocked() const { return lcd_on() && (cpu_mode_ == 2 || cpu_mode_ == 3); }

    u8 cpu_read_vram(u16 addr) const { return vram_blocked() ? 0xFF : vram_[addr & 0x1FFF]; }
    void cpu_write_vram(u16 addr, u8 value) {
        if (!vram_blocked()) vram_[addr & 0x1FFF] = value;
    }
    u8 cpu_read_oam(u16 addr) const { return oam_blocked() ? 0xFF : oam_[addr & 0xFF]; }
    void cpu_write_oam(u16 addr, u8 value) {
        if (!oam_blocked()) oam_[addr & 0xFF] = value;
    }
    u8 vram_byte(u16 addr) const { return vram_[addr & 0x1FFF]; }
    void dma_write_oam(u8 index, u8 value) { oam_[index] = value; }

    // Set when a frame is finished (start of VBlank, or every 70224 dots while the LCD is off).
    bool frame_ready() const { return frame_ready_; }
    void clear_frame_ready() { frame_ready_ = false; }

    const Framebuffer& framebuffer() const { return fb_; }
    const std::array<u8, 0x2000>& vram() const { return vram_; }
    const std::array<u8, 0xA0>& oam() const { return oam_; }

    bool lcd_on() const { return lcdc_ & 0x80; }
    u8 lcdc() const { return lcdc_; }
    u8 mode() const { return mode_; }
    u8 ly() const { return ly_; }
    u8 scx() const { return scx_; }
    u8 scy() const { return scy_; }
    u8 wx() const { return wx_; }
    u8 wy() const { return wy_; }
    u8 bgp() const { return bgp_; }
    u8 obp0() const { return obp0_; }
    u8 obp1() const { return obp1_; }
    u16 dot() const { return dot_; }

    void serialize(StateIO& s);

private:
    void step_dot();
    void next_line();
    void start_mode3();
    void render_line();
    void select_sprites();
    u8 ly_reg() const { return (ly_ == 153 && dot_ >= 4) ? 0 : ly_; }
    void update_lyc();
    void update_stat_line(u8 stat_enables);
    u8 bg_pixel(u16 map_base, u8 x, u8 y) const;

    Interrupts& irq_;

    std::array<u8, 0x2000> vram_{};
    std::array<u8, 0xA0> oam_{};
    Framebuffer fb_{};

    u8 lcdc_ = 0, stat_ = 0, scy_ = 0, scx_ = 0, lyc_ = 0;
    u8 bgp_ = 0, obp0_ = 0, obp1_ = 0, wy_ = 0, wx_ = 0;

    u8 ly_ = 0;
    u16 dot_ = 0;
    u8 mode_ = 0;
    u8 cpu_mode_ = 0;  // mode_ at the start of the current M-cycle
    u16 mode3_end_ = 252;
    bool lyc_match_ = false;
    bool stat_line_ = false;
    bool wy_triggered_ = false;
    u8 window_line_ = 0;
    bool skip_frame_ = false;  // first frame after the LCD is switched on is not displayed
    u32 off_dots_ = 0;
    bool frame_ready_ = false;

    u8 sprite_count_ = 0;
    std::array<u8, 10> sprites_{};  // OAM indices selected for the current line
};

}  // namespace gb
