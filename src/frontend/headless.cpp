#include "headless.hpp"

#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/gameboy.hpp"
#include "image_io.hpp"
#include "storage.hpp"

namespace gbemu {

namespace {

enum class Result { Running, Pass, Fail };

struct InputEvent {
    gb::u64 frame;
    gb::Button button;
    gb::u64 duration;
};

bool parse_button(const std::string& name, gb::Button& out) {
    static const std::map<std::string, gb::Button> kNames = {
        {"right", gb::Button::Right}, {"left", gb::Button::Left},   {"up", gb::Button::Up},
        {"down", gb::Button::Down},   {"a", gb::Button::A},         {"b", gb::Button::B},
        {"select", gb::Button::Select}, {"start", gb::Button::Start},
    };
    auto it = kNames.find(name);
    if (it == kNames.end()) return false;
    out = it->second;
    return true;
}

// "120:start,200:a:10" -> press START on frame 120 for 5 frames, A on frame 200 for 10 frames.
bool parse_input_script(const std::string& script, std::vector<InputEvent>& events, std::string& error) {
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        std::vector<std::string> parts;
        std::stringstream is(item);
        std::string p;
        while (std::getline(is, p, ':')) parts.push_back(p);
        InputEvent ev{};
        try {
            if (parts.size() < 2 || parts.size() > 3) throw std::invalid_argument(item);
            ev.frame = std::stoull(parts[0]);
            ev.duration = parts.size() == 3 ? std::stoull(parts[2]) : 5;
        } catch (const std::exception&) {
            error = "bad --input entry '" + item + "' (expected frame:button[:duration])";
            return false;
        }
        if (!parse_button(parts[1], ev.button)) {
            error = "unknown button '" + parts[1] + "'";
            return false;
        }
        events.push_back(ev);
    }
    return true;
}

gb::u64 hash_frame(const gb::Ppu::Framebuffer& fb) {
    gb::u64 h = 1469598103934665603ull;
    for (gb::u8 v : fb) h = (h ^ v) * 1099511628211ull;
    return h;
}

// Blargg's tests that report through cartridge RAM: A001-A003 = DE B0 61,
// A000 = 0x80 while running, otherwise the result code; text from A004.
Result check_blargg_memory(const gb::Cartridge& cart, std::string& text) {
    const auto& ram = cart.ram();
    if (ram.size() < 0x100 || ram[1] != 0xDE || ram[2] != 0xB0 || ram[3] != 0x61) return Result::Running;
    if (ram[0] == 0x80) return Result::Running;
    text.clear();
    for (size_t i = 4; i < ram.size() && ram[i] != 0; ++i) text.push_back(static_cast<char>(ram[i]));
    return ram[0] == 0 ? Result::Pass : Result::Fail;
}

// Mooneye tests finish with LD B,B: Fibonacci numbers in B-L mean pass, 0x42 everywhere means fail.
// Other programs (e.g. Blargg's 06-ld r,r) execute LD B,B too, so anything else is ignored.
Result check_mooneye(const gb::Cpu& cpu) {
    if (cpu.b == 3 && cpu.c == 5 && cpu.d == 8 && cpu.e == 13 && cpu.h == 21 && cpu.l == 34) return Result::Pass;
    if (cpu.b == 0x42 && cpu.c == 0x42 && cpu.d == 0x42 && cpu.e == 0x42 && cpu.h == 0x42 && cpu.l == 0x42) {
        return Result::Fail;
    }
    return Result::Running;
}

}  // namespace

