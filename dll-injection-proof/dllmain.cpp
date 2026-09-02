// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "state.h"
#include "util.h"
#include "minhook.h"
#include <Xinput.h>   // XINPUT_STATE, XINPUT_CAPABILITIES, button bit constants
#include <sstream>    // for hex formatting in log lines

// ============================================================
// Game window resolution (needed for focus-hook spoofing below)
// ============================================================

HWND g_gameWindowHandle = nullptr;

void ResolveGameWindow() {
    DWORD myPid = GetCurrentProcessId();
    g_gameWindowHandle = nullptr;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId() && IsWindowVisible(hwnd)) {
            g_gameWindowHandle = hwnd;
            return FALSE; // stop enumerating, found it
        }
        return TRUE;
        }, 0);

    Log("Resolved game window handle: " + std::to_string((uintptr_t)g_gameWindowHandle));
}

// ============================================================
// Focus hooks (GetFocus / GetForegroundWindow / GetActiveWindow)
// ============================================================
// RoA likely only polls keyboard state when it believes it has focus.
// These hooks make the game always believe its own window is focused,
// regardless of which window Windows actually considers active -- this
// lets input injection work even while the game runs in the background.

typedef HWND(WINAPI* GetFocus_t)();
GetFocus_t OriginalGetFocus = nullptr;

HWND WINAPI HookedGetFocus() {
    if (g_gameWindowHandle) return g_gameWindowHandle;
    return OriginalGetFocus();
}

typedef HWND(WINAPI* GetForegroundWindow_t)();
GetForegroundWindow_t OriginalGetForegroundWindow = nullptr;

HWND WINAPI HookedGetForegroundWindow() {
    if (g_gameWindowHandle) return g_gameWindowHandle;
    return OriginalGetForegroundWindow();
}

typedef HWND(WINAPI* GetActiveWindow_t)();
GetActiveWindow_t OriginalGetActiveWindow = nullptr;

HWND WINAPI HookedGetActiveWindow() {
    if (g_gameWindowHandle) return g_gameWindowHandle;
    return OriginalGetActiveWindow();
}

// ============================================================
// Keyboard hooks (GetAsyncKeyState + GetKeyState)
// ============================================================

typedef SHORT(WINAPI* GetAsyncKeyState_t)(int vKey);
GetAsyncKeyState_t OriginalGetAsyncKeyState = nullptr;

typedef SHORT(WINAPI* GetKeyState_t)(int vKey);
GetKeyState_t OriginalGetKeyState = nullptr;

volatile bool g_overrideKeys[256] = { false };
volatile bool g_forcedKeyState[256] = { false };

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
    if (vKey >= 0 && vKey < 256 && g_overrideKeys[vKey]) {
        return g_forcedKeyState[vKey] ? (SHORT)0x8000 : 0;
    }
    return OriginalGetAsyncKeyState(vKey);
}

SHORT WINAPI HookedGetKeyState(int vKey) {
    if (g_overrideKeys[vKey] || (OriginalGetKeyState(vKey) & 0x8000)) {
        Log("GetKeyState called: vKey=0x" + std::to_string(vKey) +
            " override=" + std::to_string(g_overrideKeys[vKey]));
    }

    if (vKey >= 0 && vKey < 256 && g_overrideKeys[vKey]) {
        return g_forcedKeyState[vKey] ? (SHORT)0x8000 : 0;
    }
    return OriginalGetKeyState(vKey);
}

// ============================================================
// XInput hooks (XInputGetState + XInputGetCapabilities)
// ============================================================

typedef DWORD(WINAPI* XInputGetState_t)(DWORD dwUserIndex, XINPUT_STATE* pState);
XInputGetState_t OriginalXInputGetState = nullptr;

typedef DWORD(WINAPI* XInputGetCapabilities_t)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
XInputGetCapabilities_t OriginalXInputGetCapabilities = nullptr;

const int NUM_CONTROLLED_JOYSTICKS = 2;
const int MAX_JOYSTICKS = 4;

volatile bool  g_overrideJoystick[MAX_JOYSTICKS] = { false };
volatile WORD  g_forcedButtons[MAX_JOYSTICKS] = { 0 };
volatile SHORT g_forcedThumbLX[MAX_JOYSTICKS] = { 0 };
volatile SHORT g_forcedThumbLY[MAX_JOYSTICKS] = { 0 };
volatile BYTE  g_forcedLeftTrigger[MAX_JOYSTICKS] = { 0 };
volatile BYTE  g_forcedRightTrigger[MAX_JOYSTICKS] = { 0 };

bool g_logRealXInput = true;

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

    for (int i = 0; i < NUM_CONTROLLED_JOYSTICKS; ++i) {
        g_overrideJoystick[i] = true;
    }
    Log("Marked joystick indices 0.." + std::to_string(NUM_CONTROLLED_JOYSTICKS - 1) + " as present");
}

