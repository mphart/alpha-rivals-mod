// injector.cpp
#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

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

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // LoadLibraryA's return value is the thread exit code. 0 means the DLL
    // path was missing, the image is the wrong architecture, or DllMain failed.
    if (exitCode == 0) {
        std::cerr << "LoadLibrary failed inside target process (module not loaded).\n";
        return false;
    }

    std::cout << "LoadLibrary succeeded, module base: 0x" << std::hex << exitCode << std::dec << "\n";
    return true;
}

static bool FileExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string NormalizePath(const std::string& path) {
    char full[MAX_PATH] = {};
    if (!GetFullPathNameA(path.c_str(), MAX_PATH, full, NULL))
        return path;
    return full;
}

static std::string ExeDirectory() {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string dir(exePath);
    size_t slash = dir.find_last_of("\\/");
    if (slash != std::string::npos)
        dir.resize(slash);
    return dir;
}

// Solution Debug|Win32 writes both the injector and the payload to
// <repo>\Debug\. The nested dll-injection-proof\Debug folder is IntDir
// (objs / .recipe), not the linked DLL.
static std::string FindDllPath() {
    const std::string exeDir = ExeDirectory();
    const std::string candidates[] = {
        exeDir + "\\alpha-rivals-mod.dll",
        exeDir + "\\..\\Debug\\alpha-rivals-mod.dll",
        exeDir + "\\..\\dll-injection-proof\\Debug\\alpha-rivals-mod.dll",
    };
    for (const std::string& raw : candidates) {
        std::string full = NormalizePath(raw);
        if (FileExists(full))
            return full;
    }
    return {};
}

std::vector<DWORD> GetAllProcessIdsByName(const std::wstring& processName) {
    std::vector<DWORD> pids;
    PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return pids;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (processName == entry.szExeFile) {
                pids.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}

int main() {
    std::vector<DWORD> pids = GetAllProcessIdsByName(L"RivalsofAether.exe");
    if (pids.empty()) {
        std::cerr << "No RivalsofAether.exe processes found\n";
        return 1;
    }
    std::cout << "Found " << pids.size() << " process(es).\n";

    std::string dllPath = FindDllPath();
    if (dllPath.empty()) {
        std::cerr << "alpha-rivals-mod.dll not found. Build alpha-rivals-mod "
                     "(Debug|Win32) and run this injector from the same Debug folder.\n"
                  << "Looked next to: " << ExeDirectory() << "\n";
        return 1;
    }
    std::cout << "DLL: " << dllPath << "\n";

    int failures = 0;
    for (DWORD pid : pids) {
        std::cout << "Injecting into PID: " << pid << "\n";
        if (InjectDll(pid, dllPath)) {
            std::cout << "  Injection succeeded.\n";
        }
        else {
            std::cout << "  Injection failed.\n";
            failures++;
        }
    }
    return failures ? 1 : 0;
}
