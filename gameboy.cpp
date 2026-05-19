#include "gameboy.h"
#include "memory.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "ui.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t kStateMagic   = 0x53574247u; // 'GBWS' (Game Boy Write State)
constexpr uint32_t kStateVersion = 1;

constexpr uint32_t PALETTES[][4] = {
    // 0: GREEN (DMG classic)
    { 0xFF9BBC0Fu, 0xFF8BAC0Fu, 0xFF306230u, 0xFF0F380Fu },
    // 1: GRAY
    { 0xFFFFFFFFu, 0xFFAAAAAAu, 0xFF555555u, 0xFF000000u },
    // 2: POCKET (slate)
    { 0xFFE0E0DAu, 0xFFAEB1B2u, 0xFF6F7378u, 0xFF34383Cu },
    // 3: BGB (deep green)
    { 0xFFC4D8B0u, 0xFF7BB05Eu, 0xFF347746u, 0xFF1B2A1Bu },
};
constexpr int PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);
constexpr const char* PALETTE_NAMES[] = { "GREEN", "GRAY", "POCKET", "BGB" };
}

GameBoy::GameBoy() = default;

GameBoy::~GameBoy() {
    delete ui;
    delete cpu;
    delete ppu;
    delete apu;
    delete memory;
    if (texture)  SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    SDL_Quit();
}

bool GameBoy::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }
    window = SDL_CreateWindow("Game Boy",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCREEN_WIDTH * windowScale, SCREEN_HEIGHT * windowScale,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n'; return false; }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n'; return false; }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture) { std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n'; return false; }

    memory = new Memory();
    cpu    = new CPU(*memory);
    ppu    = new PPU(*memory);
    apu    = new APU(*memory);
    memory->setCPU(cpu);
    memory->setPPU(ppu);
    memory->setAPU(apu);
    if (!apu->init()) {
        std::cerr << "APU init failed, continuing without sound\n";
    }
    setPaletteByIndex(paletteIdx);

    ui = new UI(renderer);
    ui->setSlot(saveSlot);
    ui->setPaletteName(paletteName(paletteIdx));
    return true;
}

bool GameBoy::loadROM(const std::string& path) {
    if (!memory->loadROM(path)) return false;
    cpu->reset();
    ppu->reset();
    apu->reset();
    setPaletteByIndex(paletteIdx);
    if (ui) {
        ui->setRomLoaded(true);
        ui->closeMenu();
        ui->toast("LOADED " + path);
    }
    return true;
}

bool GameBoy::unloadCurrentROM() {
    if (!memory->hasROM()) return false;
    memory->unloadROM();
    cpu->reset();
    ppu->reset();
    apu->reset();
    if (ui) ui->setRomLoaded(false);
    return true;
}

void GameBoy::resetGame() {
    if (!memory->hasROM()) return;
    std::string path = memory->romPath();
    memory->unloadROM();
    memory->loadROM(path);
    cpu->reset();
    ppu->reset();
    apu->reset();
    setPaletteByIndex(paletteIdx);
    if (ui) ui->toast("RESET");
}

void GameBoy::setPaletteByIndex(int idx) {
    if (idx < 0) idx = PALETTE_COUNT - 1;
    if (idx >= PALETTE_COUNT) idx = 0;
    paletteIdx = idx;
    if (ppu) ppu->setPalette(PALETTES[paletteIdx]);
    if (ui)  ui->setPaletteName(paletteName(paletteIdx));
}

const char* GameBoy::paletteName(int idx) const {
    if (idx < 0 || idx >= PALETTE_COUNT) return "?";
    return PALETTE_NAMES[idx];
}

void GameBoy::toggleFullscreen() {
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (ui) {
        ui->setFullscreenView(fullscreen);
        ui->toast(fullscreen ? "FULLSCREEN ON" : "FULLSCREEN OFF");
    }
}

std::string GameBoy::statePathForSlot(int slot) const {
    std::string base = memory->romPath();
    if (base.empty()) return {};
    auto dot = base.find_last_of('.');
    std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
    return stem + ".state" + std::to_string(slot);
}

bool GameBoy::saveStateToFile(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&kStateMagic), sizeof(kStateMagic));
    f.write(reinterpret_cast<const char*>(&kStateVersion), sizeof(kStateVersion));
    cpu->saveState(f);
    ppu->saveState(f);
    apu->saveState(f);
    memory->saveState(f);
    return static_cast<bool>(f);
}

