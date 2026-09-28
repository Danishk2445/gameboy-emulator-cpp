#include "debugger.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "core/disasm.hpp"

namespace gbemu {

namespace {

constexpr int kWidth = 1300;
constexpr int kHeight = 860;
constexpr float kTextScale = 2;
constexpr float kRow = 18;

constexpr SDL_Color kText{0xDC, 0xDC, 0xDC, 255};
constexpr SDL_Color kDim{0x88, 0x88, 0x90, 255};
constexpr SDL_Color kHeader{0xFF, 0xCC, 0x66, 255};
constexpr SDL_Color kPc{0x66, 0xFF, 0x99, 255};
constexpr SDL_Color kBreak{0xFF, 0x66, 0x66, 255};

std::string fmt(const char* f, auto... args) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), f, args...);
    return buf;
}

gb::u32 shade_color(const Palette& palette, gb::u8 reg, int color_index) {
    return palette[(reg >> (color_index * 2)) & 3];
}

}  // namespace

bool Debugger::open() {
    if (window_) return true;
    if (!SDL_CreateWindowAndRenderer("gbemu debugger", kWidth, kHeight, 0, &window_, &renderer_)) {
        std::fprintf(stderr, "warning: cannot open debugger window: %s\n", SDL_GetError());
        window_ = nullptr;
        renderer_ = nullptr;
        return false;
    }
    SDL_SetRenderVSync(renderer_, 0);
    tiles_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 192);
    map_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 256);
    SDL_SetTextureScaleMode(tiles_, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureScaleMode(map_, SDL_SCALEMODE_NEAREST);
    return true;
}

void Debugger::close() {
    if (tiles_) SDL_DestroyTexture(tiles_);
    if (map_) SDL_DestroyTexture(map_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    tiles_ = map_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
}

void Debugger::text(float x, float y, const std::string& s, SDL_Color color) {
    SDL_SetRenderScale(renderer_, kTextScale, kTextScale);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x / kTextScale, y / kTextScale, s.c_str());
    SDL_SetRenderScale(renderer_, 1, 1);
}

void Debugger::draw_tiles(const gb::GameBoy& gb, const Palette& palette) {
    const auto& vram = gb.ppu.vram();
    std::array<gb::u32, 128 * 192> px;
    for (int t = 0; t < 384; ++t) {
        int ox = (t % 16) * 8, oy = (t / 16) * 8;
        for (int r = 0; r < 8; ++r) {
            gb::u8 lo = vram[t * 16 + r * 2], hi = vram[t * 16 + r * 2 + 1];
            for (int c = 0; c < 8; ++c) {
                int bit = 7 - c;
                int idx = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
                px[(oy + r) * 128 + ox + c] = shade_color(palette, gb.ppu.bgp(), idx);
            }
        }
    }
    SDL_UpdateTexture(tiles_, nullptr, px.data(), 128 * 4);
}

void Debugger::draw_map(const gb::GameBoy& gb, const Palette& palette) {
    const auto& vram = gb.ppu.vram();
    bool unsigned_tiles = gb.ppu.lcdc() & 0x10;
    int base = map_9c00_ ? 0x1C00 : 0x1800;
    std::array<gb::u32, 256 * 256> px;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 32; ++tx) {
            gb::u8 tile = vram[base + ty * 32 + tx];
            int addr = unsigned_tiles ? tile * 16 : 0x1000 + static_cast<gb::i8>(tile) * 16;
            for (int r = 0; r < 8; ++r) {
                gb::u8 lo = vram[addr + r * 2], hi = vram[addr + r * 2 + 1];
                for (int c = 0; c < 8; ++c) {
                    int bit = 7 - c;
                    int idx = (((hi >> bit) & 1) << 1) | ((lo >> bit) & 1);
                    px[(ty * 8 + r) * 256 + tx * 8 + c] = shade_color(palette, gb.ppu.bgp(), idx);
                }
            }
        }
    }
    SDL_UpdateTexture(map_, nullptr, px.data(), 256 * 4);
}

