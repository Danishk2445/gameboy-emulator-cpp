#include "gameboy.h"
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom.gb>" << std::endl;
        return 1;
    }
    
    GameBoy gb;
    
    if (!gb.init()) {
        std::cerr << "Failed to initialize emulator" << std::endl;
        return 1;
    }
    
    if (!gb.loadROM(argv[1])) {
        std::cerr << "Failed to load ROM: " << argv[1] << std::endl;
        return 1;
    }
    
    std::cout << "Starting emulation..." << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Arrow keys - D-Pad" << std::endl;
    std::cout << "  Z - A button" << std::endl;
    std::cout << "  X - B button" << std::endl;
    std::cout << "  Enter - Start" << std::endl;
    std::cout << "  Backspace - Select" << std::endl;
    std::cout << "  Escape - Quit" << std::endl;
    
    gb.run();
    
    SDL_Quit();
    return 0;
}
