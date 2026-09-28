cmake_minimum_required(VERSION 3.20)
project(gbemu VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(SDL3 REQUIRED CONFIG)

# Emulator core: no SDL dependency.
add_library(gbcore STATIC
    src/core/apu.cpp
    src/core/bus.cpp
    src/core/cartridge.cpp
    src/core/cpu.cpp
    src/core/disasm.cpp
    src/core/gameboy.cpp
    src/core/joypad.cpp
    src/core/mbc.cpp
    src/core/ppu.cpp
    src/core/serial.cpp
    src/core/timer.cpp
)
target_include_directories(gbcore PUBLIC src)
target_compile_options(gbcore PRIVATE -Wall -Wextra)

add_executable(gbemu
    src/frontend/app.cpp
    src/frontend/debugger.cpp
    src/frontend/headless.cpp
    src/frontend/image_io.cpp
    src/frontend/main.cpp
    src/frontend/storage.cpp
)
target_include_directories(gbemu PRIVATE src/frontend)
target_include_directories(gbemu SYSTEM PRIVATE third_party/stb)
target_link_libraries(gbemu PRIVATE gbcore SDL3::SDL3)
target_compile_options(gbemu PRIVATE -Wall -Wextra)
# stb is third-party code; don't let its warnings drown out ours.
set_source_files_properties(src/frontend/image_io.cpp PROPERTIES COMPILE_OPTIONS "-w")
