// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "state.h"
#include "util.h"
#include "minhook.h"
#include <Xinput.h>   // XINPUT_STATE, XINPUT_CAPABILITIES, button bit constants
#include <dbt.h>      // DBT_DEVNODES_CHANGED for gamepad rediscovery
#include <sstream>    // for hex formatting in log lines
#include <cstring>

// Runner stores the main HWND here (VA 0x060662F4).
static const uintptr_t kMainWindowRva = 0x05C662F4;

// Custom GML instance vars live at slot 100000 + index (0x186A0).
static const int kGmlInstanceVarBase = 0x186A0;
// DAT_0605b540 / DAT_0605b544: char** name table indexed by that custom index.
static const uintptr_t kCustomVarNamesRva = 0x05C5B540;
static const uintptr_t kCustomVarCountRva = 0x05C5B544;

static void CopyGameCString(const char* s, char* dst, int dstSize) {
    if (!dst || dstSize <= 0) return;
    dst[0] = '\0';
    __try {
        if (!s) return;
        int i = 0;
        for (; i < dstSize - 1 && s[i]; ++i) dst[i] = s[i];
        dst[i] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        dst[0] = '\0';
    }
}

static void AppendJsonString(std::stringstream& ss, const char* s) {
    char buf[80];
    CopyGameCString(s, buf, sizeof(buf));
    ss << '"';
    for (int i = 0; buf[i]; ++i) {
        char c = buf[i];
        if (c == '"' || c == '\\') ss << '\\';
        if (static_cast<unsigned char>(c) >= 32) ss << c;
    }
    ss << '"';
}

