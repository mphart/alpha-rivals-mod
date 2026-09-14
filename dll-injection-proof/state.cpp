#include "pch.h"
#include "framework.h"
#include "state.h"
#include "util.h"
#include <cstring>

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
    //Log("ReadPlayerTeam(" + std::to_string(player) + ")");
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1810
    });
}

double ReadPlayerCursorY(int player) {
    return ReadPlayerValue(player, {
        0x05C4A8D8, 0x2C, 0x10, 0x198, 0x10, 0x24, 0xC, 0x1C10
    });
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

static int g_cssUrlIndex = -2;
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
    if (g_cssUrlIndex < 0)
        g_cssUrlIndex = FindCustomVarIndex("url");
    if (g_cssPlayerIndex < 0)
        g_cssPlayerIndex = FindCustomVarIndex("player");
    if (g_cssCursorIndex < 0)
        g_cssCursorIndex = FindCustomVarIndex("cursor_id");
    if (g_cssDrawIndex < 0)
        g_cssDrawIndex = FindCustomVarIndex("draw_index");
    if (g_cssNameWidthIndex < 0)
        g_cssNameWidthIndex = FindCustomVarIndex("name_width");
}

// CSS portraits are cs_playerbg_obj (object_index 219), one instance per slot.
static uintptr_t FindPlayerChoiceRValue(int player) {
    if (player < 0 || player > 3) return 0;
    EnsureCssFieldIndices();

    uintptr_t found = 0;
    __try {
        uintptr_t current = RoomListHead();
        int scanned = 0;
        while (current != 0 && scanned < kMaxRoomInstances) {
            uint32_t flags = *(uint32_t*)(current + 0x74);
            int32_t objectIndex = *(int32_t*)(current + 0x7C);
            if ((flags & 0x3) == 0 && objectIndex == kCssPlayerBgObjectIndex) {
                if (CssSlotFromInstance(current) == player) {
                    if (g_cssUrlIndex >= 0)
                        found = FindHashmapVarRValue(current, g_cssUrlIndex);
                    if (found) break;
                }
            }
            current = *(uintptr_t*)(current + 0x130);
            scanned++;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return found;
}

double ReadPlayerChoice(int player) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    double val = 0;
    uintptr_t rv = FindPlayerChoiceRValue(player);
    if (!rv || !ReadRValueNumber(rv, &val)) return 0;
    return val;
}

double WritePlayerChoice(int player, double val) {
    if (player < 0 || player > 3) { throw std::invalid_argument("player must be between 0 and 3"); }
    uintptr_t rv = FindPlayerChoiceRValue(player);
    if (!rv || !WriteRValueNumber(rv, val)) return 0;
    return val;
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

static bool ReadGroundFireFromInstance(uintptr_t instance, GroundFireState* out) {
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

int ReadGroundFireInstances(GroundFireState* out, int maxCount) {
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
            else if (TryReadIntArg(ReadPlayerChoice, p, v)) {
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

    GroundFireState ground[kMaxGroundFires];
    int nGround = ReadGroundFireInstances(ground, kMaxGroundFires);
    json << "\"ground\":[";
    for (int i = 0; i < nGround; ++i) {
        if (i > 0) json << ",";
        const GroundFireState& fire = ground[i];
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