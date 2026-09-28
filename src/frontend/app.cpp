#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/gameboy.hpp"
#include "debugger.hpp"
#include "image_io.hpp"
#include "storage.hpp"

namespace gbemu {

namespace fs = std::filesystem;

namespace {

constexpr gb::u64 kNsPerSec = 1'000'000'000ull;
constexpr gb::u64 kFrameNs = static_cast<gb::u64>(kNsPerSec / gb::kFrameRate);
constexpr int kAudioRate = 48000;
constexpr float kAudioTargetFrames = kAudioRate * 0.05f;  // ~50 ms of queued audio
constexpr gb::u64 kBatteryFlushNs = 5 * kNsPerSec;

gb::u8 button_bit(gb::Button b) { return static_cast<gb::u8>(1u << static_cast<int>(b)); }

std::optional<gb::Button> key_to_button(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_RIGHT: return gb::Button::Right;
        case SDL_SCANCODE_LEFT: return gb::Button::Left;
        case SDL_SCANCODE_UP: return gb::Button::Up;
        case SDL_SCANCODE_DOWN: return gb::Button::Down;
        case SDL_SCANCODE_X: return gb::Button::A;
        case SDL_SCANCODE_Z: return gb::Button::B;
        case SDL_SCANCODE_RETURN: return gb::Button::Start;
        case SDL_SCANCODE_BACKSPACE:
        case SDL_SCANCODE_RSHIFT: return gb::Button::Select;
        default: return std::nullopt;
    }
}

std::optional<gb::Button> pad_to_button(Uint8 button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return gb::Button::Right;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return gb::Button::Left;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return gb::Button::Up;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return gb::Button::Down;
        case SDL_GAMEPAD_BUTTON_EAST: return gb::Button::A;   // Nintendo layout: A on the right
        case SDL_GAMEPAD_BUTTON_SOUTH: return gb::Button::B;  // B at the bottom
        case SDL_GAMEPAD_BUTTON_START: return gb::Button::Start;
        case SDL_GAMEPAD_BUTTON_BACK: return gb::Button::Select;
        default: return std::nullopt;
    }
}

const char* const kHelpLines[] = {
    "Arrows  D-pad",
    "X / Z   A / B",
    "Enter   Start",
    "Bksp    Select",
    "",
    "Tab     Fast-forward (hold)",
    "P       Pause",
    "F1-F4   Save state",
    "Sh+F1-4 Load state",
    "F7 / F8 Step instr / frame",
    "F9      Palette",
    "F10     Debugger",
    "F11     Fullscreen",
    "F12     Screenshot",
    "Ctrl+O  Open ROM",
    "Ctrl+R  Reset",
    "Ctrl+1-6 Window scale",
    "H       This help",
    "Esc     Quit",
};

class App {
public:
    explicit App(const Options& opt) : opt_(opt) {}
    ~App();
    int run();

private:
    bool init();
    bool load_rom(const std::string& path);
    void flush_battery(bool force);
    void handle_event(const SDL_Event& e);
    void handle_key_down(const SDL_KeyboardEvent& k);
    void sync_joypad();
    void run_one_frame();
    void push_audio();
    void render();
    void draw_text(float x, float y, float scale, const std::string& text, SDL_Color color);
    void osd(const std::string& message);
    void save_state(int slot);
    void load_state(int slot);
    void take_screenshot();
    void set_scale(int scale);
    void set_paused(bool paused);
    void update_title();
    void open_file_dialog();
    const Palette& palette() const { return green_ ? kPaletteGreen : kPaletteGrey; }
    fs::path last_dir_file() const { return save_dir_.parent_path() / "last_rom_dir"; }

    Options opt_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_AudioStream* audio_ = nullptr;
    std::vector<SDL_Gamepad*> gamepads_;
    Debugger debugger_;

    std::unique_ptr<gb::GameBoy> gb_;
    std::string rom_path_;
    fs::path save_dir_;

    bool running_ = true;
    bool paused_ = false;
    bool ff_key_ = false;
    bool ff_pad_ = false;
    bool green_ = true;
    bool show_help_ = false;
    gb::u8 key_buttons_ = 0;
    gb::u8 pad_buttons_ = 0;
    gb::u8 stick_buttons_ = 0;
    gb::u8 applied_buttons_ = 0;

