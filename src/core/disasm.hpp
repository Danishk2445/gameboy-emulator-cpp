#pragma once

#include <functional>
#include <string>

#include "common.hpp"

namespace gb {

struct Disassembly {
    std::string text;  // e.g. "LD A,($FF44)"
    u8 length = 1;     // instruction size in bytes
};

// Decodes the instruction at `addr`, reading bytes through `read`.
Disassembly disassemble(u16 addr, const std::function<u8(u16)>& read);

}  // namespace gb