bool GameBoy::loadStateFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (magic != kStateMagic || version != kStateVersion) return false;
    if (!cpu->loadState(f)) return false;
    if (!ppu->loadState(f)) return false;
    if (!apu->loadState(f)) return false;
    if (!memory->loadState(f)) return false;
    return true;
}

void GameBoy::saveStateSlot(int slot) {
    if (!memory->hasROM()) {
        if (ui) ui->toast("NO ROM LOADED");
        return;
    }
    std::string path = statePathForSlot(slot);
    if (saveStateToFile(path)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "SAVED SLOT %d", slot);
        if (ui) ui->toast(buf);
    } else {
        if (ui) ui->toast("SAVE FAILED");
    }
}

void GameBoy::loadStateSlot(int slot) {
    if (!memory->hasROM()) {
        if (ui) ui->toast("NO ROM LOADED");
        return;
    }
    std::string path = statePathForSlot(slot);
    if (loadStateFromFile(path)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LOADED SLOT %d", slot);
        if (ui) ui->toast(buf);
    } else {
        if (ui) ui->toast("LOAD FAILED");
    }
}

void GameBoy::takeScreenshot() {
    if (!memory->hasROM()) {
        if (ui) ui->toast("NO ROM LOADED");
        return;
    }
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    char fname[128];
    std::snprintf(fname, sizeof(fname), "screenshot_%04d%02d%02d_%02d%02d%02d.bmp",
                  lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                  lt->tm_hour, lt->tm_min, lt->tm_sec);
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        if (ui) ui->toast("SCREENSHOT FAILED");
        return;
    }
    SDL_LockSurface(surf);
    std::memcpy(surf->pixels, ppu->getFramebuffer(),
                SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    SDL_UnlockSurface(surf);
    int ok = SDL_SaveBMP(surf, fname);
    SDL_FreeSurface(surf);
    if (ui) ui->toast(ok == 0 ? std::string("SAVED ") + fname : "SCREENSHOT FAILED");
}

void GameBoy::pollInput() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            running = false;
            return;
        }
        if (ev.type == SDL_KEYDOWN) {
            SDL_Keycode k = ev.key.keysym.sym;

            // Hotkeys that work even when the menu is open.
            switch (k) {
                case SDLK_F1:
                    if (ui->isMenuOpen()) {
                        // Only close if we have a ROM to fall back to; otherwise
                        // keep the title menu visible.
                        if (memory->hasROM()) ui->closeMenu();
                    } else {
                        ui->openInGameMenu();
                    }
                    continue;
                case SDLK_F2: saveStateSlot(saveSlot); continue;
                case SDLK_F4: loadStateSlot(saveSlot); continue;
                case SDLK_F6:
                    saveSlot = (saveSlot + 9) % 10;
                    ui->setSlot(saveSlot);
                    ui->toast("SLOT " + std::to_string(saveSlot));
                    continue;
                case SDLK_F7:
                    saveSlot = (saveSlot + 1) % 10;
                    ui->setSlot(saveSlot);
                    ui->toast("SLOT " + std::to_string(saveSlot));
                    continue;
                case SDLK_F8: takeScreenshot(); continue;
                case SDLK_F9: resetGame(); continue;
                case SDLK_F11: toggleFullscreen(); continue;
                case SDLK_m:
                    if (ui->isMenuOpen()) break;
                    muted = !muted;
                    apu->setMuted(muted);
                    ui->setMutedView(muted);
                    ui->toast(muted ? "MUTED" : "UNMUTED");
                    continue;
                case SDLK_p:
                    if (ui->isMenuOpen()) break;
                    if (memory->hasROM()) {
                        paused = !paused;
                        ui->setPaused(paused);
                        ui->toast(paused ? "PAUSED" : "RESUMED");
                    }
                    continue;
                default: break;
            }

            // If menu is open, route to UI.
            if (ui->isMenuOpen()) {
                ui->handleKey(k);
                continue;
            }

            if (k == SDLK_ESCAPE) {
                if (memory->hasROM()) ui->openInGameMenu();
                else                  ui->openTitleMenu();
                continue;
            }
        }
    }

    // Apply joypad input only when game is running and no menu is up.
    if (!ui->isMenuOpen() && !paused && memory->hasROM()) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        uint8_t btn = 0x0F;
        uint8_t dp  = 0x0F;
        if (ks[SDL_SCANCODE_Z])         btn &= ~0x01;
        if (ks[SDL_SCANCODE_X])         btn &= ~0x02;
        if (ks[SDL_SCANCODE_BACKSPACE]) btn &= ~0x04;
        if (ks[SDL_SCANCODE_RETURN])    btn &= ~0x08;
        if (ks[SDL_SCANCODE_RIGHT])     dp  &= ~0x01;
        if (ks[SDL_SCANCODE_LEFT])      dp  &= ~0x02;
        if (ks[SDL_SCANCODE_UP])        dp  &= ~0x04;
        if (ks[SDL_SCANCODE_DOWN])      dp  &= ~0x08;
        fastForward = ks[SDL_SCANCODE_SPACE] != 0;
        buttons = btn;
        dpad    = dp;
        memory->setJoypadState(buttons, dpad);
    } else {
        // Release joypad when menu is open.
        memory->setJoypadState(0x0F, 0x0F);
        fastForward = false;
    }
    ui->setFastForwardView(fastForward);
}

