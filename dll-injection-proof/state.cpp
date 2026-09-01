#include "pch.h"
#include "framework.h"
#include "state.h"
#include "util.h"

uintptr_t GetModuleBase(const char* moduleName) {
    return (uintptr_t)GetModuleHandleA(moduleName);
}

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::hex << val;
    return ss.str();
}

uintptr_t FollowOffsetChain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
    uintptr_t addr = base;
    if (offsets.size() == 0) {
        return addr;
    }

    auto it = offsets.begin();
    const auto last = offsets.end() - 1;
    for (; it != last; ++it) {
        addr = *reinterpret_cast<uintptr_t*>(addr + *it);
    }
    addr += *last;
    return addr;
}

double ReadPlayerOn(int player) {
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerOn(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x0, 0x4, 0x4, 0x310
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerPercent(int player) {
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerPercent(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1510
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerStock(int player) {
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerStock(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1710
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerX(int player) { 
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerX(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0xC, 0x110
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerY(int player) { 
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerY(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0xC, 0x10
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerVelX(int player) { return 0; }

double ReadPlayerVelY(int player) { return 0; }

double ReadPlayerAnim(int player) { 
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerAnim(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0x4, 0x10
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerCharacter(int player) { 
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerCharacter(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x28, 0x10, 0x28, 0x20, 0x24, 0x4, 0x10
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerTeam(int player){ 
    if(player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerTeam(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1810
    });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerUsedAirDodge(int player) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    Log("ReadPlayerUsedAirDodge(" + std::to_string(player) + ")");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x1A4, 0xC, 0x40, 0x24, 0xC, 0xE10
     });
    addr += 0x10 * player;
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadGameSpeed() {
    Log("ReadGameSpeed()");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x4BC, 0x630
    });
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadGameStage() {
    Log("ReadGameStage()");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x660, 0xF0
    });
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

double ReadGameClock() {
    Log("ReadGameClock()");
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x288, 0xD50
    });
    double val = *(double*)addr;
    Log("final addr = " + ToHex(addr));
    Log("final value = " + std::to_string(val));
    return val;
}

typedef double(*ReadFuncNoArg)();
typedef double(*ReadFuncIntArg)(int);

bool TryReadNoArg(ReadFuncNoArg f, double& out) {
    __try {
        out = f();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadIntArg(ReadFuncIntArg f, int arg, double& out) {
    __try {
        out = f(arg);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string BuildGameStateJson() {
    Log("Entered BuildGameStateJson");
    std::ostringstream json;
    json << "{";

    // --- Players ---
    json << "\"players\":[";
    for (int p = 0; p < 4; ++p) {
        Log("Processing player " + std::to_string(p));
        if (p > 0) json << ",";

        double onVal = 0;
        bool onOk = TryReadIntArg(ReadPlayerOn, p, onVal);
        bool isOn = onOk && (onVal != 0.0);

        json << "{\"on\":" << (isOn ? "true" : "false");

        if (isOn) {
            double v;
            if (TryReadIntArg(ReadPlayerPercent, p, v))       json << ",\"percent\":" << v;
            if (TryReadIntArg(ReadPlayerStock, p, v))         json << ",\"stock\":" << v;
            if (TryReadIntArg(ReadPlayerX, p, v))             json << ",\"x\":" << v;
            if (TryReadIntArg(ReadPlayerY, p, v))             json << ",\"y\":" << v;
            if (TryReadIntArg(ReadPlayerAnim, p, v))          json << ",\"anim\":" << v;
            if (TryReadIntArg(ReadPlayerCharacter, p, v))     json << ",\"character\":" << v;
            if (TryReadIntArg(ReadPlayerTeam, p, v))          json << ",\"team\":" << v;
            if (TryReadIntArg(ReadPlayerUsedAirDodge, p, v))  json << ",\"used_air_dodge\":" << v;
        }
        json << "}";
    }
    json << "],";

    // --- Environment ---
    Log("Finished players loop, starting environment fields");
    json << "\"game\":{";
    double v;
    bool first = true;
    auto addField = [&](const char* name, ReadFuncNoArg f) {
        double val;
        if (TryReadNoArg(f, val)) {
            if (!first) json << ",";
            json << "\"" << name << "\":" << val;
            first = false;
        }
    };
    addField("speed", ReadGameSpeed);
    addField("stage", ReadGameStage);
    addField("clock", ReadGameClock);
    json << "}";

    json << "}";
    Log("Finished BuildGameStateJson");
    return json.str();
}