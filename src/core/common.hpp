#pragma once

#include <cstddef>
#include <cstdint>

namespace gb {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

constexpr int kScreenWidth = 160;
constexpr int kScreenHeight = 144;
constexpr u32 kCpuHz = 4194304;         // T-cycles per second
constexpr u32 kCyclesPerFrame = 70224;  // T-cycles per video frame
constexpr double kFrameRate = double(kCpuHz) / kCyclesPerFrame;

// Bits of the IE / IF registers.
enum InterruptBit : u8 {
    kIntVBlank = 0x01,
    kIntStat = 0x02,
    kIntTimer = 0x04,
    kIntSerial = 0x08,
    kIntJoypad = 0x10,
};

struct Interrupts {
    u8 ie = 0x00;
    u8 flags = 0x01;  // IF, low 5 bits only
    // Requests raised in the second half of the current M-cycle (PPU, serial). The CPU's
    // interrupt check during an opcode fetch doesn't see them until the next cycle.
    u8 late = 0x00;

    void request(u8 mask) { flags |= mask & 0x1F; }
    void request_late(u8 mask) {
        late |= mask & static_cast<u8>(~flags) & 0x1F;
        flags |= mask & 0x1F;
    }
    u8 pending() const { return ie & flags & 0x1F; }
    u8 pending_at_fetch() const { return ie & flags & static_cast<u8>(~late) & 0x1F; }
};

}  // namespace gb
