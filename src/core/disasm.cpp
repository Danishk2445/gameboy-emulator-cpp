#include "disasm.hpp"

#include <cstdio>

namespace gb {

namespace {

// Operand tokens: d8 (imm byte), d16/a16 (imm word), a8 (high page), r8 (jump target), s8 (signed offset).
constexpr const char* kOps[256] = {
    "NOP", "LD BC,d16", "LD (BC),A", "INC BC", "INC B", "DEC B", "LD B,d8", "RLCA",
    "LD (a16),SP", "ADD HL,BC", "LD A,(BC)", "DEC BC", "INC C", "DEC C", "LD C,d8", "RRCA",
    "STOP", "LD DE,d16", "LD (DE),A", "INC DE", "INC D", "DEC D", "LD D,d8", "RLA",
    "JR r8", "ADD HL,DE", "LD A,(DE)", "DEC DE", "INC E", "DEC E", "LD E,d8", "RRA",
    "JR NZ,r8", "LD HL,d16", "LD (HL+),A", "INC HL", "INC H", "DEC H", "LD H,d8", "DAA",
    "JR Z,r8", "ADD HL,HL", "LD A,(HL+)", "DEC HL", "INC L", "DEC L", "LD L,d8", "CPL",
    "JR NC,r8", "LD SP,d16", "LD (HL-),A", "INC SP", "INC (HL)", "DEC (HL)", "LD (HL),d8", "SCF",
    "JR C,r8", "ADD HL,SP", "LD A,(HL-)", "DEC SP", "INC A", "DEC A", "LD A,d8", "CCF",
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,  // 0x40-0xBF generated
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "RET NZ", "POP BC", "JP NZ,a16", "JP a16", "CALL NZ,a16", "PUSH BC", "ADD A,d8", "RST $00",
    "RET Z", "RET", "JP Z,a16", "PREFIX CB", "CALL Z,a16", "CALL a16", "ADC A,d8", "RST $08",
    "RET NC", "POP DE", "JP NC,a16", "???", "CALL NC,a16", "PUSH DE", "SUB d8", "RST $10",
    "RET C", "RETI", "JP C,a16", "???", "CALL C,a16", "???", "SBC A,d8", "RST $18",
    "LDH (a8),A", "POP HL", "LD ($FF00+C),A", "???", "???", "PUSH HL", "AND d8", "RST $20",
    "ADD SP,s8", "JP HL", "LD (a16),A", "???", "???", "???", "XOR d8", "RST $28",
    "LDH A,(a8)", "POP AF", "LD A,($FF00+C)", "DI", "???", "PUSH AF", "OR d8", "RST $30",
    "LD HL,SP+s8", "LD SP,HL", "LD A,(a16)", "EI", "???", "???", "CP d8", "RST $38",
};

constexpr const char* kR8[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
constexpr const char* kAlu[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
constexpr const char* kShift[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};

}  // namespace

Disassembly disassemble(u16 addr, const std::function<u8(u16)>& read) {
    u8 op = read(addr);
    Disassembly out;
    char buf[32];

    if (op >= 0x40 && op < 0x80) {
        out.text = op == 0x76 ? "HALT" : std::string("LD ") + kR8[(op >> 3) & 7] + "," + kR8[op & 7];
        return out;
    }
    if (op >= 0x80 && op < 0xC0) {
        out.text = std::string(kAlu[(op >> 3) & 7]) + kR8[op & 7];
        return out;
    }
    if (op == 0xCB) {
        u8 cb = read(static_cast<u16>(addr + 1));
        out.length = 2;
        int reg = cb & 7, bit = (cb >> 3) & 7;
        switch (cb >> 6) {
            case 0: out.text = std::string(kShift[bit]) + " " + kR8[reg]; break;
            case 1: out.text = "BIT " + std::to_string(bit) + "," + kR8[reg]; break;
            case 2: out.text = "RES " + std::to_string(bit) + "," + kR8[reg]; break;
            default: out.text = "SET " + std::to_string(bit) + "," + kR8[reg]; break;
        }
        return out;
    }
    if (op == 0x10) {
        out.text = "STOP";
        out.length = 2;
        return out;
    }

    std::string text = kOps[op];
    auto replace = [&](const char* token, const std::string& with, u8 len) {
        size_t pos = text.find(token);
        if (pos == std::string::npos) return false;
        text.replace(pos, std::char_traits<char>::length(token), with);
        out.length = len;
        return true;
    };
    u8 b1 = read(static_cast<u16>(addr + 1));
    u8 b2 = read(static_cast<u16>(addr + 2));
    std::snprintf(buf, sizeof(buf), "$%04X", (b2 << 8) | b1);
    if (!replace("d16", buf, 3) && !replace("a16", buf, 3)) {
        std::snprintf(buf, sizeof(buf), "$%02X", b1);
        if (!replace("d8", buf, 2)) {
            std::snprintf(buf, sizeof(buf), "$FF%02X", b1);
            if (!replace("a8", buf, 2)) {
                std::snprintf(buf, sizeof(buf), "$%04X", static_cast<u16>(addr + 2 + static_cast<i8>(b1)));
                if (!replace("r8", buf, 2)) {
                    int off = static_cast<i8>(b1);
                    std::snprintf(buf, sizeof(buf), "%c$%02X", off < 0 ? '-' : '+', off < 0 ? -off : off);
                    replace("s8", buf, 2);
                }
            }
        }
    }
    out.text = text;
    return out;
}

}  // namespace gb
