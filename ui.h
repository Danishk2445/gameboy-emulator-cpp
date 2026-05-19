#pragma once

#include <SDL.h>
#include <string>
#include <vector>
#include <cstdint>

class UI {
public:
    enum class Screen {
        None,        // game playing, no menu
        TitleMenu,   // shown when no ROM loaded
        RomBrowser,  // file picker
        InGameMenu,  // pause overlay
        Settings,    // settings page
    };

    struct Action {
        bool quit                  = false;
        bool resetRequested        = false;
        bool quitToMenuRequested   = false;
        std::string loadRomPath;
        bool saveStateRequested    = false;
        bool loadStateRequested    = false;
        int  changeSlot            = 0;   // -1, 0, +1
        bool toggleMute            = false;
        bool toggleFullscreen      = false;
        bool screenshotRequested   = false;
        int  changePalette         = 0;   // -1, 0, +1
    };

    explicit UI(SDL_Renderer* renderer);
    ~UI();

    Screen screen() const { return current; }
    bool   isMenuOpen() const { return current != Screen::None; }

    void openTitleMenu();
    void openInGameMenu();
    void closeMenu();

    bool handleKey(SDL_Keycode key);

    void render(SDL_Texture* gameTexture, int winW, int winH);

    void toast(const std::string& msg, double durationSec = 2.0);

    Action pollAction();

    void setSlot(int s)               { if (s < 0) s = 0; if (s > 9) s = 9; slot = s; }
    int  slotValue() const            { return slot; }
    void setPaused(bool p)            { paused = p; }
    void setMutedView(bool m)         { mutedView = m; }
    void setFullscreenView(bool f)    { fullscreenView = f; }
    void setRomLoaded(bool b)         { romLoaded = b; }
    void setPaletteName(const std::string& s) { paletteName = s; }
    void setFastForwardView(bool f)   { fastForwardView = f; }

private:
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  fontTex  = nullptr;

    Screen current = Screen::None;
    Action pending{};

    int slot = 0;
    bool paused = false;
    bool mutedView = false;
    bool fullscreenView = false;
    bool romLoaded = false;
    bool fastForwardView = false;
    std::string paletteName = "GREEN";

    int titleSel = 0;
    int browserSel = 0;
    int browserScroll = 0;
    int ingameSel = 0;
    int settingsSel = 0;

    std::vector<std::string> romList;

    std::string toastMsg;
    Uint32 toastEnd = 0;

    void rescanRoms();

    void buildFontTexture();
    void drawText(int x, int y, const std::string& s, uint32_t rgba, int scale = 2);
    int  textWidth(const std::string& s, int scale = 2) const;
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void drawBorder(int x, int y, int w, int h, uint32_t rgba, int thickness = 2);
    void drawPanel(int x, int y, int w, int h);

    void handleKeyTitle(SDL_Keycode key);
    void handleKeyBrowser(SDL_Keycode key);
    void handleKeyInGame(SDL_Keycode key);
    void handleKeySettings(SDL_Keycode key);

    void renderTitle(int w, int h);
    void renderBrowser(int w, int h);
    void renderInGame(int w, int h);
    void renderSettings(int w, int h);
    void renderToast(int w, int h);

    static std::string basename(const std::string& path);
};