void GameBoy::applyUIAction() {
    UI::Action a = ui->pollAction();
    if (a.quit) running = false;
    if (a.resetRequested) resetGame();
    if (a.quitToMenuRequested) {
        memory->saveSRAM();
        unloadCurrentROM();
        ui->openTitleMenu();
    }
    if (!a.loadRomPath.empty()) {
        memory->saveSRAM();
        unloadCurrentROM();
        if (!loadROM(a.loadRomPath)) {
            ui->toast("LOAD FAILED");
            ui->openTitleMenu();
        }
    }
    if (a.saveStateRequested) saveStateSlot(saveSlot);
    if (a.loadStateRequested) loadStateSlot(saveSlot);
    if (a.changeSlot != 0) {
        saveSlot = (saveSlot + a.changeSlot + 10) % 10;
        ui->setSlot(saveSlot);
    }
    if (a.toggleMute) {
        muted = !muted;
        apu->setMuted(muted);
        ui->setMutedView(muted);
    }
    if (a.toggleFullscreen) toggleFullscreen();
    if (a.screenshotRequested) takeScreenshot();
    if (a.changePalette != 0) {
        setPaletteByIndex(paletteIdx + a.changePalette);
        ui->toast(std::string("PALETTE ") + paletteName(paletteIdx));
    }
}

void GameBoy::runOneFrame() {
    int frameCycles = 0;
    while (frameCycles < CYCLES_PER_FRAME && running) {
        int c = cpu->step();
        memory->updateTimer(c);
        memory->updateDMA(c);
        ppu->step(c);
        apu->step(c);
        frameCycles += c;
        if (ppu->isFrameReady()) {
            ppu->clearFrameReady();
        }
    }
}

void GameBoy::presentFrame() {
    if (memory->hasROM()) {
        SDL_UpdateTexture(texture, nullptr, ppu->getFramebuffer(),
                          SCREEN_WIDTH * sizeof(uint32_t));
    }
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(renderer, &outW, &outH);
    ui->render(texture, outW, outH);
}

void GameBoy::run() {
    running = true;
    if (!memory->hasROM()) {
        ui->openTitleMenu();
    }

    using clock = std::chrono::high_resolution_clock;
    auto last = clock::now();

    while (running) {
        pollInput();
        applyUIAction();

        if (memory->hasROM() && !paused && !ui->isMenuOpen()) {
            int frames = fastForward ? FAST_FORWARD_FRAMES : 1;
            for (int i = 0; i < frames && running; ++i) {
                runOneFrame();
            }
        }

        presentFrame();

        // Pace to 59.7275 Hz unless fast-forwarding.
        auto now = clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(now - last).count();
        double target = (memory->hasROM() && !paused && !ui->isMenuOpen() && !fastForward)
                            ? FRAME_TIME
                            : (1000.0 / 60.0);
        if (elapsed < target) {
            std::this_thread::sleep_for(
                std::chrono::duration<double, std::milli>(target - elapsed));
        }
        last = clock::now();
    }

    memory->saveSRAM();
}
