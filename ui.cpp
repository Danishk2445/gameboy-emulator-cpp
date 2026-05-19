#include "ui.h"
#include "font.h"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {
constexpr int GLYPH_W = 8;
constexpr int GLYPH_H = 8;
constexpr int FONT_COUNT = 95;

constexpr uint32_t COLOR_TEXT       = 0xFFEAEAEAu; // light
constexpr uint32_t COLOR_TEXT_DIM   = 0xFF888888u;
constexpr uint32_t COLOR_TEXT_HL    = 0xFFFFD200u; // amber highlight
constexpr uint32_t COLOR_TITLE      = 0xFF9CC95Cu; // gameboy green-ish
constexpr uint32_t COLOR_BORDER     = 0xFF505050u;
constexpr uint32_t COLOR_TOAST      = 0xFFFFFFFFu;

constexpr SDL_Color rgbaToSdl(uint32_t c) {
    return SDL_Color{
        static_cast<uint8_t>((c >> 16) & 0xFF),
        static_cast<uint8_t>((c >>  8) & 0xFF),
        static_cast<uint8_t>( c        & 0xFF),
        static_cast<uint8_t>((c >> 24) & 0xFF)
    };
}
}

UI::UI(SDL_Renderer* r) : renderer(r) {
    buildFontTexture();
    rescanRoms();
}

UI::~UI() {
    if (fontTex) SDL_DestroyTexture(fontTex);
}

void UI::buildFontTexture() {
    const int texW = FONT_COUNT * GLYPH_W;
    const int texH = GLYPH_H;
    std::vector<uint32_t> pixels(texW * texH, 0);
    for (int g = 0; g < FONT_COUNT; ++g) {
        for (int row = 0; row < GLYPH_H; ++row) {
            uint8_t bits = kFont8x8[g][row];
            for (int col = 0; col < GLYPH_W; ++col) {
                bool on = (bits >> (7 - col)) & 1;
                int x = g * GLYPH_W + col;
                int y = row;
                pixels[y * texW + x] = on ? 0xFFFFFFFFu : 0x00000000u;
            }
        }
    }
    fontTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC, texW, texH);
    if (!fontTex) {
        std::cerr << "Font texture create failed: " << SDL_GetError() << '\n';
        return;
    }
    SDL_UpdateTexture(fontTex, nullptr, pixels.data(), texW * sizeof(uint32_t));
    SDL_SetTextureBlendMode(fontTex, SDL_BLENDMODE_BLEND);
}

void UI::drawText(int x, int y, const std::string& s, uint32_t rgba, int scale) {
    if (!fontTex) return;
    SDL_Color c = rgbaToSdl(rgba);
    SDL_SetTextureColorMod(fontTex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(fontTex, c.a);
    int cx = x;
    for (char ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        int idx = (uc >= 32 && uc <= 126) ? (uc - 32) : 0;
        SDL_Rect src{ idx * GLYPH_W, 0, GLYPH_W, GLYPH_H };
        SDL_Rect dst{ cx, y, GLYPH_W * scale, GLYPH_H * scale };
        SDL_RenderCopy(renderer, fontTex, &src, &dst);
        cx += GLYPH_W * scale;
    }
}

int UI::textWidth(const std::string& s, int scale) const {
    return static_cast<int>(s.size()) * GLYPH_W * scale;
}

void UI::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rect{ x, y, w, h };
    SDL_RenderFillRect(renderer, &rect);
}

void UI::drawBorder(int x, int y, int w, int h, uint32_t rgba, int thickness) {
    SDL_Color c = rgbaToSdl(rgba);
    fillRect(x, y, w, thickness, c.r, c.g, c.b, c.a);
    fillRect(x, y + h - thickness, w, thickness, c.r, c.g, c.b, c.a);
    fillRect(x, y, thickness, h, c.r, c.g, c.b, c.a);
    fillRect(x + w - thickness, y, thickness, h, c.r, c.g, c.b, c.a);
}

void UI::drawPanel(int x, int y, int w, int h) {
    fillRect(x, y, w, h, 16, 18, 32, 220);
    drawBorder(x, y, w, h, COLOR_BORDER, 2);
}

std::string UI::basename(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

void UI::rescanRoms() {
    romList.clear();
    std::error_code ec;
    auto isRom = [](const fs::path& p) {
        if (!p.has_extension()) return false;
        auto ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return ext == ".gb" || ext == ".gbc";
    };
    auto scan = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && isRom(entry.path())) {
                romList.push_back(entry.path().string());
            }
        }
    };
    scan(fs::current_path(ec));
    scan(fs::current_path(ec) / "roms");
    std::sort(romList.begin(), romList.end());
    romList.erase(std::unique(romList.begin(), romList.end()), romList.end());
}

