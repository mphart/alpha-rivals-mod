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

double ReadPlayerValue(int player, std::initializer_list<uintptr_t> offsets) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, offsets);
    addr += 0x10 * player;
    double val = *(double*)addr;
    //Log("final addr = " + ToHex(addr));
    //Log("final value = " + std::to_string(val));
    return val;
}

double ReadPlayerOn(int player) {
    //Log("ReadPlayerOn(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x0, 0x4, 0x4, 0x310
    });
}

double ReadPlayerPercent(int player) {
    //Log("ReadPlayerPercent(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1510
    });
}

double ReadPlayerStock(int player) {
    //Log("ReadPlayerStock(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1710
    });
}

double ReadPlayerTeam(int player) {
    //Log("ReadPlayerTeam(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1810
    });
}

double ReadPlayerX(int player) {
    //Log("ReadPlayerX(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0xC, 0x110
    });
}

double ReadPlayerY(int player) {
    //Log("ReadPlayerY(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0xC, 0x10
    });
}

double ReadPlayerVelX(int player) { return 0; }

double ReadPlayerVelY(int player) { return 0; }

double ReadPlayerAnim(int player) {
    //Log("ReadPlayerAnim(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x24, 0x4, 0x10
    });
}

double ReadPlayerAnimSprite(int player) {
    //Log("ReadPlayerAnimSprite(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x210, 0x20, 0x48, 0x10, 0x24, 0x14, 0x130, 0x0, 0xC, 0x310
    });
}

double ReadPlayerCharacter(int player) {
    //Log("ReadPlayerCharacter(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x20, 0x28, 0x10, 0x28, 0x20, 0x24, 0x4, 0x10
    });
}

double ReadPlayerUsedAirDodge(int player) {
    //Log("ReadPlayerUsedAirDodge(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x210, 0x20, 0x48, 0x10, 0x24, 0xC, 0xE10
    });
}

double ReadPlayerIsInvulnerable(int player) {
    //Log("ReadPlayerIsInvulnerable(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x4B0, 0x10, 0x28, 0x10, 0x48, 0x20, 0x44, 0xC, 0x510
    });
}

double ReadPlayerDirection(int player) {
    //Log("ReadPlayerDirection(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x4B0, 0x20, 0x48, 0x0, 0x44, 0xC, 0x130, 0x0, 0xC, 0x110
    });
}

double ReadPlayerOnFire(int player) {
    //Log("ReadPlayerOnFire(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x4B0, 0x10, 0x8, 0x10, 0x4, 0x4, 0xE10
    });
}

double ReadGameSpeed() {
    //Log("ReadGameSpeed()");
    return ReadPlayerValue(0, {
        0x05C4A8D8, 0x2C, 0x10, 0x4BC, 0x630
    });
}

double ReadGameStage() {
    //Log("ReadGameStage()");
    return ReadPlayerValue(0, {
        0x05C4A8D8, 0x2C, 0x10, 0x660, 0xF0
    });
}

double ReadGameClock() {
    //Log("ReadGameClock()");
    return ReadPlayerValue(0, {
        0x05C4A8D8, 0x2C, 0x10, 0x288, 0xD50
    });
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
    std::ostringstream json;
    json << "{";

    // --- Players ---
    json << "\"players\":[";
    for (int p = 0; p < 4; ++p) {
        if (p > 0) json << ",";

        double onVal = 0;
        bool onOk = TryReadIntArg(ReadPlayerOn, p, onVal);
        bool isOn = onOk && (onVal != 0.0);

        json << "{\"on\":" << (isOn ? "true" : "false");

        if (isOn) {
            double v;
            if (TryReadIntArg(ReadPlayerPercent, p, v))        json << ",\"percent\":" << v;
            if (TryReadIntArg(ReadPlayerStock, p, v))          json << ",\"stock\":" << v;
            if (TryReadIntArg(ReadPlayerX, p, v))              json << ",\"x\":" << v;
            if (TryReadIntArg(ReadPlayerY, p, v))              json << ",\"y\":" << v;
            if (TryReadIntArg(ReadPlayerAnim, p, v))           json << ",\"anim\":" << v;
            if (TryReadIntArg(ReadPlayerAnimSprite, p, v))     json << ",\"anim_sprite\":" << v;
            if (TryReadIntArg(ReadPlayerCharacter, p, v))      json << ",\"character\":" << v;
            if (TryReadIntArg(ReadPlayerTeam, p, v))           json << ",\"team\":" << v;
            if (TryReadIntArg(ReadPlayerUsedAirDodge, p, v))   json << ",\"used_air_dodge\":" << v;
            if (TryReadIntArg(ReadPlayerDirection, p, v))      json << ",\"dir\":" << v;
            if (TryReadIntArg(ReadPlayerIsInvulnerable, p, v)) json << ",\"invuln\":" << v;

            // zetterburn
            if (TryReadIntArg(ReadPlayerOnFire, p, v))          json << ",\"on_fire\":" << v;
        }
        json << "}";
    }
    json << "],";

    // --- Environment ---
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
    return json.str();
}