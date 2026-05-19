#include "gameboy.h"
#include <iostream>

int main(int argc, char* argv[]) {
    GameBoy gb;
    if (!gb.init()) {
        std::cerr << "Failed to initialize emulator\n";
        return 1;
    }

    if (argc >= 2) {
        if (!gb.loadROM(argv[1])) {
            std::cerr << "Failed to load ROM: " << argv[1]
                      << " — opening menu.\n";
        }
    }

    std::cout <<
        "Controls:\n"
        "  Arrows      D-Pad\n"
        "  Z / X       A / B\n"
        "  Enter       Start\n"
        "  Backspace   Select\n"
        "Hotkeys (RetroArch style):\n"
        "  F1          Menu / Pause overlay\n"
        "  F2          Save state (current slot)\n"
        "  F4          Load state (current slot)\n"
        "  F6 / F7     Slot -1 / +1\n"
        "  F8          Screenshot\n"
        "  F9          Reset\n"
        "  F11         Fullscreen toggle\n"
        "  P           Pause toggle\n"
        "  Space       Fast-forward (hold)\n"
        "  M           Mute toggle\n"
        "  Esc         Open menu\n";

    gb.run();
    return 0;
}
