#pragma once

#include <string>
#include <vector>

#include "core/common.hpp"

namespace gbemu {

struct Options {
    std::string rom_path;
    std::string save_dir;  // empty = platform default (GUI) / no battery files (headless)
    std::string boot_rom;
    std::vector<gb::u16> breakpoints;

    // GUI
    int scale = 4;
    bool fullscreen = false;
    bool debugger = false;
    bool greyscale = false;
    bool mute = false;

    // Headless
    bool headless = false;
    gb::u64 frames = 0;         // run exactly this many frames (0 = until a result or timeout)
    double timeout_sec = 120;   // emulated seconds before giving up
    std::string screenshot;
    std::string compare;
    std::string input_script;   // "frame:button[:duration],..."
    bool verify_savestate = false;
    bool detect = true;         // --mode none disables pass/fail detection
    bool quiet = false;
};

}  // namespace gbemu