    std::string osd_text_;
    gb::u64 osd_until_ns_ = 0;
    gb::u64 next_frame_ns_ = 0;
    gb::u64 last_battery_flush_ns_ = 0;
    gb::u64 fps_window_start_ns_ = 0;
    int fps_frames_ = 0;
    double fps_ = 0;

    std::mutex dialog_mutex_;
    std::optional<std::string> dialog_result_;
    bool dialog_open_ = false;
};

App::~App() {
    flush_battery(true);
    for (SDL_Gamepad* pad : gamepads_) SDL_CloseGamepad(pad);
    debugger_.close();
    if (audio_) SDL_DestroyAudioStream(audio_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool App::init() {
    SDL_SetAppMetadata("gbemu", "1.0", "io.github.gbemu");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        // Audio or gamepad support missing shouldn't stop the emulator from running.
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
    }

    if (!SDL_CreateWindowAndRenderer("gbemu", gb::kScreenWidth * opt_.scale, gb::kScreenHeight * opt_.scale,
                                     SDL_WINDOW_RESIZABLE, &window_, &renderer_)) {
        std::fprintf(stderr, "error: cannot create window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, gb::kScreenWidth, gb::kScreenHeight);
    SDL_SetRenderVSync(renderer_, 0);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, gb::kScreenWidth,
                                 gb::kScreenHeight);
    if (!texture_) {
        std::fprintf(stderr, "error: cannot create texture: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);
    if (opt_.fullscreen) SDL_SetWindowFullscreen(window_, true);

    if (!opt_.mute && SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kAudioRate};
        audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (audio_) {
            SDL_ResumeAudioStreamDevice(audio_);
        } else {
            std::fprintf(stderr, "warning: no audio output: %s\n", SDL_GetError());
        }
    }

    save_dir_ = opt_.save_dir.empty() ? default_save_dir() : fs::path(opt_.save_dir);
    std::error_code ec;
    fs::create_directories(save_dir_, ec);
    green_ = !opt_.greyscale;
    if (opt_.debugger) debugger_.open();
    return true;
}

bool App::load_rom(const std::string& path) {
    std::string error;
    auto cart = gb::Cartridge::from_file(path, error);
    if (!cart) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "gbemu - cannot load ROM", error.c_str(), window_);
        return false;
    }

    flush_battery(true);  // the previous game, if any

    auto gb = std::make_unique<gb::GameBoy>();
    if (!opt_.boot_rom.empty()) {
        std::vector<gb::u8> boot;
        if (read_file(opt_.boot_rom, boot) && boot.size() >= 0x100) {
            gb->set_boot_rom(std::move(boot));
        } else {
            std::fprintf(stderr, "warning: cannot read boot ROM %s, skipping it\n", opt_.boot_rom.c_str());
        }
    }
    gb->insert_cartridge(std::move(cart));
    gb->apu.set_sample_rate(kAudioRate);
    for (gb::u16 addr : opt_.breakpoints) gb->add_breakpoint(addr);

    std::string message = gb->cartridge()->header().title;
    if (gb->cartridge()->has_battery()) {
        std::vector<gb::u8> data;
        fs::path sav = battery_path(save_dir_, path);
        if (read_file(sav, data)) {
            if (gb->cartridge()->load_battery_data(data)) {
                message += " - save loaded";
            } else {
                message += " - save file is too small, ignored";
            }
        }
    }

    gb_ = std::move(gb);
    rom_path_ = path;
    applied_buttons_ = 0;
    paused_ = false;
    next_frame_ns_ = SDL_GetTicksNS();
    last_battery_flush_ns_ = next_frame_ns_;
    if (audio_) {
        SDL_ClearAudioStream(audio_);
        std::vector<float> silence(static_cast<size_t>(kAudioTargetFrames) * 2, 0.0f);
        SDL_PutAudioStreamData(audio_, silence.data(), static_cast<int>(silence.size() * sizeof(float)));
    }
    write_file_atomic(last_dir_file(), [&] {
        std::string dir = fs::absolute(fs::path(path)).parent_path().string();
        return std::vector<gb::u8>(dir.begin(), dir.end());
    }());

    std::printf("Loaded \"%s\" (%s)\n", gb_->cartridge()->header().title.c_str(), gb_->cartridge()->type_info().name);
    osd(message);
    update_title();
    return true;
}

void App::flush_battery(bool force) {
    if (!gb_ || !gb_->cartridge()->has_battery()) return;
    gb::Cartridge& cart = *gb_->cartridge();
    // RTC carts are always written on exit so the clock timestamp stays current.
    if (!cart.battery_dirty() && !(force && cart.type_info().rtc)) return;
    fs::path sav = battery_path(save_dir_, rom_path_);
    if (write_file_atomic(sav, cart.battery_data())) {
        cart.clear_battery_dirty();
    } else {
        std::fprintf(stderr, "warning: could not write %s\n", sav.string().c_str());
    }
}

void App::osd(const std::string& message) {
    osd_text_ = message;
    osd_until_ns_ = SDL_GetTicksNS() + 2 * kNsPerSec;
}

void App::update_title() {
    std::string title = "gbemu";
    if (gb_) {
        title += " - " + gb_->cartridge()->header().title;
        if (paused_) {
            title += " [paused]";
        } else if (fps_ > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), " - %.0f fps", fps_);
            title += buf;
        }
    }
    SDL_SetWindowTitle(window_, title.c_str());
}

