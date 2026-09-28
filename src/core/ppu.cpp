#include "ppu.hpp"

#include <algorithm>

namespace gb {

void Ppu::reset(bool post_boot) {
    vram_.fill(0);
    oam_.fill(0);
    fb_.fill(0);
    stat_ = 0;
    scy_ = scx_ = lyc_ = 0;
    wy_ = wx_ = 0;
    obp0_ = obp1_ = 0xFF;
    window_line_ = 0;
    wy_triggered_ = false;
    skip_frame_ = false;
    off_dots_ = 0;
    frame_ready_ = false;
    stat_line_ = false;
    sprite_count_ = 0;
    if (post_boot) {
        // State at PC=0x0100 after the DMG boot ROM: LCD on, near the end of line 153.
        lcdc_ = 0x91;
        bgp_ = 0xFC;
        ly_ = 153;
        dot_ = 400;
        mode_ = 1;
    } else {
        lcdc_ = 0;
        bgp_ = 0;
        ly_ = 0;
        dot_ = 0;
        mode_ = 0;
    }
    cpu_mode_ = mode_;
    update_lyc();
}

void Ppu::tick() {
    cpu_mode_ = mode_;
    for (int i = 0; i < 4; ++i) step_dot();
}

void Ppu::step_dot() {
    if (!lcd_on()) {
        // Keep producing frames at the normal rate so the frontend doesn't stall.
        if (++off_dots_ >= kCyclesPerFrame) {
            off_dots_ = 0;
            frame_ready_ = true;
        }
        return;
    }

    if (++dot_ == 456) {
        dot_ = 0;
        next_line();
    }
    if (ly_ < 144) {
        if (dot_ == 80) {
            start_mode3();
        } else if (dot_ == mode3_end_) {
            if (!skip_frame_) render_line();
            mode_ = 0;
        }
    }
    if (ly_ == 153 && dot_ == 4) update_lyc();  // LY reads 0 for the rest of line 153

    u8 enables = stat_;
    // At the start of line 144 the mode 2 interrupt source also fires.
    if (ly_ == 144 && dot_ == 0 && (stat_ & 0x20)) enables |= 0x10;
    update_stat_line(enables);
}

void Ppu::next_line() {
    ++ly_;
    if (ly_ == 144) {
        mode_ = 1;
        irq_.request_late(kIntVBlank);
        frame_ready_ = true;
        skip_frame_ = false;
    } else if (ly_ == 154) {
        ly_ = 0;
        window_line_ = 0;
        wy_triggered_ = false;
    }
    if (ly_ < 144) {
        mode_ = 2;
        if (ly_ == wy_) wy_triggered_ = true;
    }
    update_lyc();
}

void Ppu::select_sprites() {
    sprite_count_ = 0;
    int height = (lcdc_ & 0x04) ? 16 : 8;
    for (u8 i = 0; i < 40 && sprite_count_ < 10; ++i) {
        int y = oam_[i * 4];
        int line = ly_ + 16;
        if (line >= y && line < y + height) sprites_[sprite_count_++] = i;
    }
    // Drawing priority on DMG: lower X wins, ties go to the lower OAM index.
    std::stable_sort(sprites_.begin(), sprites_.begin() + sprite_count_,
                     [this](u8 a, u8 b) { return oam_[a * 4 + 1] < oam_[b * 4 + 1]; });
}

void Ppu::start_mode3() {
    mode_ = 3;
    select_sprites();

    int len = 172 + (scx_ & 7);
    if ((lcdc_ & 0x20) && wy_triggered_ && wx_ <= 166) len += 6;
    if (lcdc_ & 0x02) {
        // Approximate object fetch penalties (Pan Docs, "Mode 3 length").
        u32 penalized_tiles = 0;
        for (u8 n = 0; n < sprite_count_; ++n) {
            int x = oam_[sprites_[n] * 4 + 1];
            if (x >= 168) continue;
            if (x == 0) {
                len += 11;
                continue;
            }
            len += 6;
            u32 tile_bit = 1u << (((x + scx_) >> 3) & 31);
            if (!(penalized_tiles & tile_bit)) {
                penalized_tiles |= tile_bit;
                len += std::max(0, 5 - ((x + scx_) & 7));
            }
        }
    }
    mode3_end_ = static_cast<u16>(80 + std::min(len, 289));
}

u8 Ppu::bg_pixel(u16 map_base, u8 x, u8 y) const {
    u8 tile = vram_[(map_base + (y >> 3) * 32 + (x >> 3)) & 0x1FFF];
    u16 addr = (lcdc_ & 0x10) ? static_cast<u16>(tile * 16)
                              : static_cast<u16>(0x1000 + static_cast<i8>(tile) * 16);
    addr = static_cast<u16>(addr + (y & 7) * 2);
    u8 lo = vram_[addr & 0x1FFF];
    u8 hi = vram_[(addr + 1) & 0x1FFF];
    int bit = 7 - (x & 7);
    return static_cast<u8>((((hi >> bit) & 1) << 1) | ((lo >> bit) & 1));
}

void Ppu::render_line() {
    std::array<u8, kScreenWidth> bg{};  // colour indices before palette, used for OBJ priority
    u8* out = fb_.data() + ly_ * kScreenWidth;

    bool window_visible = (lcdc_ & 0x20) && wy_triggered_ && wx_ <= 166;

    // On DMG, LCDC bit 0 blanks both background and window.
    if (lcdc_ & 0x01) {
        u16 bg_map = (lcdc_ & 0x08) ? 0x1C00 : 0x1800;
        u8 y = static_cast<u8>(ly_ + scy_);
        for (int x = 0; x < kScreenWidth; ++x) bg[x] = bg_pixel(bg_map, static_cast<u8>(x + scx_), y);

        if (window_visible) {
            u16 win_map = (lcdc_ & 0x40) ? 0x1C00 : 0x1800;
            int start = wx_ - 7;
            for (int x = std::max(start, 0); x < kScreenWidth; ++x) {
                bg[x] = bg_pixel(win_map, static_cast<u8>(x - start), window_line_);
            }
        }
    }
    if (window_visible) ++window_line_;

    for (int x = 0; x < kScreenWidth; ++x) out[x] = (bgp_ >> (bg[x] * 2)) & 3;

    if (!(lcdc_ & 0x02)) return;

    int height = (lcdc_ & 0x04) ? 16 : 8;
    for (int x = 0; x < kScreenWidth; ++x) {
        for (u8 n = 0; n < sprite_count_; ++n) {
            const u8* s = &oam_[sprites_[n] * 4];
            int sx = s[1] - 8;
            if (x < sx || x >= sx + 8) continue;
            u8 attr = s[3];
            int row = ly_ + 16 - s[0];
            if (attr & 0x40) row = height - 1 - row;
            u8 tile = s[2];
            if (height == 16) tile = static_cast<u8>((tile & 0xFE) | (row >> 3));
            u16 addr = static_cast<u16>(tile * 16 + (row & 7) * 2);
            int col = x - sx;
            int bit = (attr & 0x20) ? col : 7 - col;
            u8 color = static_cast<u8>((((vram_[addr + 1] >> bit) & 1) << 1) | ((vram_[addr] >> bit) & 1));
            if (color == 0) continue;  // transparent: lower-priority objects may show through
            if (!((attr & 0x80) && bg[x] != 0)) {
                u8 pal = (attr & 0x10) ? obp1_ : obp0_;
                out[x] = (pal >> (color * 2)) & 3;
            }
            break;
        }
    }
}

void Ppu::update_lyc() { lyc_match_ = ly_reg() == lyc_; }

void Ppu::update_stat_line(u8 enables) {
    bool line = false;
    if (lcd_on()) {
        if ((enables & 0x40) && lyc_match_) line = true;
        switch (mode_) {
            case 0: line |= (enables & 0x08) != 0; break;
            case 1: line |= (enables & 0x10) != 0; break;
            case 2: line |= (enables & 0x20) != 0; break;
            default: break;
        }
    }
    if (line && !stat_line_) irq_.request_late(kIntStat);
    stat_line_ = line;
}

u8 Ppu::read_reg(u16 addr) const {
    switch (addr) {
        case 0xFF40: return lcdc_;
        case 0xFF41: {
            u8 v = 0x80 | stat_ | (lyc_match_ ? 0x04 : 0);
            return lcd_on() ? static_cast<u8>(v | cpu_mode_) : v;
        }
        case 0xFF42: return scy_;
        case 0xFF43: return scx_;
        case 0xFF44: return lcd_on() ? ly_reg() : 0;
        case 0xFF45: return lyc_;
        case 0xFF47: return bgp_;
        case 0xFF48: return obp0_;
        case 0xFF49: return obp1_;
        case 0xFF4A: return wy_;
        case 0xFF4B: return wx_;
        default: return 0xFF;
    }
}

void Ppu::write_reg(u16 addr, u8 value) {
    switch (addr) {
        case 0xFF40: {
            bool was_on = lcd_on();
            lcdc_ = value;
            if (was_on && !lcd_on()) {
                ly_ = 0;
                dot_ = 0;
                mode_ = 0;
                off_dots_ = 0;
                cpu_mode_ = 0;
                fb_.fill(0);
                stat_line_ = false;
            } else if (!was_on && lcd_on()) {
                ly_ = 0;
                dot_ = 0;
                mode_ = 0;  // line 0 after power-on skips the OAM scan mode
                cpu_mode_ = 0;
                window_line_ = 0;
                wy_triggered_ = (wy_ == 0);
                skip_frame_ = true;
                update_lyc();
                update_stat_line(stat_);
            }
            break;
        }
        case 0xFF41:
            // DMG quirk: a STAT write briefly behaves as if every source were enabled.
            update_stat_line(0x78);
            stat_ = value & 0x78;
            update_stat_line(stat_);
            break;
        case 0xFF42: scy_ = value; break;
        case 0xFF43: scx_ = value; break;
        case 0xFF44: break;  // read-only
        case 0xFF45:
            lyc_ = value;
            if (lcd_on()) {
                update_lyc();
                update_stat_line(stat_);
            }
            break;
        case 0xFF47: bgp_ = value; break;
        case 0xFF48: obp0_ = value; break;
        case 0xFF49: obp1_ = value; break;
        case 0xFF4A: wy_ = value; break;
        case 0xFF4B: wx_ = value; break;
    }
}

void Ppu::serialize(StateIO& s) {
    s(vram_);
    s(oam_);
    s(fb_);
    s(lcdc_);
    s(stat_);
    s(scy_);
    s(scx_);
    s(lyc_);
    s(bgp_);
    s(obp0_);
    s(obp1_);
    s(wy_);
    s(wx_);
    s(ly_);
    s(dot_);
    s(mode_);
    s(cpu_mode_);
    s(mode3_end_);
    s(lyc_match_);
    s(stat_line_);
    s(wy_triggered_);
    s(window_line_);
    s(skip_frame_);
    s(off_dots_);
    s(frame_ready_);
    s(sprite_count_);
    s(sprites_);
}

}  // namespace gb