int run_headless(const Options& opt) {
    std::string error;
    auto cart = gb::Cartridge::from_file(opt.rom_path, error);
    if (!cart) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 3;
    }
    if (!opt.quiet) {
        std::printf("ROM: \"%s\" (%s, %zu KiB ROM, %zu KiB RAM)\n", cart->header().title.c_str(),
                    cart->type_info().name, cart->rom_size() / 1024, cart->ram().size() / 1024);
    }

    std::vector<InputEvent> script;
    if (!parse_input_script(opt.input_script, script, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 3;
    }

    std::vector<gb::u8> reference;
    if (!opt.compare.empty() && !read_png_shades(opt.compare, reference, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 3;
    }

    gb::GameBoy gb;
    if (!opt.boot_rom.empty()) {
        std::vector<gb::u8> boot;
        if (!read_file(opt.boot_rom, boot) || boot.size() < 0x100) {
            std::fprintf(stderr, "error: cannot read boot ROM %s\n", opt.boot_rom.c_str());
            return 3;
        }
        gb.set_boot_rom(std::move(boot));
    }
    gb.insert_cartridge(std::move(cart));
    gb.apu.set_output_enabled(false);
    for (gb::u16 addr : opt.breakpoints) gb.add_breakpoint(addr);

    std::filesystem::path sav;
    if (!opt.save_dir.empty() && gb.cartridge()->has_battery()) {
        sav = battery_path(opt.save_dir, opt.rom_path);
        std::vector<gb::u8> data;
        if (read_file(sav, data) && gb.cartridge()->load_battery_data(data) && !opt.quiet) {
            std::printf("Loaded battery save %s\n", sav.string().c_str());
        }
    }

    const bool fixed_length = opt.frames > 0;
    const gb::u64 max_frames = fixed_length ? opt.frames : static_cast<gb::u64>(opt.timeout_sec * gb::kFrameRate);
    Result result = Result::Running;
    std::string memory_text;
    gb::u64 frame = 0;
    gb::u64 settle_frames = 0;  // frames to run after LD B,B so the final screen is fully drawn

    for (; frame < max_frames; ++frame) {
        for (const auto& ev : script) {
            if (frame == ev.frame) gb.set_button(ev.button, true);
            if (frame == ev.frame + ev.duration) gb.set_button(ev.button, false);
        }
        if (gb.run_frame() == gb::GameBoy::StopReason::Breakpoint) {
            const gb::Cpu& c = gb.cpu;
            std::printf("BREAK at %04X: AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X IME=%d cycles=%llu\n", c.pc, c.af(),
                        c.bc(), c.de(), c.hl(), c.sp, c.ime() ? 1 : 0,
                        static_cast<unsigned long long>(gb.bus.cycles()));
            return 0;
        }

        if (!opt.detect) continue;
        if (settle_frames > 0) {
            if (--settle_frames == 0) break;
            continue;
        }
        const std::string& serial = gb.serial.output();
        if (serial.find("Passed") != std::string::npos) {
            result = Result::Pass;
        } else if (serial.find("Failed") != std::string::npos) {
            result = Result::Fail;
        }
        if (result == Result::Running) result = check_blargg_memory(*gb.cartridge(), memory_text);
        if (result == Result::Running && gb.cpu.ld_b_b_hit) {
            if (!reference.empty()) {
                settle_frames = 2;
                continue;
            }
            result = check_mooneye(gb.cpu);
            gb.cpu.ld_b_b_hit = false;
        }
        if (result != Result::Running) {
            ++frame;
            break;
        }
    }

    if (!reference.empty()) {
        size_t mismatches = 0;
        const auto& fb = gb.framebuffer();
        for (size_t i = 0; i < fb.size(); ++i) mismatches += fb[i] != reference[i];
        if (!opt.quiet) std::printf("Screen compare: %zu / %zu pixels differ\n", mismatches, fb.size());
        result = mismatches == 0 ? Result::Pass : Result::Fail;
    }

    if (!opt.screenshot.empty()) {
        if (write_png(opt.screenshot, gb.framebuffer(), kPaletteGrey)) {
            if (!opt.quiet) std::printf("Screenshot written to %s\n", opt.screenshot.c_str());
        } else {
            std::fprintf(stderr, "error: could not write %s\n", opt.screenshot.c_str());
        }
    }

    if (!sav.empty()) {
        if (write_file_atomic(sav, gb.cartridge()->battery_data()) && !opt.quiet) {
            std::printf("Battery save written to %s\n", sav.string().c_str());
        }
    }

    if (!opt.quiet) {
        const std::string& serial = gb.serial.output();
        if (!serial.empty()) std::printf("--- serial output ---\n%s\n---------------------\n", serial.c_str());
        if (!memory_text.empty()) std::printf("--- $A004 text ---\n%s\n------------------\n", memory_text.c_str());
        const gb::Cpu& c = gb.cpu;
        std::printf("CPU: AF=%04X BC=%04X DE=%04X HL=%04X SP=%04X PC=%04X IME=%d\n", c.af(), c.bc(), c.de(), c.hl(),
                    c.sp, c.pc, c.ime() ? 1 : 0);
    }

    if (opt.verify_savestate) {
        constexpr int kFrames = 180;
        std::vector<gb::u8> state = gb.save_state();
        std::vector<gb::u64> hashes;
        for (int i = 0; i < kFrames; ++i) {
            gb.run_frame();
            hashes.push_back(hash_frame(gb.framebuffer()));
        }
        gb::u16 pc = gb.cpu.pc;
        gb::u64 cycles = gb.bus.cycles();
        if (!gb.load_state(state, error)) {
            std::printf("SAVESTATE FAIL: %s\n", error.c_str());
            return 1;
        }
        for (int i = 0; i < kFrames; ++i) {
            gb.run_frame();
            if (hash_frame(gb.framebuffer()) != hashes[static_cast<size_t>(i)]) {
                std::printf("SAVESTATE FAIL: frame %d differs after reload\n", i);
                return 1;
            }
        }
        if (gb.cpu.pc != pc || gb.bus.cycles() != cycles) {
            std::printf("SAVESTATE FAIL: CPU state differs after reload\n");
            return 1;
        }
        std::printf("SAVESTATE OK: %zu byte state, %d frames replayed identically\n", state.size(), kFrames);
    }

    switch (result) {
        case Result::Pass: std::printf("PASS (%llu frames)\n", static_cast<unsigned long long>(frame)); return 0;
        case Result::Fail: std::printf("FAIL (%llu frames)\n", static_cast<unsigned long long>(frame)); return 1;
        case Result::Running: break;
    }
    if (fixed_length || !opt.detect) {
        if (!opt.quiet) std::printf("DONE (%llu frames)\n", static_cast<unsigned long long>(frame));
        return 0;
    }
    std::printf("TIMEOUT (%llu frames)\n", static_cast<unsigned long long>(frame));
    return 2;
}

}  // namespace gbemu