void App::set_paused(bool paused) {
    if (!gb_) return;
    paused_ = paused;
    next_frame_ns_ = SDL_GetTicksNS();
    if (audio_ && !paused_) SDL_ClearAudioStream(audio_);
    update_title();
}

void App::set_scale(int scale) {
    SDL_SetWindowFullscreen(window_, false);
    SDL_SetWindowSize(window_, gb::kScreenWidth * scale, gb::kScreenHeight * scale);
    osd("Scale " + std::to_string(scale) + "x");
}

void App::save_state(int slot) {
    if (!gb_) return;
    fs::path path = state_path(save_dir_, rom_path_, slot);
    if (write_file_atomic(path, gb_->save_state())) {
        osd("Saved state " + std::to_string(slot));
    } else {
        osd("Could not save state " + std::to_string(slot));
    }
}

void App::load_state(int slot) {
    if (!gb_) return;
    fs::path path = state_path(save_dir_, rom_path_, slot);
    std::vector<gb::u8> data;
    if (!read_file(path, data)) {
        osd("Slot " + std::to_string(slot) + " is empty");
        return;
    }
    std::string error;
    if (gb_->load_state(data, error)) {
        osd("Loaded state " + std::to_string(slot));
        applied_buttons_ = gb_->joypad.pressed_mask();
        if (audio_) SDL_ClearAudioStream(audio_);
    } else {
        osd(error);
    }
}

void App::take_screenshot() {
    if (!gb_) return;
    std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
    fs::path dir = save_dir_.parent_path() / "screenshots";
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path file = dir / (fs::path(rom_path_).stem().string() + "-" + stamp + ".png");
    if (write_png(file.string(), gb_->framebuffer(), palette())) {
        std::printf("Screenshot saved to %s\n", file.string().c_str());
        osd("Screenshot saved");
    } else {
        osd("Screenshot failed");
    }
}

void App::open_file_dialog() {
    if (dialog_open_) return;
    dialog_open_ = true;
    static const SDL_DialogFileFilter kFilters[] = {{"Game Boy ROMs", "gb;gbc;sgb"}, {"All files", "*"}};
    std::string start_dir;
    std::vector<gb::u8> saved;
    if (read_file(last_dir_file(), saved)) start_dir.assign(saved.begin(), saved.end());
    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* files, int) {
            auto* app = static_cast<App*>(userdata);
            std::lock_guard<std::mutex> lock(app->dialog_mutex_);
            app->dialog_result_ = (files && files[0]) ? std::string(files[0]) : std::string();
        },
        this, window_, kFilters, 2, start_dir.empty() ? nullptr : start_dir.c_str(), false);
}