static bool SafeRead32(uintptr_t addr, uint32_t* out) {
    __try {
        *out = *(uint32_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadPtr(uintptr_t addr, uintptr_t* out) {
    __try {
        *out = *(uintptr_t*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SafeReadDouble(uintptr_t addr, double* out) {
    __try {
        *out = *(double*)addr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static const char* GetInstanceObjectName(uintptr_t instance) {
    const char* name = nullptr;
    __try {
        uintptr_t objectGm = *(uintptr_t*)(instance + 0x68);
        if (objectGm) name = *(const char**)objectGm;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        name = nullptr;
    }
    return name;
}

// Scan DAT_0605b540 for a custom instance-var name. Returns the index, or -1.
// FUN_0554af20 only searches builtins, so "hsp"/"vsp" always miss there.
static int FindCustomVarIndex(uintptr_t moduleBase, const char* name) {
    int found = -1;
    __try {
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

static const char* CustomVarName(uintptr_t moduleBase, int index) {
    const char* name = nullptr;
    __try {
        int count = *(int*)(moduleBase + kCustomVarCountRva);
        const char** names = *(const char***)(moduleBase + kCustomVarNamesRva);
        if (names && index >= 0 && index < count) name = names[index];
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        name = nullptr;
    }
    return name;
}

// Custom vars: packed RValue array at CInstance+0x4, else CHashMap at +0x2C
// (12-byte nodes: value*, key, hash). FUN_0554b070 / FUN_055534b0.
static bool ReadInstCustomReal(uintptr_t instance, int index, double* out) {
    if (index < 0 || !out) return false;
    __try {
        uintptr_t arr = *(uintptr_t*)(instance + 0x4);
        if (arr) {
            uintptr_t rv = arr + static_cast<uintptr_t>(index) * 16;
            uint32_t kind = *(uint32_t*)(rv + 0xC) & 0xFFFFFFu;
            if (kind != 0) return false;
            *out = *(double*)rv;
            return true;
        }
        uintptr_t map = *(uintptr_t*)(instance + 0x2C);
        if (!map) return false;
        int cap = *(int*)map;
        uintptr_t entries = *(uintptr_t*)(map + 0x10);
        if (!entries || cap <= 0 || cap > 65536) return false;
        for (int i = 0; i < cap; ++i) {
            uintptr_t e = entries + static_cast<uintptr_t>(i) * 12;
            if ((int32_t)*(uint32_t*)(e + 8) <= 0) continue;
            if ((int32_t)*(uint32_t*)(e + 4) != index) continue;
            uintptr_t rv = *(uintptr_t*)e;
            if (!rv) return false;
            uint32_t kind = *(uint32_t*)(rv + 0xC) & 0xFFFFFFu;
            if (kind != 0) return false;
            *out = *(double*)rv;
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

static void AppendRValueJson(std::stringstream& ss, uintptr_t moduleBase, int index, uintptr_t rv) {
    ss << "{\"i\":" << index << ",\"name\":";
    AppendJsonString(ss, CustomVarName(moduleBase, index));
    uint32_t kindRaw = 0xFFFFFFFF;
    if (!rv || !SafeRead32(rv + 0xC, &kindRaw)) {
        ss << ",\"kind\":-1}";
        return;
    }
    uint32_t kind = kindRaw & 0xFFFFFFu;
    ss << ",\"kind\":" << kind;
    if (kind == 0 || kind == 13) {
        double v = 0;
        if (SafeReadDouble(rv, &v)) ss << ",\"v\":" << v;
    }
    else if (kind == 7) {
        uint32_t raw = 0;
        if (SafeRead32(rv, &raw)) ss << ",\"v\":" << (int32_t)raw;
    }
    else {
        uintptr_t payload = 0;
        SafeReadPtr(rv, &payload);
        ss << ",\"ptr\":\"0x" << std::hex << payload << std::dec << "\"";
    }
    ss << "}";
}

static void AppendInstanceVarsJson(std::stringstream& ss, uintptr_t moduleBase, uintptr_t inst) {
    uintptr_t packed = 0, map = 0;
    uint32_t n30 = 0, used = 0, cap = 0;
    SafeReadPtr(inst + 0x4, &packed);
    SafeReadPtr(inst + 0x2C, &map);
    SafeRead32(inst + 0x30, &n30);
    if (map) {
        SafeRead32(map, &cap);
        SafeRead32(map + 0x4, &used);
    }

    ss << "\"packed\":\"0x" << std::hex << packed
        << "\",\"yyvars\":\"0x" << map
        << "\",\"n30\":" << std::dec << n30
        << ",\"map_cap\":" << cap
        << ",\"map_used\":" << used
        << ",\"vars\":[";

    bool first = true;
    int emitted = 0;
    const int kMax = 600;

    uintptr_t entries = 0;
    if (map) SafeReadPtr(map + 0x10, &entries);
    if (entries && cap > 0 && cap <= 65536) {
        for (uint32_t i = 0; i < cap && emitted < kMax; ++i) {
            uintptr_t e = entries + static_cast<uintptr_t>(i) * 12;
            uint32_t hash = 0, key = 0;
            uintptr_t rv = 0;
            if (!SafeRead32(e + 8, &hash) || (int32_t)hash <= 0) continue;
            SafeRead32(e + 4, &key);
            SafeReadPtr(e, &rv);
            if (!first) ss << ",";
            first = false;
            AppendRValueJson(ss, moduleBase, (int32_t)key, rv);
            emitted++;
        }
    }
    else if (packed && n30 > 0 && n30 < 2000) {
        uint32_t n = n30 < (uint32_t)kMax ? n30 : (uint32_t)kMax;
        for (uint32_t i = 0; i < n; ++i) {
            if (!first) ss << ",";
            first = false;
            AppendRValueJson(ss, moduleBase, (int)i, packed + static_cast<uintptr_t>(i) * 16);
            emitted++;
        }
    }

    ss << "]";
    if (emitted >= kMax) ss << ",\"truncated\":true";

    static const int kPackedSlots[] = { 0x9f, 0xd12, 0x2c5, 0x5b2, 0x5be, 0x14f5, 0x1275 };
    ss << ",\"packed_slots\":[";
    bool pfirst = true;
    if (packed) {
        for (int slot : kPackedSlots) {
            uintptr_t rv = packed + static_cast<uintptr_t>(slot) * 16;
            uint32_t kindRaw = 0;
            if (!SafeRead32(rv + 0xC, &kindRaw)) continue;
            uint32_t kind = kindRaw & 0xFFFFFFu;
            double v = 0;
            if (kind == 0 || kind == 13) {
                if (!SafeReadDouble(rv, &v)) continue;
            }
            else if (kind == 7) {
                uint32_t raw = 0;
                if (!SafeRead32(rv, &raw)) continue;
                v = (int32_t)raw;
            }
            else {
                continue;
            }
            if (!pfirst) ss << ",";
            pfirst = false;
            ss << "{\"i\":" << slot << ",\"kind\":" << kind << ",\"v\":" << v << "}";
        }
    }
    ss << "]";
}

// ============================================================
// Game window resolution (needed for focus-hook spoofing below)
// ============================================================

// HWND g_gameWindowHandle = nullptr;

// void ResolveGameWindow() {
//     DWORD myPid = GetCurrentProcessId();
//     g_gameWindowHandle = nullptr;

//     EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
//         DWORD pid;
//         GetWindowThreadProcessId(hwnd, &pid);
//         if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
//             g_gameWindowHandle = hwnd;
//             return FALSE; // stop enumerating, found it
//         }
//         return TRUE;
//         }, 0);

//     Log("Resolved game window handle: " + std::to_string((uintptr_t)g_gameWindowHandle));
// }

// ============================================================
// Focus hooks (GetFocus / GetForegroundWindow / GetActiveWindow)
// ============================================================
// RoA likely only polls keyboard state when it believes it has focus.
// These hooks make the game always believe its own window is focused,
// regardless of which window Windows actually considers active -- this
// lets input injection work even while the game runs in the background.

// typedef HWND(WINAPI* GetFocus_t)();
// GetFocus_t OriginalGetFocus = nullptr;

// HWND WINAPI HookedGetFocus() {
//     if (g_gameWindowHandle) return g_gameWindowHandle;
//     return OriginalGetFocus();
// }

// typedef HWND(WINAPI* GetForegroundWindow_t)();
// GetForegroundWindow_t OriginalGetForegroundWindow = nullptr;

// HWND WINAPI HookedGetForegroundWindow() {
//     if (g_gameWindowHandle) return g_gameWindowHandle;
//     return OriginalGetForegroundWindow();
// }

// typedef HWND(WINAPI* GetActiveWindow_t)();
// GetActiveWindow_t OriginalGetActiveWindow = nullptr;

// HWND WINAPI HookedGetActiveWindow() {
//     if (g_gameWindowHandle) return g_gameWindowHandle;
//     return OriginalGetActiveWindow();
// }

// ============================================================
// XInput hooks (XInputGetState + XInputGetCapabilities)
// ============================================================

typedef DWORD(WINAPI* XInputGetState_t)(DWORD dwUserIndex, XINPUT_STATE* pState);
XInputGetState_t OriginalXInputGetState = nullptr;

typedef DWORD(WINAPI* XInputGetCapabilities_t)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
XInputGetCapabilities_t OriginalXInputGetCapabilities = nullptr;

const int MAX_JOYSTICKS = 4;

volatile bool  g_overrideJoystick[MAX_JOYSTICKS] = { false };
volatile WORD  g_forcedButtons[MAX_JOYSTICKS] = { 0 };
volatile SHORT g_forcedThumbLX[MAX_JOYSTICKS] = { 0 };
volatile SHORT g_forcedThumbLY[MAX_JOYSTICKS] = { 0 };
volatile BYTE  g_forcedLeftTrigger[MAX_JOYSTICKS] = { 0 };
volatile BYTE  g_forcedRightTrigger[MAX_JOYSTICKS] = { 0 };

// Kept for the set_key pipe command; keyboard hooks are currently disabled.
volatile bool g_overrideKeys[256] = { false };
volatile bool g_forcedKeyState[256] = { false };

bool g_logRealXInput = true;

static void ClearForcedJoystick(int joyIndex) {
    if (joyIndex < 0 || joyIndex >= MAX_JOYSTICKS) return;
    g_forcedButtons[joyIndex] = 0;
    g_forcedThumbLX[joyIndex] = 0;
    g_forcedThumbLY[joyIndex] = 0;
    g_forcedLeftTrigger[joyIndex] = 0;
    g_forcedRightTrigger[joyIndex] = 0;
}

// SEH helper kept free of C++ objects (C2712).
static HWND SafeReadMainWindowHwnd(HMODULE game) {
    HWND hwnd = nullptr;
    __try {
        hwnd = *(HWND*)((uintptr_t)game + kMainWindowRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        hwnd = nullptr;
    }
    return hwnd;
}

// GameMaker only rescans XInput slots 0-3 on init / WM_DEVICECHANGE.
// After claiming virtual pads via the hooks, poke that path so disconnected
// slots get GetState again and flip their connected flags.
static void NotifyGamepadRescan() {
    HWND hwnd = nullptr;
    HMODULE game = GetModuleHandleA("RivalsofAether.exe");
    if (game) {
        hwnd = SafeReadMainWindowHwnd(game);
    }
    if (!hwnd || !IsWindow(hwnd)) {
        Log("NotifyGamepadRescan: no valid game HWND");
        return;
    }
    if (PostMessageW(hwnd, WM_DEVICECHANGE, DBT_DEVNODES_CHANGED, 0)) {
        Log("Posted WM_DEVICECHANGE to trigger pad rediscovery");
    }
    else {
        Log("PostMessage WM_DEVICECHANGE failed");
    }
}

static void ClaimJoystickOverride(int joyIndex) {
    if (joyIndex < 0 || joyIndex >= MAX_JOYSTICKS) return;
    const bool wasClaimed = g_overrideJoystick[joyIndex];
    g_overrideJoystick[joyIndex] = true;
    if (!wasClaimed) {
        NotifyGamepadRescan();
    }
}

DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    DWORD result = OriginalXInputGetState(dwUserIndex, pState);

    if (g_logRealXInput && result == ERROR_SUCCESS && pState->Gamepad.wButtons != 0) {
        std::stringstream hexStream;
        hexStream << "0x" << std::hex << pState->Gamepad.wButtons;
        Log("XInput " + std::to_string(dwUserIndex) + " buttons=" + hexStream.str()
            + " LX=" + std::to_string(pState->Gamepad.sThumbLX)
            + " LY=" + std::to_string(pState->Gamepad.sThumbLY)
            + " LT=" + std::to_string(pState->Gamepad.bLeftTrigger)
            + " RT=" + std::to_string(pState->Gamepad.bRightTrigger));
    }

    if (dwUserIndex < MAX_JOYSTICKS && g_overrideJoystick[dwUserIndex]) {
        ZeroMemory(pState, sizeof(XINPUT_STATE));
        pState->Gamepad.wButtons = g_forcedButtons[dwUserIndex];
        pState->Gamepad.sThumbLX = g_forcedThumbLX[dwUserIndex];
        pState->Gamepad.sThumbLY = g_forcedThumbLY[dwUserIndex];
        pState->Gamepad.bLeftTrigger = g_forcedLeftTrigger[dwUserIndex];
        pState->Gamepad.bRightTrigger = g_forcedRightTrigger[dwUserIndex];
        return ERROR_SUCCESS;
    }
    return result;
}

DWORD WINAPI HookedXInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
    if (dwUserIndex < MAX_JOYSTICKS && g_overrideJoystick[dwUserIndex]) {
        ZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));
        pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
        pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        pCapabilities->Flags = 0;
        pCapabilities->Gamepad.wButtons = 0xFFFF;
        pCapabilities->Gamepad.bLeftTrigger = 0xFF;
        pCapabilities->Gamepad.bRightTrigger = 0xFF;
        pCapabilities->Gamepad.sThumbLX = -32768;
        pCapabilities->Gamepad.sThumbLY = -32768;
        pCapabilities->Gamepad.sThumbRX = -32768;
        pCapabilities->Gamepad.sThumbRY = -32768;
        return ERROR_SUCCESS;
    }
    return OriginalXInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
}

void SetupXInputHook() {
    HMODULE xinputDll = nullptr;
    const char* candidates[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };

    for (int attempt = 0; attempt < 100 && !xinputDll; ++attempt) {
        for (const char* name : candidates) {
            xinputDll = GetModuleHandleA(name);
            if (xinputDll) {
                Log(std::string("Found loaded: ") + name);
                break;
            }
        }
        if (!xinputDll) Sleep(100);
    }

    if (!xinputDll) {
        Log("Timed out waiting for XInput DLL to load");
        return;
    }

    void* stateTarget = GetProcAddress(xinputDll, "XInputGetState");
    if (!stateTarget) {
        Log("Could not resolve XInputGetState");
    }
    else if (MH_CreateHook(stateTarget, &HookedXInputGetState,
        (void**)&OriginalXInputGetState) != MH_OK) {
        Log("MH_CreateHook (XInputGetState) failed");
    }
    else if (MH_EnableHook(stateTarget) != MH_OK) {
        Log("MH_EnableHook (XInputGetState) failed");
    }
    else {
        Log("XInputGetState hook installed successfully");
    }

    void* capsTarget = GetProcAddress(xinputDll, "XInputGetCapabilities");
    if (!capsTarget) {
        Log("Could not resolve XInputGetCapabilities");
    }
    else if (MH_CreateHook(capsTarget, &HookedXInputGetCapabilities,
        (void**)&OriginalXInputGetCapabilities) != MH_OK) {
        Log("MH_CreateHook (XInputGetCapabilities) failed");
    }
    else if (MH_EnableHook(capsTarget) != MH_OK) {
        Log("MH_EnableHook (XInputGetCapabilities) failed");
    }
    else {
        Log("XInputGetCapabilities hook installed successfully");
    }

    Log("XInput passthrough by default; claim pads with set_joy_override");
}

// ============================================================
// Hook setup entry point
// ============================================================

void SetupInputHook() {
    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed"); return; }

    // ResolveGameWindow();
    // Focus hooks (GetFocus / GetForegroundWindow / GetActiveWindow) are
    // currently disabled; reinstate with the declarations above if needed.

    // --- XInput hooks, on their own thread since the DLL may not be
    //     loaded by the game yet at this point in startup ---
    CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        SetupXInputHook();
        return 0;
        }, NULL, 0, NULL);
}

// ============================================================
// Pipe server
// ============================================================

DWORD WINAPI MainThread(LPVOID param) {
    SetupInputHook();

    // Unique pipe name per process, so multiple simultaneously-injected
    // game instances don't collide on the same named pipe. Each instance
    // derives its own name from its own PID -- no coordination with the
    // injector needed.
    DWORD myPid = GetCurrentProcessId();
    std::string pipeName = "\\\\.\\pipe\\bridge_" + std::to_string(myPid);
    Log("Using pipe name: " + pipeName);

    HANDLE pipe = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 262144, 262144, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) {
        Log("Failed to create pipe, error: " + std::to_string(GetLastError()));
        return 1;
    }

    while (true) {
        BOOL connected = ConnectNamedPipe(pipe, NULL) ?
            TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected) {
            Log("Client connected.");
            char buffer[256];
            DWORD bytesRead;

            while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                std::string command(buffer);

                std::string response;
                if (command == "get_state") {
                    response = BuildGameStateJson();
                }
                else if (command.rfind("set_player_stock", 0) == 0) {
                    int player = 0;
                    int val = 0;
                    sscanf_s(command.c_str(), "set_player_stock %d %d", &player, &val);
                    response = std::to_string(WritePlayerStock(player, static_cast<double>(val)));
                }
                else if (command.rfind("set_player_percent", 0) == 0) {
                    int player = 0;
                    int val = 0;
                    sscanf_s(command.c_str(), "set_player_percent %d %d", &player, &val);
                    response = std::to_string(WritePlayerPercent(player, static_cast<double>(val)));
                }
                else if (command.rfind("get_player_choice", 0) == 0) {
                    int player = 0;
                    sscanf_s(command.c_str(), "get_player_choice %d", &player);
                    response = std::to_string(ReadPlayerChoice(player));
                }
                else if (command.rfind("set_player_choice", 0) == 0) {
                    int player = 0;
                    int val = 0;
                    sscanf_s(command.c_str(), "set_player_choice %d %d", &player, &val);
                    double written = WritePlayerChoice(player, static_cast<double>(val));
                    if (written == 0.0 && val != 0)
                        response = "error: css url not found for player " + std::to_string(player);
                    else
                        response = std::to_string(written);
                }
                else if (command.rfind("set_player_on", 0) == 0) {
                    int player = 0;
                    int val = 0;
                    sscanf_s(command.c_str(), "set_player_on %d %d", &player, &val);
                    response = std::to_string(WritePlayerOn(player, static_cast<double>(val)));
                }
                else if (command.rfind("set_key", 0) == 0) {
                    int vKey = 0; int down = 0;
                    sscanf_s(command.c_str(), "set_key %x %d", &vKey, &down);
                    if (vKey >= 0 && vKey < 256) {
                        g_overrideKeys[vKey] = true;
                        g_forcedKeyState[vKey] = (down != 0);
                        response = "ok";
                    }
                    else {
                        response = "error: invalid vKey";
                    }
                }
                else if (command.rfind("set_joy_override", 0) == 0) {
                    int joyIndex = -1;
                    int enabled = 0;
                    sscanf_s(command.c_str(), "set_joy_override %d %d", &joyIndex, &enabled);
                    if (joyIndex >= 0 && joyIndex < MAX_JOYSTICKS) {
                        if (enabled) {
                            ClaimJoystickOverride(joyIndex);
                        }
                        else {
                            g_overrideJoystick[joyIndex] = false;
                            ClearForcedJoystick(joyIndex);
                        }
                        response = "ok";
                    }
                    else {
                        response = "error: invalid joystick index";
                    }
                }
                else if (command.rfind("set_joy", 0) == 0) {
                    int joyIndex = -1;
                    char field[32] = { 0 };
                    int value = 0;
                    sscanf_s(command.c_str(), "set_joy %d %31s %d",
                        &joyIndex, field, (unsigned)_countof(field), &value);

                    if (joyIndex >= 0 && joyIndex < MAX_JOYSTICKS) {
                        ClaimJoystickOverride(joyIndex);
                        std::string f(field);

                        auto setButtonBit = [&](WORD bit) {
                            if (value) g_forcedButtons[joyIndex] |= bit;
                            else g_forcedButtons[joyIndex] &= ~bit;
                            };

                        if (f == "a") setButtonBit(XINPUT_GAMEPAD_A);
                        else if (f == "b") setButtonBit(XINPUT_GAMEPAD_B);
                        else if (f == "x") setButtonBit(XINPUT_GAMEPAD_X);
                        else if (f == "y") setButtonBit(XINPUT_GAMEPAD_Y);
                        else if (f == "lb") setButtonBit(XINPUT_GAMEPAD_LEFT_SHOULDER);
                        else if (f == "rb") setButtonBit(XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        else if (f == "back") setButtonBit(XINPUT_GAMEPAD_BACK);
                        else if (f == "start") setButtonBit(XINPUT_GAMEPAD_START);
                        else if (f == "lthumb") setButtonBit(XINPUT_GAMEPAD_LEFT_THUMB);
                        else if (f == "rthumb") setButtonBit(XINPUT_GAMEPAD_RIGHT_THUMB);
                        else if (f == "dup") setButtonBit(XINPUT_GAMEPAD_DPAD_UP);
                        else if (f == "ddown") setButtonBit(XINPUT_GAMEPAD_DPAD_DOWN);
                        else if (f == "dleft") setButtonBit(XINPUT_GAMEPAD_DPAD_LEFT);
                        else if (f == "dright") setButtonBit(XINPUT_GAMEPAD_DPAD_RIGHT);
                        else if (f == "ltrigger") g_forcedLeftTrigger[joyIndex] = (BYTE)value;
                        else if (f == "rtrigger") g_forcedRightTrigger[joyIndex] = (BYTE)value;
                        else if (f == "lx") g_forcedThumbLX[joyIndex] = (SHORT)value;
                        else if (f == "ly") g_forcedThumbLY[joyIndex] = (SHORT)value;
                        else response = "error: unknown field";

                        if (response.empty()) response = "ok";
                    }
                    else {
                        response = "error: invalid joystick index";
                    }
                }
                else if (command == "list_instances") {
                    // Walk the room's active-instance linked list.
                    // Run_Room VA 0x06066758 => RVA 0x05C66758.
                    uintptr_t moduleBase = (uintptr_t)GetModuleHandleA("RivalsofAether.exe");
                    uintptr_t runRoom = *(uintptr_t*)(moduleBase + 0x05C66758);
                    uintptr_t current = runRoom ? *(uintptr_t*)(runRoom + 0x80) : 0;

                    static bool slotsReady = false;
                    static int hspIndex = -1;
                    static int vspIndex = -1;
                    if (!slotsReady) {
                        hspIndex = FindCustomVarIndex(moduleBase, "hsp");
                        vspIndex = FindCustomVarIndex(moduleBase, "vsp");
                        slotsReady = true;
                    }

                    std::stringstream ss;
                    ss << "{\"run_room\":\"0x" << std::hex << runRoom
                        << "\",\"head\":\"0x" << current
                        << "\",\"hsp_index\":" << std::dec << hspIndex
                        << ",\"vsp_index\":" << vspIndex
                        << ",\"instances\":[";
                    bool first = true;
                    int count = 0;
                    const int MAX_INSTANCES = 500;

                    while (current != 0 && count < MAX_INSTANCES) {
                        uint32_t flags = *(uint32_t*)(current + 0x74);
                        if ((flags & 0x3) == 0) {
                            int32_t instanceId = *(int32_t*)(current + 0x78);
                            int32_t objectIndex = *(int32_t*)(current + 0x7C);
                            int32_t spriteIndex = *(int32_t*)(current + 0x80);
                            float x = *(float*)(current + 0xA0);
                            float y = *(float*)(current + 0xA4);
                            const char* name = GetInstanceObjectName(current);

                            if (!first) ss << ",";
                            first = false;
                            ss << "{\"addr\":\"0x" << std::hex << current << std::dec << "\""
                                << ",\"id\":" << instanceId
                                << ",\"object_index\":" << objectIndex
                                << ",\"name\":";
                            AppendJsonString(ss, name);
                            ss << ",\"sprite_index\":" << spriteIndex
                                << ",\"x\":" << x
                                << ",\"y\":" << y;

                            double hsp = 0, vsp = 0;
                            if (ReadInstCustomReal(current, hspIndex, &hsp))
                                ss << ",\"hsp\":" << hsp;
                            if (ReadInstCustomReal(current, vspIndex, &vsp))
                                ss << ",\"vsp\":" << vsp;
                            ss << "}";
                        }
                        current = *(uintptr_t*)(current + 0x130);
                        count++;
                    }
                    ss << "]}";
                    response = ss.str();
                }
                else if (command == "dump_css") {
                    uintptr_t moduleBase = (uintptr_t)GetModuleHandleA("RivalsofAether.exe");
                    uintptr_t runRoom = *(uintptr_t*)(moduleBase + 0x05C66758);
                    uintptr_t current = runRoom ? *(uintptr_t*)(runRoom + 0x80) : 0;

                    std::stringstream ss;
                    ss << "{\"boxes\":[";
                    bool first = true;
                    int scanned = 0;
                    const int MAX_INSTANCES = 500;
                    while (current != 0 && scanned < MAX_INSTANCES) {
                        uint32_t flags = *(uint32_t*)(current + 0x74);
                        int32_t objectIndex = *(int32_t*)(current + 0x7C);
                        if ((flags & 0x3) == 0 && objectIndex == 219) {
                            if (!first) ss << ",";
                            first = false;
                            ss << "{\"addr\":\"0x" << std::hex << current << std::dec << "\""
                                << ",\"id\":" << *(int32_t*)(current + 0x78)
                                << ",\"x\":" << *(float*)(current + 0xA0)
                                << ",\"y\":" << *(float*)(current + 0xA4)
                                << ",";
                            AppendInstanceVarsJson(ss, moduleBase, current);
                            ss << "}";
                        }
                        current = *(uintptr_t*)(current + 0x130);
                        scanned++;
                    }
                    ss << "]}";
                    response = ss.str();
                }
                else if (command.rfind("dump_vars", 0) == 0) {
                    // First live instance of object_index (default 3 = oPlayer).
                    int objectIndex = 382;
                    sscanf_s(command.c_str(), "dump_vars %d", &objectIndex);

                    uintptr_t moduleBase = (uintptr_t)GetModuleHandleA("RivalsofAether.exe");
                    uintptr_t runRoom = *(uintptr_t*)(moduleBase + 0x05C66758);
                    uintptr_t current = runRoom ? *(uintptr_t*)(runRoom + 0x80) : 0;

                    uintptr_t found = 0;
                    int scanned = 0;
                    const int MAX_INSTANCES = 500;
                    while (current != 0 && scanned < MAX_INSTANCES) {
                        uint32_t flags = *(uint32_t*)(current + 0x74);
                        if ((flags & 0x3) == 0 &&
                            *(int32_t*)(current + 0x7C) == objectIndex) {
                            found = current;
                            break;
                        }
                        current = *(uintptr_t*)(current + 0x130);
                        scanned++;
                    }

                    std::stringstream ss;
                    ss << "{\"object_index\":" << objectIndex
                        << ",\"var_base\":" << kGmlInstanceVarBase
                        << ",\"addr\":\"0x" << std::hex << found << std::dec << "\"";
                    if (found) {
                        ss << ",\"id\":" << *(int32_t*)(found + 0x78)
                            << ",\"name\":";
                        AppendJsonString(ss, GetInstanceObjectName(found));
                        ss << ",";
                        AppendInstanceVarsJson(ss, moduleBase, found);
                    }
                    ss << "}";
                    response = ss.str();
                }
                else {
                    response = "unknown command";
                }

                WriteFile(pipe, response.c_str(), (DWORD)response.size(), NULL, NULL);
                if (command.rfind("dump_vars", 0) == 0 || command == "dump_css")
                    Log(command + " bytes=" + std::to_string(response.size()));
                else
                    Log(response);
            }

            Log("Client disconnected.");
        }

        DisconnectNamedPipe(pipe);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}