void UI::openTitleMenu() {
    current = Screen::TitleMenu;
    titleSel = 0;
}

void UI::openInGameMenu() {
    if (!romLoaded) {
        openTitleMenu();
        return;
    }
    current = Screen::InGameMenu;
    ingameSel = 0;
}

void UI::closeMenu() {
    current = Screen::None;
}

void UI::toast(const std::string& msg, double durationSec) {
    toastMsg = msg;
    toastEnd = SDL_GetTicks() + static_cast<Uint32>(durationSec * 1000.0);
}

UI::Action UI::pollAction() {
    Action a = pending;
    pending = Action{};
    return a;
}

bool UI::handleKey(SDL_Keycode key) {
    switch (current) {
        case Screen::TitleMenu:   handleKeyTitle(key);    return true;
        case Screen::RomBrowser:  handleKeyBrowser(key);  return true;
        case Screen::InGameMenu:  handleKeyInGame(key);   return true;
        case Screen::Settings:    handleKeySettings(key); return true;
        case Screen::None:        return false;
    }
    return false;
}

void UI::handleKeyTitle(SDL_Keycode key) {
    const int N = 2; // LOAD ROM, QUIT
    switch (key) {
        case SDLK_UP:    titleSel = (titleSel + N - 1) % N; break;
        case SDLK_DOWN:  titleSel = (titleSel + 1) % N; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (titleSel == 0) {
                rescanRoms();
                browserSel = 0;
                browserScroll = 0;
                current = Screen::RomBrowser;
            } else if (titleSel == 1) {
                pending.quit = true;
            }
            break;
        case SDLK_ESCAPE:
            if (romLoaded) current = Screen::None;
            break;
        default: break;
    }
}

void UI::handleKeyBrowser(SDL_Keycode key) {
    int N = static_cast<int>(romList.size());
    switch (key) {
        case SDLK_UP:
            if (N > 0) browserSel = (browserSel + N - 1) % N;
            break;
        case SDLK_DOWN:
            if (N > 0) browserSel = (browserSel + 1) % N;
            break;
        case SDLK_PAGEUP:
            browserSel = std::max(0, browserSel - 10);
            break;
        case SDLK_PAGEDOWN:
            browserSel = std::min(N - 1, browserSel + 10);
            break;
        case SDLK_HOME:    browserSel = 0; break;
        case SDLK_END:     browserSel = std::max(0, N - 1); break;
        case SDLK_F5:      rescanRoms(); browserSel = 0; break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (browserSel >= 0 && browserSel < N) {
                pending.loadRomPath = romList[browserSel];
                current = Screen::None;
            }
            break;
        case SDLK_BACKSPACE:
        case SDLK_ESCAPE:
            current = Screen::TitleMenu;
            break;
        default: break;
    }
}

void UI::handleKeyInGame(SDL_Keycode key) {
    // Menu items: 0 Resume, 1 Save State, 2 Load State, 3 Slot, 4 Reset, 5 Settings, 6 Quit to Menu
    const int N = 7;
    switch (key) {
        case SDLK_UP:    ingameSel = (ingameSel + N - 1) % N; break;
        case SDLK_DOWN:  ingameSel = (ingameSel + 1) % N; break;
        case SDLK_LEFT:
            if (ingameSel == 3) pending.changeSlot = -1;
            break;
        case SDLK_RIGHT:
            if (ingameSel == 3) pending.changeSlot = +1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            switch (ingameSel) {
                case 0: current = Screen::None; break;                   // Resume
                case 1: pending.saveStateRequested = true; current = Screen::None; break;
                case 2: pending.loadStateRequested = true; current = Screen::None; break;
                case 3: pending.changeSlot = +1; break;
                case 4: pending.resetRequested = true; current = Screen::None; break;
                case 5: current = Screen::Settings; settingsSel = 0; break;
                case 6: pending.quitToMenuRequested = true; openTitleMenu(); break;
            }
            break;
        case SDLK_ESCAPE:
            current = Screen::None;
            break;
        default: break;
    }
}