void App::handle_key_down(const SDL_KeyboardEvent& k) {
    bool ctrl = k.mod & SDL_KMOD_CTRL;
    bool shift = k.mod & SDL_KMOD_SHIFT;

    if (ctrl) {
        if (k.key >= SDLK_1 && k.key <= SDLK_6) {
            set_scale(static_cast<int>(k.key - SDLK_0));
        } else if (k.key == SDLK_O) {
            open_file_dialog();
        } else if (k.key == SDLK_R && gb_) {
            flush_battery(false);
            gb_->reset();
            applied_buttons_ = 0;
            osd("Reset");
        }
        return;
    }

    switch (k.key) {
        case SDLK_ESCAPE: running_ = false; break;
        case SDLK_TAB: ff_key_ = true; break;
        case SDLK_P:
        case SDLK_PAUSE:
            set_paused(!paused_);
            osd(paused_ ? "Paused" : "Resumed");
            break;
        case SDLK_H: show_help_ = !show_help_; break;
        case SDLK_M: debugger_.toggle_map(); break;
        case SDLK_F1:
        case SDLK_F2:
        case SDLK_F3:
        case SDLK_F4: {
            int slot = static_cast<int>(k.key - SDLK_F1) + 1;
            if (shift) {
                load_state(slot);
            } else {
                save_state(slot);
            }
            break;
        }
        case SDLK_F7:
            if (!gb_) break;
            if (!paused_) {
                set_paused(true);
            } else {
                gb_->step_instruction();
            }
            break;
        case SDLK_F8:
            if (!gb_) break;
            if (!paused_) set_paused(true);
            gb_->run_frame();
            gb_->apu.samples().clear();
            break;
        case SDLK_F9:
            green_ = !green_;
            osd(green_ ? "Palette: DMG green" : "Palette: grey");
            break;
        case SDLK_F10:
            if (debugger_.is_open()) {
                debugger_.close();
            } else {
                debugger_.open();
            }
            break;
        case SDLK_F11: {
            bool fullscreen = SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN;
            SDL_SetWindowFullscreen(window_, !fullscreen);
            break;
        }
        case SDLK_F12: take_screenshot(); break;
        default: break;
    }
}

void App::handle_event(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT: running_ = false; break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (debugger_.is_open() && e.window.windowID == debugger_.window_id()) {
                debugger_.close();
            } else {
                running_ = false;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (auto b = key_to_button(e.key.scancode)) {
                key_buttons_ |= button_bit(*b);
            } else if (!e.key.repeat) {
                handle_key_down(e.key);
            }
            break;
        case SDL_EVENT_KEY_UP:
            if (auto b = key_to_button(e.key.scancode)) key_buttons_ &= static_cast<gb::u8>(~button_bit(*b));
            if (e.key.key == SDLK_TAB) ff_key_ = false;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            // Don't leave keys stuck down when focus moves elsewhere.
            if (e.window.windowID == SDL_GetWindowID(window_)) {
                key_buttons_ = 0;
                ff_key_ = false;
            }
            break;
        case SDL_EVENT_DROP_FILE:
            if (e.drop.data) load_rom(e.drop.data);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) {
                gamepads_.push_back(pad);
                const char* name = SDL_GetGamepadName(pad);
                osd(std::string("Gamepad connected: ") + (name ? name : "unknown"));
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (auto it = gamepads_.begin(); it != gamepads_.end(); ++it) {
                if (SDL_GetGamepadID(*it) == e.gdevice.which) {
                    SDL_CloseGamepad(*it);
                    gamepads_.erase(it);
                    break;
                }
            }
            pad_buttons_ = stick_buttons_ = 0;
            ff_pad_ = false;
            osd("Gamepad disconnected");
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            if (auto b = pad_to_button(e.gbutton.button)) {
                if (down) {
                    pad_buttons_ |= button_bit(*b);
                } else {
                    pad_buttons_ &= static_cast<gb::u8>(~button_bit(*b));
                }
            } else if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                ff_pad_ = down;
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            constexpr int kDeadzone = 12000;
            auto set = [&](gb::Button b, bool on) {
                if (on) {
                    stick_buttons_ |= button_bit(b);
                } else {
                    stick_buttons_ &= static_cast<gb::u8>(~button_bit(b));
                }
            };
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                set(gb::Button::Left, e.gaxis.value < -kDeadzone);
                set(gb::Button::Right, e.gaxis.value > kDeadzone);
            } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                set(gb::Button::Up, e.gaxis.value < -kDeadzone);
                set(gb::Button::Down, e.gaxis.value > kDeadzone);
            }
            break;
        }
        default: break;
    }
}

