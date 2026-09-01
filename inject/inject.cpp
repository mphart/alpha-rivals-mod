// injector.cpp
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

DWORD GetProcessIdByName(const std::wstring& processName) {
    PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (processName == entry.szExeFile) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

bool InjectDll(DWORD pid, const std::string& dllPath) {
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        std::cerr << "OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }

    // Allocate space in the target process for the DLL path string
    size_t pathSize = dllPath.size() + 1;
    LPVOID remotePath = VirtualAllocEx(hProcess, NULL, pathSize,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        std::cerr << "VirtualAllocEx failed: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    // Write the DLL path into that memory
    if (!WriteProcessMemory(hProcess, remotePath, dllPath.c_str(), pathSize, NULL)) {
        std::cerr << "WriteProcessMemory failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Get the address of LoadLibraryA in kernel32.dll (same address across
    // processes on the same architecture, since kernel32 is loaded at a
    // consistent base for a given Windows session)
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPTHREAD_START_ROUTINE loadLibraryAddr =
        (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryA");

    // Create a remote thread in the target process that calls
    // LoadLibraryA(remotePath) — this is the actual injection moment
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        loadLibraryAddr, remotePath, 0, NULL);
    if (!hThread) {
        std::cerr << "CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode;
    GetExitCodeThread(hThread, &exitCode);
    if (exitCode == 0) {
        std::cout << "LoadLibrary failed inside target process (module not loaded).\n";
    }
    else {
        std::cout << "LoadLibrary succeeded, module base: 0x" << std::hex << exitCode << "\n";
    }

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return true;
}

int main() {
    DWORD pid = GetProcessIdByName(L"RivalsofAether.exe");
    if (pid == 0) {
        std::cerr << "Process not found. Is the game running?\n";
        return 1;
    }
    std::cout << "Found process, PID: " << pid << "\n";

    // Use the FULL absolute path to your compiled payload.dll
    std::string dllPath = "C:\\Users\\mhart\\source\\repos\\dll-injection-proof\\Debug\\alpha-rivals-mod.dll";

    if (InjectDll(pid, dllPath)) {
        std::cout << "Injection succeeded.\n";
    }
    else {
        std::cout << "Injection failed.\n";
    }
    return 0;
}