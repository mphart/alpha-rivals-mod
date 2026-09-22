#pragma once 

#include "framework.h"
#include <dbt.h>
#include <Xinput.h>
#include <windows.h>
#include "minhook.h"

// Runner stores the main HWND here (VA 0x060662F4).
static const uintptr_t kMainWindowRva = 0x05C662F4;

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