// ============================================================
// Hook setup entry point
// ============================================================

void SetupInputHook() {
    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed"); return; }

    ResolveGameWindow();

    HMODULE user32 = GetModuleHandleA("user32.dll");

    // --- GetAsyncKeyState ---
    void* asyncKeyTarget = GetProcAddress(user32, "GetAsyncKeyState");
    if (MH_CreateHook(asyncKeyTarget, &HookedGetAsyncKeyState,
        (void**)&OriginalGetAsyncKeyState) != MH_OK) {
        Log("MH_CreateHook (GetAsyncKeyState) failed");
    }
    else if (MH_EnableHook(asyncKeyTarget) != MH_OK) {
        Log("MH_EnableHook (GetAsyncKeyState) failed");
    }
    else {
        Log("GetAsyncKeyState hook installed successfully");
    }

    // --- GetKeyState (confirmed to be the function RoA actually polls) ---
    void* keyStateTarget = GetProcAddress(user32, "GetKeyState");
    if (MH_CreateHook(keyStateTarget, &HookedGetKeyState,
        (void**)&OriginalGetKeyState) != MH_OK) {
        Log("MH_CreateHook (GetKeyState) failed");
    }
    else if (MH_EnableHook(keyStateTarget) != MH_OK) {
        Log("MH_EnableHook (GetKeyState) failed");
    }
    else {
        Log("GetKeyState hook installed successfully");
    }

    // --- GetFocus ---
    void* focusTarget = GetProcAddress(user32, "GetFocus");
    if (MH_CreateHook(focusTarget, &HookedGetFocus,
        (void**)&OriginalGetFocus) != MH_OK) {
        Log("MH_CreateHook (GetFocus) failed");
    }
    else if (MH_EnableHook(focusTarget) != MH_OK) {
        Log("MH_EnableHook (GetFocus) failed");
    }
    else {
        Log("GetFocus hook installed successfully");
    }

    // --- GetForegroundWindow ---
    void* fgTarget = GetProcAddress(user32, "GetForegroundWindow");
    if (MH_CreateHook(fgTarget, &HookedGetForegroundWindow,
        (void**)&OriginalGetForegroundWindow) != MH_OK) {
        Log("MH_CreateHook (GetForegroundWindow) failed");
    }
    else if (MH_EnableHook(fgTarget) != MH_OK) {
        Log("MH_EnableHook (GetForegroundWindow) failed");
    }
    else {
        Log("GetForegroundWindow hook installed successfully");
    }

    // --- GetActiveWindow ---
    void* activeTarget = GetProcAddress(user32, "GetActiveWindow");
    if (MH_CreateHook(activeTarget, &HookedGetActiveWindow,
        (void**)&OriginalGetActiveWindow) != MH_OK) {
        Log("MH_CreateHook (GetActiveWindow) failed");
    }
    else if (MH_EnableHook(activeTarget) != MH_OK) {
        Log("MH_EnableHook (GetActiveWindow) failed");
    }
    else {
        Log("GetActiveWindow hook installed successfully");
    }

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

    HANDLE pipe = CreateNamedPipeA(
        "\\\\.\\pipe\\bridge",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
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
                else if (command.rfind("set_joy", 0) == 0) {
                    int joyIndex = -1;
                    char field[32] = { 0 };
                    int value = 0;
                    sscanf_s(command.c_str(), "set_joy %d %31s %d",
                        &joyIndex, field, (unsigned)_countof(field), &value);

                    if (joyIndex >= 0 && joyIndex < MAX_JOYSTICKS) {
                        g_overrideJoystick[joyIndex] = true;
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
                else if (command == "get_percent") {
                    double percent1, percent2, percent3, percent4;
                    percent1 = ReadPlayerPercent(0);
                    percent2 = ReadPlayerPercent(1);
                    percent3 = ReadPlayerPercent(2);
                    percent4 = ReadPlayerPercent(3);
                    response = std::to_string(percent1) + " " + std::to_string(percent2) + " " + std::to_string(percent3) + " " + std::to_string(percent4);
                }
                else if (command == "get_stock") {
                    double stock1, stock2, stock3, stock4;
                    stock1 = ReadPlayerStock(0);
                    stock2 = ReadPlayerStock(1);
                    stock3 = ReadPlayerStock(2);
                    stock4 = ReadPlayerStock(3);
                    response = std::to_string(stock1) + " " + std::to_string(stock2) + " " + std::to_string(stock3) + " " + std::to_string(stock4);
                }
                else {
                    response = "unknown command";
                }

                WriteFile(pipe, response.c_str(), (DWORD)response.size(), NULL, NULL);
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