#include "pch.h"
#include "framework.h"
#include "state.h"
#include "util.h"
#include <cstring>
#include <cmath>

uintptr_t GetModuleBase(const char* moduleName) {
    return (uintptr_t)GetModuleHandleA(moduleName);
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
    return val;
}
double WritePlayerValue(int player, double val, std::initializer_list<uintptr_t> offsets) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, offsets);
    addr += 0x10 * player;
    *(double*)addr = val;
    return val;
}

double ReadPlayerOn(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x0, 0x4, 0x4, 0x310
    });
}

double ReadPlayerPercent(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1510
    });
}

double ReadPlayerStock(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1710
    });
}

double ReadPlayerTeam(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1810
    });
}

double ReadPlayerCursorY(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1C10
    });
}

// CSS character ids live in a parallel double array next to cursor_y in the
// same CE player struct. On this build the character slot is +0x600 from the
// cursor_y slot for the same player (stride 0x10). old_char on cs_playerbg_obj
// is a one-frame cache that the game overwrites from this array.
static const uintptr_t kCssCursorYFinal = 0x1C10;
static const uintptr_t kCssChoiceFromCursorY = 0x600;

static uintptr_t PlayerCursorYAddr(int player) {
    if (player < 0 || player > 3) return 0;
    uintptr_t base = GetModuleBase("RivalsofAether.exe");
    uintptr_t addr = FollowOffsetChain(base, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, kCssCursorYFinal
    });
    return addr + 0x10 * static_cast<uintptr_t>(player);
}

static uintptr_t PlayerCssChoiceAddr(int player) {
    uintptr_t cy = PlayerCursorYAddr(player);
    return cy ? cy + kCssChoiceFromCursorY : 0;
}

static bool LooksLikeCharId(double v) {
    if (!(v == v) || v < 0.0 || v > 40.0) return false; // NaN / out of range
    double iv = 0;
    if (modf(v, &iv) != 0.0) return false;
    return true;
}