void App::sync_joypad() {
    if (!gb_) return;
    gb::u8 want = key_buttons_ | pad_buttons_ | stick_buttons_;
    // Opposite directions at once confuse some games; treat them as neither.
    constexpr gb::u8 kLR = (1u << static_cast<int>(gb::Button::Left)) | (1u << static_cast<int>(gb::Button::Right));
    constexpr gb::u8 kUD = (1u << static_cast<int>(gb::Button::Up)) | (1u << static_cast<int>(gb::Button::Down));
    if ((want & kLR) == kLR) want &= static_cast<gb::u8>(~kLR);
    if ((want & kUD) == kUD) want &= static_cast<gb::u8>(~kUD);
    gb::u8 changed = want ^ applied_buttons_;
    for (int i = 0; i < 8; ++i) {
        if (changed & (1u << i)) gb_->set_button(static_cast<gb::Button>(i), want & (1u << i));
    }
    applied_buttons_ = want;
}

void App::run_one_frame() {
    if (gb_->run_frame() == gb::GameBoy::StopReason::Breakpoint) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Breakpoint at $%04X", gb_->cpu.pc);
        osd(buf);
        paused_ = true;
        update_title();
        if (!debugger_.is_open()) debugger_.open();
    }
    ++fps_frames_;
}

void App::push_audio() {
    auto& samples = gb_->apu.samples();
    if (audio_ && !samples.empty()) {
        float queued = static_cast<float>(SDL_GetAudioStreamQueued(audio_)) / (2 * sizeof(float));
        if (queued > kAudioTargetFrames * 4) {
            // Way behind (e.g. after a stall): drop the backlog instead of lagging.
            SDL_ClearAudioStream(audio_);
            queued = 0;
        }
        // Nudge playback speed by up to +-0.5% to keep the queue near the target.
        float error = std::clamp((queued - kAudioTargetFrames) / kAudioTargetFrames, -1.0f, 1.0f);
        SDL_SetAudioStreamFrequencyRatio(audio_, 1.0f + 0.005f * error);
        SDL_PutAudioStreamData(audio_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
    }
    samples.clear();
}

void App::draw_text(float x, float y, float scale, const std::string& text, SDL_Color color) {
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x / scale, y / scale, text.c_str());
    SDL_SetRenderScale(renderer_, 1, 1);
}