void UI::handleKeySettings(SDL_Keycode key) {
    // 0 fullscreen, 1 audio mute, 2 palette, 3 take screenshot, 4 back
    const int N = 5;
    switch (key) {
        case SDLK_UP:    settingsSel = (settingsSel + N - 1) % N; break;
        case SDLK_DOWN:  settingsSel = (settingsSel + 1) % N; break;
        case SDLK_LEFT:
            if (settingsSel == 2) pending.changePalette = -1;
            break;
        case SDLK_RIGHT:
            if (settingsSel == 2) pending.changePalette = +1;
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            switch (settingsSel) {
                case 0: pending.toggleFullscreen = true; break;
                case 1: pending.toggleMute = true; break;
                case 2: pending.changePalette = +1; break;
                case 3: pending.screenshotRequested = true; break;
                case 4: current = Screen::InGameMenu; break;
            }
            break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
            current = Screen::InGameMenu;
            break;
        default: break;
    }
}

void UI::render(SDL_Texture* gameTexture, int winW, int winH) {
    SDL_RenderClear(renderer);
    if (gameTexture && romLoaded) {
        SDL_RenderCopy(renderer, gameTexture, nullptr, nullptr);
        if (isMenuOpen() || paused) {
            // Dim the game.
            fillRect(0, 0, winW, winH, 0, 0, 0, 160);
        }
    } else {
        // Solid background when no ROM is loaded.
        SDL_SetRenderDrawColor(renderer, 12, 14, 22, 255);
        SDL_RenderFillRect(renderer, nullptr);
    }

    switch (current) {
        case Screen::TitleMenu:  renderTitle(winW, winH); break;
        case Screen::RomBrowser: renderBrowser(winW, winH); break;
        case Screen::InGameMenu: renderInGame(winW, winH); break;
        case Screen::Settings:   renderSettings(winW, winH); break;
        case Screen::None:       break;
    }

    renderToast(winW, winH);

    if (current == Screen::None && fastForwardView && romLoaded) {
        drawText(winW - textWidth(">>", 2) - 16, 12, ">>", COLOR_TEXT_HL, 2);
    }

    SDL_RenderPresent(renderer);
}

void UI::renderTitle(int w, int h) {
    const int panelW = std::min(560, w - 40);
    const int panelH = 260;
    const int px = (w - panelW) / 2;
    const int py = (h - panelH) / 2;
    drawPanel(px, py, panelW, panelH);

    const std::string title = "GAME BOY EMULATOR";
    int tw = textWidth(title, 3);
    drawText(px + (panelW - tw) / 2, py + 20, title, COLOR_TITLE, 3);

    const char* items[] = { "LOAD ROM", "QUIT" };
    int y = py + 90;
    for (int i = 0; i < 2; ++i) {
        std::string line = (i == titleSel ? "> " : "  ") + std::string(items[i]);
        uint32_t col = (i == titleSel) ? COLOR_TEXT_HL : COLOR_TEXT;
        drawText(px + 60, y, line, col, 2);
        y += 32;
    }

    const std::string foot = "ENTER SELECT  ESC CLOSE";
    int fw = textWidth(foot, 1);
    drawText(px + (panelW - fw) / 2, py + panelH - 24, foot, COLOR_TEXT_DIM, 1);

    const std::string hint = "F1 MENU  F2 SAVE  F4 LOAD  P PAUSE  SPACE FF";
    int hw = textWidth(hint, 1);
    drawText((w - hw) / 2, h - 24, hint, COLOR_TEXT_DIM, 1);
}

void UI::renderBrowser(int w, int h) {
    const int panelW = std::min(760, w - 40);
    const int panelH = std::min(540, h - 40);
    const int px = (w - panelW) / 2;
    const int py = (h - panelH) / 2;
    drawPanel(px, py, panelW, panelH);

    const std::string title = "SELECT ROM";
    drawText(px + 20, py + 16, title, COLOR_TITLE, 2);

    int listX = px + 20;
    int listY = py + 60;
    int rowH = 24;
    int rows = (panelH - 100) / rowH;
    int N = static_cast<int>(romList.size());

    if (N == 0) {
        drawText(listX, listY,
                 "NO .GB FILES FOUND IN CURRENT DIRECTORY",
                 COLOR_TEXT_DIM, 2);
        drawText(listX, listY + 40,
                 "PLACE ROMS HERE OR IN ./ROMS/ AND PRESS F5",
                 COLOR_TEXT_DIM, 1);
    } else {
        if (browserSel < browserScroll) browserScroll = browserSel;
        if (browserSel >= browserScroll + rows) browserScroll = browserSel - rows + 1;

        for (int i = 0; i < rows; ++i) {
            int idx = browserScroll + i;
            if (idx >= N) break;
            bool selected = (idx == browserSel);
            std::string name = basename(romList[idx]);
            std::string line = (selected ? "> " : "  ") + name;
            uint32_t col = selected ? COLOR_TEXT_HL : COLOR_TEXT;
            drawText(listX, listY + i * rowH, line, col, 2);
        }
    }

    const std::string foot = "ENTER LOAD   ESC BACK   F5 RESCAN";
    int fw = textWidth(foot, 1);
    drawText(px + (panelW - fw) / 2, py + panelH - 24, foot, COLOR_TEXT_DIM, 1);
}