static bool TryReadStatsOff(int player, uintptr_t off, double* out) {
    __try {
        *out = ReadPlayerValue(player, {
            0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, off
        });
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadOnPathOff(int player, uintptr_t off, double* out) {
    __try {
        *out = ReadPlayerValue(player, {
            0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x0, 0x4, 0x4, off
        });
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Scan the known CE player struct for integer-ish values in the character-id range.
// Used to locate the live CSS character pick (old_char is overwritten by the game).
std::string ScanPlayerGlobalCandidates(int player) {
    if (player < 0 || player > 3) return "error: player must be 0-3";
    std::ostringstream ss;
    ss << "{\"player\":" << player << ",\"stats\":[";
    bool first = true;
    for (uintptr_t off = 0x1400; off <= 0x1E00; off += 0x10) {
        double v = 0;
        if (!TryReadStatsOff(player, off, &v)) continue;
        if (!LooksLikeCharId(v)) continue;
        if (!first) ss << ",";
        first = false;
        ss << "{\"off\":\"0x" << std::hex << off << std::dec
           << "\",\"v\":" << v << "}";
    }
    ss << "],\"onpath\":[";
    first = true;
    for (uintptr_t off = 0x200; off <= 0x500; off += 0x10) {
        double v = 0;
        if (!TryReadOnPathOff(player, off, &v)) continue;
        if (!LooksLikeCharId(v)) continue;
        if (!first) ss << ",";
        first = false;
        ss << "{\"off\":\"0x" << std::hex << off << std::dec
           << "\",\"v\":" << v << "}";
    }
    ss << "]}";
    return ss.str();
}

static int FindCustomVarIndex(const char* name);
static bool ReadRValueNumber(uintptr_t rv, double* out);
static uintptr_t FindInstanceVarRValue(uintptr_t instance, int index);

static uintptr_t GlobalInstance() {
    uintptr_t global = 0;
    __try {
        uintptr_t moduleBase = GetModuleBase("RivalsofAether.exe");
        global = *(uintptr_t*)(moduleBase + 0x05C4A8D8);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return global;
}

static bool TryReadArrayElem(uintptr_t arrObj, int elem, int pArrayOff, int lengthOff, double* out) {
    __try {
        int length = *(int*)(arrObj + lengthOff);
        uintptr_t pArray = *(uintptr_t*)(arrObj + pArrayOff);
        if (!pArray || length <= 0 || length > 256 || elem < 0 || elem >= length) return false;
        uintptr_t rv = pArray + static_cast<uintptr_t>(elem) * 16;
        return ReadRValueNumber(rv, out);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadRvKind(uintptr_t rv, uint32_t* kindOut) {
    __try {
        *kindOut = *(uint32_t*)(rv + 0xC) & 0xFFFFFFu;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadRvPayload(uintptr_t rv, uintptr_t* payloadOut) {
    __try {
        *payloadOut = *(uintptr_t*)rv;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool TryReadU32(uintptr_t addr, uint32_t* out) {
    __try {
        *out = *(uint32_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string DumpGlobalVar(const char* name) {
    if (!name || !name[0]) return "error: name required";
    uintptr_t global = GlobalInstance();
    if (!global) return "error: global instance null";
    int index = FindCustomVarIndex(name);
    if (index < 0) return std::string("error: custom var not found: ") + name;

    uintptr_t rv = FindInstanceVarRValue(global, index);
    std::ostringstream ss;
    ss << "{\"name\":\"" << name << "\",\"index\":" << index
       << ",\"global\":\"0x" << std::hex << global
       << "\",\"rv\":\"0x" << (rv ? rv : 0) << std::dec << "\"";
    if (!rv) {
        ss << ",\"error\":\"rvalue not present\"}";
        return ss.str();
    }

    uint32_t kind = 0;
    if (!TryReadRvKind(rv, &kind)) {
        ss << ",\"error\":\"bad rvalue\"}";
        return ss.str();
    }
    ss << ",\"kind\":" << kind;

    double num = 0;
    if (ReadRValueNumber(rv, &num))
        ss << ",\"v\":" << num;

    uintptr_t payload = 0;
    TryReadRvPayload(rv, &payload);
    ss << ",\"ptr\":\"0x" << std::hex << payload << std::dec << "\"";

    // If array (kind 2), probe common RefDynamicArrayOfRValue layouts + raw header.
    if (kind == 2 && payload) {
        ss << ",\"raw32\":[";
        for (int i = 0; i < 16; ++i) {
            uint32_t w = 0xFFFFFFFFu;
            TryReadU32(payload + i * 4, &w);
            if (i) ss << ",";
            ss << "\"0x" << std::hex << w << std::dec << "\"";
        }
        ss << "],\"ptr_reads\":[";
        bool first = true;
        for (int i = 0; i < 16; ++i) {
            uint32_t w = 0;
            if (!TryReadU32(payload + i * 4, &w)) continue;
            // Likely user-mode heap pointer on Win32
            if (w < 0x01000000 || w > 0x7FFE0000) continue;
            uintptr_t cand = (uintptr_t)w;
            ss << (first ? "" : ",");
            first = false;
            ss << "{\"at\":" << (i * 4) << ",\"p\":\"0x" << std::hex << cand << std::dec
               << "\",\"elems\":[";
            for (int e = 0; e < 8; ++e) {
                double v = 0;
                uintptr_t erv = cand + static_cast<uintptr_t>(e) * 16;
                bool ok = ReadRValueNumber(erv, &v);
                if (e) ss << ",";
                if (ok) ss << v;
                else {
                    uint32_t k = 0xFFFFFFFFu;
                    TryReadU32(erv + 0xC, &k);
                    ss << "{\"kind\":" << (k & 0xFFFFFFu) << "}";
                }
            }
            ss << "]}";
        }
        ss << "],\"array_probes\":[";
        first = true;
        // Wider set of (pArrayOff, lengthOff) pairs.
        for (int pArrayOff = 4; pArrayOff <= 0x40; pArrayOff += 4) {
            for (int lengthOff = 4; lengthOff <= 0x40; lengthOff += 4) {
                if (pArrayOff == lengthOff) continue;
                double elems[8];
                int got = 0;
                for (int i = 0; i < 8; ++i) {
                    if (!TryReadArrayElem(payload, i, pArrayOff, lengthOff, &elems[i])) break;
                    got++;
                }
                if (got < 4) continue;
                // Prefer layouts whose elems look like CSS character ids.
                int charish = 0;
                for (int i = 0; i < got; ++i)
                    if (elems[i] >= 0 && elems[i] <= 40 && elems[i] == (double)(int)elems[i])
                        charish++;
                if (charish < 3) continue;
                if (!first) ss << ",";
                first = false;
                ss << "{\"pArrayOff\":" << pArrayOff << ",\"lengthOff\":" << lengthOff
                   << ",\"n\":" << got << ",\"elems\":[";
                for (int i = 0; i < got; ++i) {
                    if (i) ss << ",";
                    ss << elems[i];
                }
                ss << "]}";
            }
        }
        ss << "]";
    }
    ss << "}";
    return ss.str();
}

double WritePlayerOn(int player, double val) {
    return WritePlayerValue(player, val, {
        0x05C4A8D8, 0x2C, 0x10, 0x78C, 0x0, 0x4, 0x4, 0x310
        });
}


double WritePlayerPercent(int player, double val) {
    return WritePlayerValue(player, val, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1510
        });
}

double WritePlayerStock(int player, double val) {
    return WritePlayerValue(player, val, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1710
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

double ReadGameTeamsEnabled() { return 0; }

// --- oPlayer instance vars (CInstance hashmap / packed RValue array) ---

static const uintptr_t kCustomVarNamesRva = 0x05C5B540;
static const uintptr_t kCustomVarCountRva = 0x05C5B544;
static const uintptr_t kRunRoomRva = 0x05C66758;
static const int kOPlayerObjectIndex = 3;
static const int kPHitBoxObjectIndex = 6;
static const int kPBurnBoxObjectIndex = 17;
static const int kBubbleObjectIndex = 11;
static const int kPuddleObjectIndex = 13;
static const int kCssPlayerBgObjectIndex = 219;
static const int kCssPlayerPackedSlot = 0x1275; // set to 1..4 by local_charselect_room
static const int kMaxRoomInstances = 500;
static const int kMaxProjectiles = 64;
static const int kMaxGroundFires = 64;
static const int kMaxBubbles = 64;
static const int kMaxPuddles = 64;

enum OPlayerField {
    kUrl = 0,
    kState,
    kStateTimer,
    kTrackPlayer,
    kPrevState,
    kPrevPrevState,
    kAttack,
    kSprDir,
    kHsp,
    kVsp,
    kHasWalljump,
    kHasAirdodge,
    kDjumps,
    kAttackInvince,
    kRespawnInvinceTime,
    kHitstop,
    kHitstopFull,
    kStrongCharge,
    kWindow,
    kWindowTimer,
    kBurnTimer,
    kPlayer,
    kOPlayerFieldCount
};

static const char* kOPlayerFieldNames[kOPlayerFieldCount] = {
    "url", "state", "state_timer", "track_player", "prev_state", "prev_prev_state",
    "attack", "spr_dir", "hsp", "vsp", "has_walljump", "has_airdodge", "djumps",
    "attack_invince", "respawn_invince_time", "hitstop", "hitstop_full",
    "strong_charge", "window", "window_timer", "burn_timer", "player"
};

static int g_oPlayerFieldIndex[kOPlayerFieldCount];
static bool g_oPlayerFieldIndexReady = false;

static int FindCustomVarIndex(const char* name) {
    int found = -1;
    __try {
        uintptr_t moduleBase = GetModuleBase("RivalsofAether.exe");
        int count = *(int*)(moduleBase + kCustomVarCountRva);
        const char** names = *(const char***)(moduleBase + kCustomVarNamesRva);
        if (!names || count <= 0 || count > 200000) return -1;
        for (int i = 0; i < count; ++i) {
            const char* s = names[i];
            if (s && strcmp(s, name) == 0) {
                found = i;
                break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return found;
}

static void EnsureOPlayerFieldIndices() {
    if (g_oPlayerFieldIndexReady) return;
    for (int i = 0; i < kOPlayerFieldCount; ++i)
        g_oPlayerFieldIndex[i] = FindCustomVarIndex(kOPlayerFieldNames[i]);
    g_oPlayerFieldIndexReady = true;
}

static double* OPlayerFieldPtr(OPlayerState* st, int field) {
    switch (field) {
    case kUrl: return &st->url;
    case kState: return &st->state;
    case kStateTimer: return &st->state_timer;
    case kTrackPlayer: return &st->track_player;
    case kPrevState: return &st->prev_state;
    case kPrevPrevState: return &st->prev_prev_state;
    case kAttack: return &st->attack;
    case kSprDir: return &st->spr_dir;
    case kHsp: return &st->hsp;
    case kVsp: return &st->vsp;
    case kHasWalljump: return &st->has_walljump;
    case kHasAirdodge: return &st->has_airdodge;
    case kDjumps: return &st->djumps;
    case kAttackInvince: return &st->attack_invince;
    case kRespawnInvinceTime: return &st->respawn_invince_time;
    case kHitstop: return &st->hitstop;
    case kHitstopFull: return &st->hitstop_full;
    case kStrongCharge: return &st->strong_charge;
    case kWindow: return &st->window;
    case kWindowTimer: return &st->window_timer;
    case kBurnTimer: return &st->burn_timer;
    case kPlayer: return &st->player;
    default: return nullptr;
    }
}

// GMS RValue kinds we treat as numbers:
// 0=real, 7=int32, 10=int64, 13=bool. state/player/track_player are often int32.
static uintptr_t RoomListHead();
static bool ReadInstanceXY(uintptr_t instance, float* x, float* y);

static bool ReadRValueNumber(uintptr_t rv, double* out) {
    if (!rv || !out) return false;
    __try {
        uint32_t kind = *(uint32_t*)(rv + 0xC) & 0xFFFFFFu;
        switch (kind) {
        case 0:  // real
        case 13: // bool (stored as double 0/1 in this build)
            *out = *(double*)rv;
            return true;
        case 7:  // int32
            *out = (double)(int32_t)(*(uint32_t*)rv);
            return true;
        case 10: // int64
            *out = (double)(*(int64_t*)rv);
            return true;
        default:
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool WriteRValueNumber(uintptr_t rv, double val) {
    if (!rv) return false;
    __try {
        uint32_t kind = *(uint32_t*)(rv + 0xC) & 0xFFFFFFu;
        switch (kind) {
        case 0:  // real
        case 13: // bool
            *(double*)rv = val;
            return true;
        case 7:  // int32
            *(uint32_t*)rv = (uint32_t)(int32_t)val;
            return true;
        case 10: // int64
            *(int64_t*)rv = (int64_t)val;
            return true;
        default:
            // Kind 1 is a YYString. Overwriting that pointer crashes.
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Hashmap-only: packed slots exist for every index, so they can't mark a var as present.
static uintptr_t FindHashmapVarRValue(uintptr_t instance, int index) {
    if (!instance || index < 0) return 0;
    uintptr_t found = 0;
    __try {
        uintptr_t map = *(uintptr_t*)(instance + 0x2C);
        if (!map) return 0;
        int cap = *(int*)map;
        uintptr_t entries = *(uintptr_t*)(map + 0x10);
        if (!entries || cap <= 0 || cap > 65536) return 0;
        for (int i = 0; i < cap; ++i) {
            uintptr_t e = entries + static_cast<uintptr_t>(i) * 12;
            if ((int32_t)*(uint32_t*)(e + 8) <= 0) continue;
            if ((int32_t)*(uint32_t*)(e + 4) != index) continue;
            found = *(uintptr_t*)e;
            break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return found;
}

static uintptr_t FindPackedVarRValue(uintptr_t instance, int index) {
    if (!instance || index < 0) return 0;
    uintptr_t rv = 0;
    __try {
        uintptr_t packed = *(uintptr_t*)(instance + 0x4);
        if (!packed) return 0;
        rv = packed + static_cast<uintptr_t>(index) * 16;
        (void)*(uint32_t*)(rv + 0xC);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return rv;
}

static uintptr_t FindInstanceVarRValue(uintptr_t instance, int index) {
    uintptr_t rv = FindHashmapVarRValue(instance, index);
    if (rv) return rv;
    return FindPackedVarRValue(instance, index);
}

// CSS portraits store the selected character id in old_char (1=random,
// 2=Zetterburn, 3=Orcane, ...). In-match oPlayer uses url; that var is not
// present on cs_playerbg_obj.
static int g_cssChoiceIndex = -2;
static int g_cssPlayerIndex = -2;
static int g_cssCursorIndex = -2;
static int g_cssDrawIndex = -2;
static int g_cssNameWidthIndex = -2;

static int CssSlotFromInstance(uintptr_t instance) {
    int slot = -1;
    double playerVal = 0;
    uintptr_t packedPlayer = FindPackedVarRValue(instance, kCssPlayerPackedSlot);
    if (packedPlayer && ReadRValueNumber(packedPlayer, &playerVal)) {
        int p = (int)playerVal;
        if (p >= 1 && p <= 4) return p - 1;
    }
    if (g_cssPlayerIndex >= 0) {
        uintptr_t playerRv = FindHashmapVarRValue(instance, g_cssPlayerIndex);
        if (playerRv && ReadRValueNumber(playerRv, &playerVal)) {
            int p = (int)playerVal;
            if (p >= 1 && p <= 4) return p - 1;
        }
    }
    float x = 0, y = 0;
    if (ReadInstanceXY(instance, &x, &y)) {
        int fromX = (int)((x + 100.0f) / 238.0f);
        if (fromX >= 0 && fromX <= 3) slot = fromX;
    }
    return slot;
}

static void EnsureCssFieldIndices() {
    if (g_cssChoiceIndex < 0)
        g_cssChoiceIndex = FindCustomVarIndex("old_char");
    if (g_cssPlayerIndex < 0)
        g_cssPlayerIndex = FindCustomVarIndex("player");
    if (g_cssCursorIndex < 0)
        g_cssCursorIndex = FindCustomVarIndex("cursor_id");
    if (g_cssDrawIndex < 0)
        g_cssDrawIndex = FindCustomVarIndex("draw_index");
    if (g_cssNameWidthIndex < 0)
        g_cssNameWidthIndex = FindCustomVarIndex("name_width");
}

// Live CSS character pick: CE array parallel to cursor_y (+0x600).
static uintptr_t FindPlayerChoiceRValue(int player) {
    if (player < 0 || player > 3) return 0;
    // These slots are plain doubles (kind-less CE memory), not GML RValues.
    // Callers use Read/Write via the address as a double*.
    return PlayerCssChoiceAddr(player);
}

bool TryReadPlayerChoice(int player, double* out) {
    if (player < 0 || player > 3 || !out) return false;
    uintptr_t addr = FindPlayerChoiceRValue(player);
    if (!addr) return false;
    __try {
        *out = *(double*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

double ReadPlayerChoice(int player) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    double val = 0;
    if (!TryReadPlayerChoice(player, &val)) return 0;
    return val;
}

bool WritePlayerChoice(int player, double val) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    uintptr_t addr = FindPlayerChoiceRValue(player);
    if (!addr) return false;
    __try {
        *(double*)addr = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string ResolvePlayerChoiceAddr(int player) {
    uintptr_t addr = FindPlayerChoiceRValue(player);
    uintptr_t cy = PlayerCursorYAddr(player);
    double val = 0;
    bool ok = TryReadPlayerChoice(player, &val);
    std::ostringstream ss;
    ss << "{\"player\":" << player
       << ",\"choice_addr\":\"0x" << std::hex << addr
       << "\",\"cursor_y_addr\":\"0x" << cy << std::dec << "\""
       << ",\"ok\":" << (ok ? "true" : "false");
    if (ok) ss << ",\"v\":" << val;
    ss << "}";
    return ss.str();
}

enum ProjField {
    kProjHsp = 0,
    kProjVsp,
    kProjSprDir,
    kProjPlayer,
    kProjFieldCount
};

static const char* kProjFieldNames[kProjFieldCount] = {
    "hsp", "vsp", "spr_dir", "player"
};

static int g_projFieldIndex[kProjFieldCount];
static bool g_projFieldIndexReady = false;

static int g_playerVarIndex = -2; // -2 = not resolved yet

static void EnsureProjFieldIndices() {
    if (g_projFieldIndexReady) return;
    for (int i = 0; i < kProjFieldCount; ++i)
        g_projFieldIndex[i] = FindCustomVarIndex(kProjFieldNames[i]);
    g_projFieldIndexReady = true;
}

static int PlayerVarIndex() {
    if (g_playerVarIndex == -2)
        g_playerVarIndex = FindCustomVarIndex("player");
    return g_playerVarIndex;
}

// Fill values[0..nFields) from the hashmap and/or packed RValue array.
static bool ReadCustomVars(uintptr_t instance, const int* indices, int nFields,
    double* values, uint32_t* have)
{
    if (!indices || !values || !have || nFields <= 0) return false;
    memset(values, 0, sizeof(double) * nFields);
    *have = 0;

    __try {
        uintptr_t packed = *(uintptr_t*)(instance + 0x4);
        uintptr_t map = *(uintptr_t*)(instance + 0x2C);

        if (map) {
            int cap = *(int*)map;
            uintptr_t entries = *(uintptr_t*)(map + 0x10);
            if (entries && cap > 0 && cap <= 65536) {
                for (int i = 0; i < cap; ++i) {
                    uintptr_t e = entries + static_cast<uintptr_t>(i) * 12;
                    if ((int32_t)*(uint32_t*)(e + 8) <= 0) continue;
                    int key = (int32_t)*(uint32_t*)(e + 4);
                    uintptr_t rv = *(uintptr_t*)e;
                    for (int f = 0; f < nFields; ++f) {
                        if (indices[f] < 0 || indices[f] != key) continue;
                        if (ReadRValueNumber(rv, &values[f]))
                            *have |= (1u << f);
                    }
                }
            }
        }

        if (packed) {
            for (int f = 0; f < nFields; ++f) {
                if (indices[f] < 0 || (*have & (1u << f))) continue;
                uintptr_t rv = packed + static_cast<uintptr_t>(indices[f]) * 16;
                if (ReadRValueNumber(rv, &values[f]))
                    *have |= (1u << f);
            }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadInstanceXY(uintptr_t instance, float* x, float* y) {
    __try {
        *x = *(float*)(instance + 0xA0);
        *y = *(float*)(instance + 0xA4);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool ReadOPlayerFromInstance(uintptr_t instance, OPlayerState* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    EnsureOPlayerFieldIndices();

    if (!ReadInstanceXY(instance, &out->x, &out->y)) return false;

    double values[kOPlayerFieldCount];
    if (!ReadCustomVars(instance, g_oPlayerFieldIndex, kOPlayerFieldCount, values, &out->have))
        return false;

    for (int f = 0; f < kOPlayerFieldCount; ++f) {
        double* dst = OPlayerFieldPtr(out, f);
        if (dst) *dst = values[f];
    }
    out->valid = true;
    return true;
}

static bool ReadProjectileFromInstance(uintptr_t instance, ProjectileState* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    EnsureProjFieldIndices();
    if (!ReadInstanceXY(instance, &out->x, &out->y)) return false;

    double values[kProjFieldCount];
    if (!ReadCustomVars(instance, g_projFieldIndex, kProjFieldCount, values, &out->have))
        return false;

    out->hsp = values[kProjHsp];
    out->vsp = values[kProjVsp];
    out->spr_dir = values[kProjSprDir];
    out->player = values[kProjPlayer];
    out->valid = true;
    return true;
}

static bool ReadGroundFireFromInstance(uintptr_t instance, FireState* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!ReadInstanceXY(instance, &out->x, &out->y)) return false;

    int playerIndex = PlayerVarIndex();
    double playerVal = 0;
    uint32_t have = 0;
    if (playerIndex >= 0)
        ReadCustomVars(instance, &playerIndex, 1, &playerVal, &have);

    out->player = playerVal;
    out->have = have;
    out->valid = true;
    return true;
}

enum BubbleField {
    kBubbleHsp = 0,
    kBubbleVsp,
    kBubblePlayer,
    kBubbleFieldCount
};

static const char* kBubbleFieldNames[kBubbleFieldCount] = {
    "hsp", "vsp", "player"
};

static int g_bubbleFieldIndex[kBubbleFieldCount];
static bool g_bubbleFieldIndexReady = false;

static void EnsureBubbleFieldIndices() {
    if (g_bubbleFieldIndexReady) return;
    for (int i = 0; i < kBubbleFieldCount; ++i)
        g_bubbleFieldIndex[i] = FindCustomVarIndex(kBubbleFieldNames[i]);
    g_bubbleFieldIndexReady = true;
}

static bool ReadBubbleFromInstance(uintptr_t instance, BubbleState* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    EnsureBubbleFieldIndices();
    if (!ReadInstanceXY(instance, &out->x, &out->y)) return false;

    double values[kBubbleFieldCount];
    if (!ReadCustomVars(instance, g_bubbleFieldIndex, kBubbleFieldCount, values, &out->have))
        return false;

    out->hsp = values[kBubbleHsp];
    out->vsp = values[kBubbleVsp];
    out->player = values[kBubblePlayer];
    out->valid = true;
    return true;
}

static bool ReadPuddleFromInstance(uintptr_t instance, PuddleState* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!ReadInstanceXY(instance, &out->x, &out->y)) return false;

    int playerIndex = PlayerVarIndex();
    double playerVal = 0;
    uint32_t have = 0;
    if (playerIndex >= 0)
        ReadCustomVars(instance, &playerIndex, 1, &playerVal, &have);

    out->player = playerVal;
    out->have = have;
    out->valid = true;
    return true;
}

static uintptr_t RoomListHead() {
    uintptr_t current = 0;
    __try {
        uintptr_t moduleBase = GetModuleBase("RivalsofAether.exe");
        uintptr_t runRoom = *(uintptr_t*)(moduleBase + kRunRoomRva);
        current = runRoom ? *(uintptr_t*)(runRoom + 0x80) : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return current;
}

// There is no durable GML bool named game_is_running. The game's own check is
// the script is_gameplay_room(), which ORs many room ids. As a memory proxy we
// treat a match as "running" when the current room has a live gameplay_parent
// (match controller) or at least one live oPlayer. CSS / menus have neither.
static bool InstanceObjectNameEquals(uintptr_t instance, const char* want) {
    if (!instance || !want) return false;
    bool match = false;
    __try {
        uintptr_t objectGm = *(uintptr_t*)(instance + 0x68);
        if (!objectGm) return false;
        const char* name = *(const char**)objectGm;
        if (!name) return false;
        match = (strcmp(name, want) == 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return match;
}

bool ReadGameIsRunning() {
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0) {
                int32_t objectIndex = *(int32_t*)(current + 0x7C);
                if (objectIndex == kOPlayerObjectIndex)
                    return true;
                if (InstanceObjectNameEquals(current, "gameplay_parent"))
                    return true;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

// CSS and stage/map select share the same game-stage id. Distinguish map
// select by the ss_* objects that only exist in local/network stage_select
// rooms (ss_stagebox_obj tiles, ss_stage_header_obj). Character select uses
// cs_playerbg_obj instead and will return false here.
bool ReadIsMapSelection() {
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0) {
                if (InstanceObjectNameEquals(current, "ss_stagebox_obj") ||
                    InstanceObjectNameEquals(current, "ss_stage_header_obj"))
                    return true;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

// Versus results rooms instance draw_result_screen / result_screen_box.
// Story mode uses chapter_results_object; include it so we don't miss
// those rooms either. game_stage is not a stable id for this screen.
bool ReadIsPostMatch() {
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0) {
                if (InstanceObjectNameEquals(current, "draw_result_screen") ||
                    InstanceObjectNameEquals(current, "result_screen_box") ||
                    InstanceObjectNameEquals(current, "chapter_results_object"))
                    return true;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

static bool TryReadGlobalNumber(const char* name, double* out) {
    if (!name || !out) return false;
    uintptr_t global = GlobalInstance();
    if (!global) return false;
    int index = FindCustomVarIndex(name);
    if (index < 0) return false;
    uintptr_t rv = FindInstanceVarRValue(global, index);
    return ReadRValueNumber(rv, out);
}

static bool GlobalNumberNonzero(const char* name) {
    double v = 0;
    return TryReadGlobalNumber(name, &v) && v != 0.0;
}

bool TryReadGameplayTime(double* out) {
    return TryReadGlobalNumber("gameplay_time", out);
}

// 3 / 2 / 1 / GO are each shown for 30 frames (get_gameplay_time packed
// slot 0xb6b). Fight inputs unlock as oPlayers leave PS_SPAWN at GO.
static const double kCountdownUnlockFrames = 120.0;
static const double kPsSpawn = 24.0;

double ReadCountdownRemaining() {
    if (!ReadGameIsRunning()) return 0;
    double t = 0;
    if (!TryReadGameplayTime(&t)) return 0;
    if (t >= kCountdownUnlockFrames) return 0;
    return kCountdownUnlockFrames - t;
}

bool ReadCanMakeInputs() {
    if (!ReadGameIsRunning()) return false;
    if (ReadIsPostMatch()) return false;
    if (GlobalNumberNonzero("gameplay_has_stopped")) return false;
    if (GlobalNumberNonzero("game_ending")) return false;

    OPlayerState players[4];
    ReadOPlayerInstances(players);
    for (int i = 0; i < 4; ++i) {
        if (!players[i].valid) continue;
        if (!(players[i].have & (1u << kState))) continue;
        if (players[i].state != kPsSpawn)
            return true;
    }
    return false;
}

int ReadOPlayerInstances(OPlayerState out[4]) {
    if (!out) return 0;
    for (int i = 0; i < 4; ++i)
        memset(&out[i], 0, sizeof(out[i]));

    int assigned = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0 &&
                *(int32_t*)(current + 0x7C) == kOPlayerObjectIndex) {
                OPlayerState st;
                if (ReadOPlayerFromInstance(current, &st)) {
                    int slot = -1;
                    if (st.have & (1u << kPlayer))
                        slot = (int)st.player - 1;
                    else if (st.have & (1u << kTrackPlayer))
                        slot = (int)st.track_player - 1;
                    if (slot >= 0 && slot < 4) {
                        out[slot] = st;
                        assigned++;
                    }
                }
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return assigned;
    }
    return assigned;
}

int ReadProjectileInstances(ProjectileState* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    int n = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances && n < maxCount) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0 &&
                *(int32_t*)(current + 0x7C) == kPHitBoxObjectIndex) {
                if (ReadProjectileFromInstance(current, &out[n]))
                    n++;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

int ReadGroundFireInstances(FireState* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    int n = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances && n < maxCount) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0 &&
                *(int32_t*)(current + 0x7C) == kPBurnBoxObjectIndex) {
                if (ReadGroundFireFromInstance(current, &out[n]))
                    n++;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

int ReadBubbleInstances(BubbleState* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    int n = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances && n < maxCount) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0 &&
                *(int32_t*)(current + 0x7C) == kBubbleObjectIndex) {
                if (ReadBubbleFromInstance(current, &out[n]))
                    n++;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
}

int ReadPuddleInstances(PuddleState* out, int maxCount) {
    if (!out || maxCount <= 0) return 0;
    int n = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances && n < maxCount) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            if ((flags & 0x3) == 0 &&
                *(int32_t*)(current + 0x7C) == kPuddleObjectIndex) {
                if (ReadPuddleFromInstance(current, &out[n]))
                    n++;
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return n;
    }
    return n;
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

    OPlayerState instPlayers[4];
    ReadOPlayerInstances(instPlayers);

    // --- Players ---
    json << "\"players\":[";
    for (int p = 0; p < 4; ++p) {
        if (p > 0) json << ",";

        double onVal = 0;
        bool onOk = TryReadIntArg(ReadPlayerOn, p, onVal);
        bool isOn = onOk && (onVal != 0.0);
        const OPlayerState& inst = instPlayers[p];
        if (inst.valid) isOn = true;

        json << "{\"on\":" << (isOn ? "true" : "false");

        if (isOn) {
            double v;
            if (TryReadIntArg(ReadPlayerPercent, p, v)) json << ",\"percent\":" << v;
            if (TryReadIntArg(ReadPlayerStock, p, v))   json << ",\"stock\":" << v;

            if (inst.valid) {
                json << ",\"x\":" << inst.x << ",\"y\":" << inst.y;
                auto addInst = [&](OPlayerField field, const char* name, double value) {
                    if (inst.have & (1u << field))
                        json << ",\"" << name << "\":" << value;
                };
                addInst(kUrl, "url", inst.url);
                addInst(kState, "state", inst.state);
                addInst(kStateTimer, "state_timer", inst.state_timer);
                addInst(kTrackPlayer, "track_player", inst.track_player);
                addInst(kPrevState, "prev_state", inst.prev_state);
                addInst(kPrevPrevState, "prev_prev_state", inst.prev_prev_state);
                addInst(kAttack, "attack", inst.attack);
                addInst(kSprDir, "spr_dir", inst.spr_dir);
                addInst(kHsp, "hsp", inst.hsp);
                addInst(kVsp, "vsp", inst.vsp);
                addInst(kHasWalljump, "has_walljump", inst.has_walljump);
                addInst(kHasAirdodge, "has_airdodge", inst.has_airdodge);
                addInst(kDjumps, "djumps", inst.djumps);
                addInst(kAttackInvince, "attack_invince", inst.attack_invince);
                addInst(kRespawnInvinceTime, "respawn_invince_time", inst.respawn_invince_time);
                addInst(kHitstop, "hitstop", inst.hitstop);
                addInst(kHitstopFull, "hitstop_full", inst.hitstop_full);
                addInst(kStrongCharge, "strong_charge", inst.strong_charge);
                addInst(kWindow, "window", inst.window);
                addInst(kWindowTimer, "window_timer", inst.window_timer);
                addInst(kBurnTimer, "burn_timer", inst.burn_timer);
                addInst(kPlayer, "player", inst.player);
            }
            else if (TryReadPlayerChoice(p, &v)) {
                json << ",\"url\":" << v;
            }
        }
        json << "}";
    }
    json << "],";

    ProjectileState projectiles[kMaxProjectiles];
    int nProj = ReadProjectileInstances(projectiles, kMaxProjectiles);
    json << "\"projectiles\":[";
    for (int i = 0; i < nProj; ++i) {
        if (i > 0) json << ",";
        const ProjectileState& hit = projectiles[i];
        json << "{\"x\":" << hit.x << ",\"y\":" << hit.y;
        if (hit.have & (1u << kProjHsp)) json << ",\"hsp\":" << hit.hsp;
        if (hit.have & (1u << kProjVsp)) json << ",\"vsp\":" << hit.vsp;
        if (hit.have & (1u << kProjSprDir)) json << ",\"spr_dir\":" << hit.spr_dir;
        if (hit.have & (1u << kProjPlayer)) json << ",\"player\":" << hit.player;
        json << "}";
    }
    json << "],";

    FireState ground[kMaxGroundFires];
    int nGround = ReadGroundFireInstances(ground, kMaxGroundFires);
    json << "\"ground\":[";
    for (int i = 0; i < nGround; ++i) {
        if (i > 0) json << ",";
        const FireState& fire = ground[i];
        json << "{\"x\":" << fire.x << ",\"y\":" << fire.y;
        if (fire.have & 1u) json << ",\"player\":" << fire.player;
        json << "}";
    }
    json << "],";

    BubbleState bubbles[kMaxBubbles];
    int nBubbles = ReadBubbleInstances(bubbles, kMaxBubbles);
    json << "\"bubbles\":[";
    for (int i = 0; i < nBubbles; ++i) {
        if (i > 0) json << ",";
        const BubbleState& bubble = bubbles[i];
        json << "{\"x\":" << bubble.x << ",\"y\":" << bubble.y;
        if (bubble.have & (1u << kBubbleHsp)) json << ",\"hsp\":" << bubble.hsp;
        if (bubble.have & (1u << kBubbleVsp)) json << ",\"vsp\":" << bubble.vsp;
        if (bubble.have & (1u << kBubblePlayer)) json << ",\"player\":" << bubble.player;
        json << "}";
    }
    json << "],";

    PuddleState puddles[kMaxPuddles];
    int nPuddles = ReadPuddleInstances(puddles, kMaxPuddles);
    json << "\"puddles\":[";
    for (int i = 0; i < nPuddles; ++i) {
        if (i > 0) json << ",";
        const PuddleState& puddle = puddles[i];
        json << "{\"x\":" << puddle.x << ",\"y\":" << puddle.y;
        if (puddle.have & 1u) json << ",\"player\":" << puddle.player;
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
    addField("stage", ReadGameStage);
    addField("clock", ReadGameClock);
    addField("teams_enabled", ReadGameTeamsEnabled);
    json << "}";

    json << "}";
    return json.str();
}