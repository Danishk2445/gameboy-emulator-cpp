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
