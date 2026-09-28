#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "app.hpp"
#include "headless.hpp"
#include "options.hpp"

namespace {

void print_usage(const char* argv0) {
    std::printf(
        "Usage: %s [options] [ROM]\n"
        "\n"
        "Game Boy (DMG) emulator. Without a ROM argument a file picker opens.\n"
        "\n"
        "General options:\n"
        "  --save-dir DIR        where battery saves and save states go\n"
        "                        (default ~/.local/share/gbemu/saves)\n"
        "  --boot-rom FILE       run a 256-byte DMG boot ROM before the game\n"
        "  --break ADDR          pause when PC reaches ADDR (hex, repeatable)\n"
        "\n"
        "Window options:\n"
        "  --scale N             window scale factor, 1-8 (default 4)\n"
        "  --fullscreen          start in fullscreen\n"
        "  --greyscale           use a grey palette instead of DMG green\n"
        "  --debugger            open the debugger window at startup\n"
        "  --mute                disable audio output\n"
        "\n"
        "Headless / testing options:\n"
        "  --headless            run without a window\n"
        "  --frames N            run exactly N frames, then exit 0\n"
        "  --timeout-sec S       emulated seconds before reporting TIMEOUT (default 120)\n"
        "  --screenshot FILE     write the final frame as PNG\n"
        "  --compare FILE        compare the final frame against a reference PNG\n"
        "  --input SCRIPT        scripted presses: frame:button[:frames],...\n"
        "                        buttons: a b start select up down left right\n"
        "  --verify-savestate    check that a save state replays identically\n"
        "  --mode auto|none      auto-detect Blargg/Mooneye results (default auto)\n"
        "  --quiet               only print the result line\n"
        "\n"
        "Exit codes (headless): 0 pass, 1 fail, 2 timeout, 3 error.\n",
        argv0);
}

bool parse_hex16(const std::string& s, gb::u16& out) {
    std::string t = s;
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
    if (!t.empty() && t[0] == '$') t = t.substr(1);
    char* end = nullptr;
    unsigned long v = std::strtoul(t.c_str(), &end, 16);
    if (t.empty() || *end != '\0' || v > 0xFFFF) return false;
    out = static_cast<gb::u16>(v);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    gbemu::Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: %s needs a value\n", name);
                std::exit(3);
            }
            return argv[++i];
        };
        try {
            if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else if (arg == "--headless") {
                opt.headless = true;
            } else if (arg == "--frames") {
                opt.frames = std::stoull(value("--frames"));
            } else if (arg == "--timeout-sec") {
                opt.timeout_sec = std::stod(value("--timeout-sec"));
            } else if (arg == "--screenshot") {
                opt.screenshot = value("--screenshot");
            } else if (arg == "--compare") {
                opt.compare = value("--compare");
            } else if (arg == "--input") {
                opt.input_script = value("--input");
            } else if (arg == "--verify-savestate") {
                opt.verify_savestate = true;
            } else if (arg == "--mode") {
                std::string m = value("--mode");
                if (m != "auto" && m != "none") throw std::invalid_argument(m);
                opt.detect = m == "auto";
            } else if (arg == "--quiet") {
                opt.quiet = true;
            } else if (arg == "--scale") {
                opt.scale = std::stoi(value("--scale"));
                if (opt.scale < 1 || opt.scale > 8) throw std::out_of_range("scale");
            } else if (arg == "--fullscreen") {
                opt.fullscreen = true;
            } else if (arg == "--greyscale" || arg == "--grayscale") {
                opt.greyscale = true;
            } else if (arg == "--debugger") {
                opt.debugger = true;
            } else if (arg == "--mute") {
                opt.mute = true;
            } else if (arg == "--save-dir") {
                opt.save_dir = value("--save-dir");
            } else if (arg == "--boot-rom") {
                opt.boot_rom = value("--boot-rom");
            } else if (arg == "--break") {
                gb::u16 addr = 0;
                std::string v = value("--break");
                if (!parse_hex16(v, addr)) throw std::invalid_argument(v);
                opt.breakpoints.push_back(addr);
            } else if (!arg.empty() && arg[0] == '-') {
                std::fprintf(stderr, "error: unknown option %s (see --help)\n", arg.c_str());
                return 3;
            } else if (opt.rom_path.empty()) {
                opt.rom_path = arg;
            } else {
                std::fprintf(stderr, "error: more than one ROM given\n");
                return 3;
            }
        } catch (const std::exception&) {
            std::fprintf(stderr, "error: bad value for %s\n", arg.c_str());
            return 3;
        }
    }

    if (opt.headless) {
        if (opt.rom_path.empty()) {
            std::fprintf(stderr, "error: --headless needs a ROM path\n");
            return 3;
        }
        return gbemu::run_headless(opt);
    }
    return gbemu::run_gui(opt);
}