void UI::renderInGame(int w, int h) {
    const int panelW = std::min(540, w - 40);
    const int panelH = 360;
    const int px = (w - panelW) / 2;
    const int py = (h - panelH) / 2;
    drawPanel(px, py, panelW, panelH);

    drawText(px + 20, py + 16, "PAUSED", COLOR_TITLE, 2);

    char slotBuf[32];
    SDL_snprintf(slotBuf, sizeof(slotBuf), "[SLOT %d]", slot);
    std::string slotStr = slotBuf;

    struct Item { const char* label; std::string suffix; };
    Item items[] = {
        { "RESUME",        "" },
        { "SAVE STATE",    slotStr },
        { "LOAD STATE",    slotStr },
        { "STATE SLOT",    slotStr + "  < >" },
        { "RESET",         "" },
        { "SETTINGS",      "" },
        { "QUIT TO MENU",  "" },
    };
    int N = sizeof(items) / sizeof(items[0]);
    int y = py + 60;
    for (int i = 0; i < N; ++i) {
        std::string line = (i == ingameSel ? "> " : "  ") + std::string(items[i].label);
        uint32_t col = (i == ingameSel) ? COLOR_TEXT_HL : COLOR_TEXT;
        drawText(px + 30, y, line, col, 2);
        if (!items[i].suffix.empty()) {
            int sw = textWidth(items[i].suffix, 2);
            drawText(px + panelW - sw - 30, y, items[i].suffix, COLOR_TEXT_DIM, 2);
        }
        y += 30;
    }

    const std::string foot = "F1/ESC CLOSE   F2 SAVE   F4 LOAD";
    int fw = textWidth(foot, 1);
    drawText(px + (panelW - fw) / 2, py + panelH - 24, foot, COLOR_TEXT_DIM, 1);
}

void UI::renderSettings(int w, int h) {
    const int panelW = std::min(560, w - 40);
    const int panelH = 320;
    const int px = (w - panelW) / 2;
    const int py = (h - panelH) / 2;
    drawPanel(px, py, panelW, panelH);

    drawText(px + 20, py + 16, "SETTINGS", COLOR_TITLE, 2);

    struct Row { const char* label; std::string value; };
    Row rows[] = {
        { "FULLSCREEN", fullscreenView ? "ON" : "OFF" },
        { "AUDIO",      mutedView ? "MUTED" : "ON" },
        { "PALETTE",    paletteName },
        { "SCREENSHOT", "TAKE NOW" },
        { "BACK",       "" },
    };
    int N = sizeof(rows) / sizeof(rows[0]);
    int y = py + 60;
    for (int i = 0; i < N; ++i) {
        std::string line = (i == settingsSel ? "> " : "  ") + std::string(rows[i].label);
        uint32_t col = (i == settingsSel) ? COLOR_TEXT_HL : COLOR_TEXT;
        drawText(px + 30, y, line, col, 2);
        if (!rows[i].value.empty()) {
            int sw = textWidth(rows[i].value, 2);
            drawText(px + panelW - sw - 30, y, rows[i].value, COLOR_TEXT_DIM, 2);
        }
        y += 30;
    }

    const std::string foot = "ESC BACK   ENTER TOGGLE   LR CYCLE";
    int fw = textWidth(foot, 1);
    drawText(px + (panelW - fw) / 2, py + panelH - 24, foot, COLOR_TEXT_DIM, 1);
}

void UI::renderToast(int w, int h) {
    if (toastMsg.empty()) return;
    Uint32 now = SDL_GetTicks();
    if (now > toastEnd) {
        toastMsg.clear();
        return;
    }
    int tw = textWidth(toastMsg, 2);
    int boxW = tw + 32;
    int boxH = 28;
    int x = (w - boxW) / 2;
    int y = h - boxH - 20;
    fillRect(x, y, boxW, boxH, 0, 0, 0, 200);
    drawBorder(x, y, boxW, boxH, COLOR_BORDER, 1);
    drawText(x + 16, y + 6, toastMsg, COLOR_TOAST, 2);
}
