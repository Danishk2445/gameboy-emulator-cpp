

<img width="641" height="601" alt="Screenshot_20260519_105801" src="https://github.com/user-attachments/assets/4a1201bf-0a4e-4d33-8b3a-3977e8cc9778" />
<img width="636" height="600" alt="Screenshot_20260519_105719" src="https://github.com/user-attachments/assets/b01ca965-0f6b-43fb-ad69-bc487db7c3dc" />
<img width="642" height="605" alt="Screenshot_20260519_105658" src="https://github.com/user-attachments/assets/402f24b1-4c24-4a9f-bef1-e5b713aae93e" />

# Game Boy Emulator

A Game Boy (DMG) emulator written in C++ using SDL2 for video, input, and audio.

## Requirements

- A C++17 compiler (e.g. `g++` or `clang++`)
- `make`
- SDL2 development headers (`sdl2-config` must be on `PATH`)

### Installing SDL2

- **Arch Linux:** `sudo pacman -S sdl2`
- **Debian / Ubuntu:** `sudo apt install libsdl2-dev`
- **Fedora:** `sudo dnf install SDL2-devel`
- **macOS (Homebrew):** `brew install sdl2`

## Building

From the project root:

```sh
make
```

This produces an executable named `gameboy` in the current directory.

To clean build artifacts:

```sh
make clean
```

## Running

Launch with a ROM file:

```sh
./gameboy path/to/rom.gb
```

Or launch without arguments to start with the menu open:

```sh
./gameboy
```

## Controls

| Key         | Action     |
| ----------- | ---------- |
| Arrow keys  | D-Pad      |
| Z           | A          |
| X           | B          |
| Enter       | Start      |
| Backspace   | Select     |

### Hotkeys

| Key       | Action                          |
| --------- | ------------------------------- |
| F1 / Esc  | Open menu / pause overlay       |
| F2        | Save state (current slot)       |
| F4        | Load state (current slot)       |
| F6 / F7   | Save-state slot -1 / +1         |
| F8        | Screenshot                      |
| F9        | Reset                           |
| F11       | Toggle fullscreen               |
| P         | Toggle pause                    |
| Space     | Fast-forward (hold)             |
| M         | Toggle mute                     |