void App::render() {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    const Palette& pal = palette();

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    // Integer-scaled, centred game image (fractional only if the window is tiny).
    float scale = std::min(static_cast<float>(w) / gb::kScreenWidth, static_cast<float>(h) / gb::kScreenHeight);
    if (scale >= 1) scale = std::floor(scale);
    SDL_FRect dst{(w - gb::kScreenWidth * scale) / 2, (h - gb::kScreenHeight * scale) / 2, gb::kScreenWidth * scale,
                  gb::kScreenHeight * scale};

    std::array<gb::u32, gb::kScreenWidth * gb::kScreenHeight> pixels;
    if (gb_) {
        const auto& fb = gb_->framebuffer();
        for (size_t i = 0; i < fb.size(); ++i) pixels[i] = pal[fb[i] & 3];
    } else {
        pixels.fill(pal[0]);
    }
    SDL_UpdateTexture(texture_, nullptr, pixels.data(), gb::kScreenWidth * 4);
    SDL_RenderTexture(renderer_, texture_, nullptr, &dst);

    float ts = std::max(1.0f, std::floor(scale / 2));  // text scale
    float line = 10 * ts;
    SDL_Color ink{0x08, 0x18, 0x20, 255};
    SDL_Color light{0xFF, 0xFF, 0xFF, 255};

    if (!gb_) {
        const char* lines[] = {"Drop a Game Boy ROM", "on this window", "", "or press Ctrl+O"};
        for (int i = 0; i < 4; ++i) {
            std::string s = lines[i];
            float tw = static_cast<float>(s.size()) * 8 * ts;
            draw_text(dst.x + (dst.w - tw) / 2, dst.y + dst.h / 2 - 2 * line + i * line, ts, s, ink);
        }
    }

    auto boxed_text = [&](float x, float y, const std::string& s) {
        SDL_FRect box{x - 2 * ts, y - 2 * ts, static_cast<float>(s.size()) * 8 * ts + 4 * ts, 12 * ts};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer_, &box);
        draw_text(x, y, ts, s, light);
    };

    gb::u64 now = SDL_GetTicksNS();
    if (!osd_text_.empty() && now < osd_until_ns_) boxed_text(dst.x + 4 * ts, dst.y + 4 * ts, osd_text_);
    if (gb_ && paused_) boxed_text(dst.x + dst.w - 7 * 8 * ts - 4 * ts, dst.y + dst.h - 14 * ts, "PAUSED");
    if (gb_ && !paused_ && (ff_key_ || ff_pad_)) boxed_text(dst.x + dst.w - 3 * 8 * ts - 4 * ts, dst.y + 4 * ts, ">>");

    if (show_help_) {
        SDL_FRect box{dst.x, dst.y, dst.w, dst.h};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 210);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer_, &box);
        float hs = std::max(1.0f, std::floor(scale / 3));
        float y = dst.y + 6 * hs;
        for (const char* l : kHelpLines) {
            draw_text(dst.x + 6 * hs, y, hs, l, light);
            y += 10 * hs;
        }
    }

    SDL_RenderPresent(renderer_);

    if (debugger_.is_open() && gb_) debugger_.render(*gb_, paused_, pal);
}

int App::run() {
    if (!init()) return 3;

    if (!opt_.rom_path.empty()) {
        load_rom(opt_.rom_path);
    } else {
        open_file_dialog();
    }

    next_frame_ns_ = SDL_GetTicksNS();
    fps_window_start_ns_ = next_frame_ns_;
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(e);

        {
            std::lock_guard<std::mutex> lock(dialog_mutex_);
            if (dialog_result_) {
                dialog_open_ = false;
                if (!dialog_result_->empty()) {
                    std::string path = *dialog_result_;
                    dialog_result_.reset();
                    load_rom(path);
                } else {
                    dialog_result_.reset();
                }
            }
        }

        sync_joypad();
        bool fast = ff_key_ || ff_pad_;
        if (gb_ && !paused_) {
            if (fast) {
                // Run as many frames as fit in ~14 ms, then show the latest one.
                gb::u64 budget_end = SDL_GetTicksNS() + 14'000'000;
                do {
                    run_one_frame();
                } while (!paused_ && SDL_GetTicksNS() < budget_end);
                gb_->apu.samples().clear();
            } else {
                run_one_frame();
                push_audio();
            }
        }

        render();

        gb::u64 now = SDL_GetTicksNS();
        if (now - last_battery_flush_ns_ >= kBatteryFlushNs) {
            flush_battery(false);
            last_battery_flush_ns_ = now;
        }
        if (now - fps_window_start_ns_ >= kNsPerSec) {
            fps_ = fps_frames_ * double(kNsPerSec) / double(now - fps_window_start_ns_);
            fps_frames_ = 0;
            fps_window_start_ns_ = now;
            update_title();
        }

        // Pace to the Game Boy's 59.73 Hz; fast-forward runs uncapped.
        if (fast || paused_ || !gb_) {
            next_frame_ns_ = now;
            if (!fast) SDL_DelayPrecise(kFrameNs);  // keep the UI responsive without spinning
        } else {
            next_frame_ns_ += kFrameNs;
            if (next_frame_ns_ > now) {
                SDL_DelayPrecise(next_frame_ns_ - now);
            } else if (now - next_frame_ns_ > 100'000'000) {
                next_frame_ns_ = now;  // fell far behind (window drag, debugger...): resync
            }
        }
    }
    return 0;
}

}  // namespace

int run_gui(const Options& options) {
    App app(options);
    return app.run();
}

}  // namespace gbemu