void Debugger::render(gb::GameBoy& gb, bool paused, const Palette& palette) {
    SDL_SetRenderDrawColor(renderer_, 0x1E, 0x1E, 0x24, 255);
    SDL_RenderClear(renderer_);
    auto rd = [&](gb::u16 addr) { return gb.bus.debug_read(addr); };
    const gb::Cpu& cpu = gb.cpu;

    // ---- Left column: CPU, code, IO ----
    float x = 12, y = 12;
    text(x, y, paused ? "CPU  (paused)" : "CPU  (running)", kHeader);
    y += kRow;
    text(x, y, fmt("AF %04X   BC %04X", cpu.af(), cpu.bc()), kText);
    y += kRow;
    text(x, y, fmt("DE %04X   HL %04X", cpu.de(), cpu.hl()), kText);
    y += kRow;
    text(x, y, fmt("SP %04X   PC %04X", cpu.sp, cpu.pc), kText);
    y += kRow;
    text(x, y,
         fmt("Z%d N%d H%d C%d  IME %d", (cpu.f >> 7) & 1, (cpu.f >> 6) & 1, (cpu.f >> 5) & 1, (cpu.f >> 4) & 1,
             cpu.ime() ? 1 : 0),
         kText);
    y += kRow;
    text(x, y, fmt("HALT %d  STOP %d  %s", cpu.halted() ? 1 : 0, cpu.stopped() ? 1 : 0, cpu.locked() ? "LOCKED" : ""),
         kText);
    y += kRow;
    text(x, y, fmt("Frame %llu", static_cast<unsigned long long>(gb.frame_count())), kDim);
    y += kRow * 1.5f;

    text(x, y, "Code", kHeader);
    y += kRow;
    gb::u16 addr = cpu.pc;
    for (int i = 0; i < 17; ++i) {
        gb::Disassembly d = gb::disassemble(addr, rd);
        std::string bytes;
        for (int b = 0; b < d.length; ++b) bytes += fmt("%02X", rd(static_cast<gb::u16>(addr + b)));
        bool bp = gb.has_breakpoint(addr);
        std::string line = fmt("%c%04X %-6s %s", i == 0 ? '>' : (bp ? '*' : ' '), addr, bytes.c_str(), d.text.c_str());
        text(x, y, line, i == 0 ? kPc : (bp ? kBreak : kText));
        y += kRow;
        addr = static_cast<gb::u16>(addr + d.length);
    }
    y += kRow * 0.5f;

    text(x, y, "IO", kHeader);
    y += kRow;
    text(x, y, fmt("LCDC %02X STAT %02X LY %02X", rd(0xFF40), rd(0xFF41), rd(0xFF44)), kText);
    y += kRow;
    text(x, y, fmt("LYC %02X  SCX %02X  SCY %02X", rd(0xFF45), rd(0xFF43), rd(0xFF42)), kText);
    y += kRow;
    text(x, y, fmt("WX %02X   WY %02X   DMA %02X", rd(0xFF4B), rd(0xFF4A), rd(0xFF46)), kText);
    y += kRow;
    text(x, y, fmt("IE %02X   IF %02X   P1 %02X", rd(0xFFFF), rd(0xFF0F), rd(0xFF00)), kText);
    y += kRow;
    text(x, y, fmt("DIV %02X TIMA %02X TMA %02X", rd(0xFF04), rd(0xFF05), rd(0xFF06)), kText);
    y += kRow;
    text(x, y, fmt("TAC %02X  SB %02X   SC %02X", rd(0xFF07), rd(0xFF01), rd(0xFF02)), kText);
    y += kRow;
    text(x, y, fmt("NR52 %02X NR50 %02X NR51 %02X", rd(0xFF26), rd(0xFF24), rd(0xFF25)), kText);
    y += kRow * 1.5f;

    text(x, y, "P pause  F7 step  F8 frame", kDim);
    y += kRow;
    text(x, y, "M BG map  F10 close", kDim);

    // ---- Tiles ----
    const float tiles_x = 500, tiles_y = 36;
    text(tiles_x, 12, "Tiles 8000-97FF", kHeader);
    draw_tiles(gb, palette);
    SDL_FRect tiles_dst{tiles_x, tiles_y, 256, 384};
    SDL_RenderTexture(renderer_, tiles_, nullptr, &tiles_dst);

    // ---- Palettes ----
    float py = tiles_y + 384 + 16;
    const std::pair<const char*, gb::u8> pals[] = {
        {"BGP ", gb.ppu.bgp()}, {"OBP0", gb.ppu.obp0()}, {"OBP1", gb.ppu.obp1()}};
    for (const auto& [name, reg] : pals) {
        text(tiles_x, py + 2, fmt("%s %02X", name, reg), kText);
        for (int i = 0; i < 4; ++i) {
            gb::u32 c = shade_color(palette, reg, i);
            SDL_SetRenderDrawColor(renderer_, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
            SDL_FRect sw{tiles_x + 136 + i * 30.0f, py, 26, 18};
            SDL_RenderFillRect(renderer_, &sw);
        }
        py += 24;
    }

    // ---- BG map with viewport ----
    const float map_x = 776, map_y = 36;
    text(map_x, 12, fmt("BG map %s  (LCDC.3=%d)", map_9c00_ ? "9C00" : "9800", (gb.ppu.lcdc() >> 3) & 1), kHeader);
    draw_map(gb, palette);
    SDL_FRect map_dst{map_x, map_y, 512, 512};
    SDL_RenderTexture(renderer_, map_, nullptr, &map_dst);

    SDL_SetRenderDrawColor(renderer_, 0xFF, 0x40, 0x40, 255);
    auto hline = [&](int my, int mx0, int len) {
        int start = mx0 & 0xFF;
        while (len > 0) {
            int piece = std::min(len, 256 - start);
            SDL_FRect r{map_x + start * 2.0f, map_y + (my & 0xFF) * 2.0f, piece * 2.0f, 2};
            SDL_RenderFillRect(renderer_, &r);
            len -= piece;
            start = 0;
        }
    };
    auto vline = [&](int mx, int my0, int len) {
        int start = my0 & 0xFF;
        while (len > 0) {
            int piece = std::min(len, 256 - start);
            SDL_FRect r{map_x + (mx & 0xFF) * 2.0f, map_y + start * 2.0f, 2, piece * 2.0f};
            SDL_RenderFillRect(renderer_, &r);
            len -= piece;
            start = 0;
        }
    };
    int sx = gb.ppu.scx(), sy = gb.ppu.scy();
    hline(sy, sx, 160);
    hline(sy + 143, sx, 160);
    vline(sx, sy, 144);
    vline(sx + 159, sy, 144);

    // ---- OAM ----
    const float oam_x = 500, oam_y = 560;
    text(oam_x, oam_y, "OAM", kHeader);
    for (int col = 0; col < 3; ++col) text(oam_x + col * 264.0f, oam_y + kRow, "#  Y  X  T  F", kDim);
    const auto& oam = gb.ppu.oam();
    for (int i = 0; i < 40; ++i) {
        int col = i / 14, row = i % 14;
        float ex = oam_x + col * 264.0f;
        float ey = oam_y + kRow * (row + 2);
        const gb::u8* s = &oam[i * 4];
        bool visible = s[0] > 0 && s[0] < 160 && s[1] > 0 && s[1] < 168;
        text(ex, ey, fmt("%02d %02X %02X %02X %02X", i, s[0], s[1], s[2], s[3]), visible ? kText : kDim);
    }

    SDL_RenderPresent(renderer_);
}

}  // namespace gbemu
