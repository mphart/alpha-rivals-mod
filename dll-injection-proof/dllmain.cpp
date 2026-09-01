// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "state.h"
#include "util.h"
#include "minhook.h"

typedef SHORT(WINAPI* GetAsyncKeyState_t)(int vKey);
GetAsyncKeyState_t OriginalGetAsyncKeyState = nullptr;

// Shared state your pipe handler writes to
volatile bool g_overrideKeys[256] = { false };
volatile bool g_forcedKeyState[256] = { false };

SHORT WINAPI HookedGetAsyncKeyState(int vKey) {
    if (vKey >= 0 && vKey < 256 && g_overrideKeys[vKey]) {
        // High bit set (0x8000) means "currently held", matching real API semantics
        return g_forcedKeyState[vKey] ? (SHORT)0x8000 : 0;
    }
    return OriginalGetAsyncKeyState(vKey);
}

void SetupInputHook() {
    if (MH_Initialize() != MH_OK) { Log("MH_Initialize failed"); return; }

    HMODULE user32 = GetModuleHandleA("user32.dll");
    void* target = GetProcAddress(user32, "GetAsyncKeyState");

    if (MH_CreateHook(target, &HookedGetAsyncKeyState,
        (void**)&OriginalGetAsyncKeyState) != MH_OK) {
        Log("MH_CreateHook failed");
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("MH_EnableHook failed");
        return;
    }
    Log("Input hook installed successfully");
}

DWORD WINAPI MainThread(LPVOID param) {
    // setup the input hook
    SetupInputHook();

    // open a new shared file to communicate with the python script
    HANDLE pipe = CreateNamedPipeA(
        "\\\\.\\pipe\\bridge",
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 4096, 4096, 0, NULL);
    // ensure the file was created
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

            // read the last line of the file repeatedly for commands
            while (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                std::string command(buffer);

                std::string response;
                if (command == "get_state") {
                    response = BuildGameStateJson();
                }
                else if (command.rfind("set_key", 0) == 0) {
                    // parse something like "set_key 0x41 1" (vKey, down/up)
                    int vKey; int down;
                    sscanf_s(command.c_str(), "set_key %x %d", &vKey, &down);
                    if (vKey >= 0 && vKey < 256) {
                        g_overrideKeys[vKey] = true;
                        g_forcedKeyState[vKey] = (down != 0);
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