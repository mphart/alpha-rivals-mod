#include "pch.h"
#include "util.h"

void Log(const std::string& msg) {
    static const std::filesystem::path logPath = [] {
        char modulePath[MAX_PATH]{};
        HMODULE self{};
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&Log),
            &self);
        GetModuleFileNameA(self, modulePath, MAX_PATH);
        return std::filesystem::path(modulePath).parent_path() / "roa_mod_log.txt";
    }();

    std::ofstream log(logPath, std::ios::app);
    if (!log.is_open()) {
        // Can't even log the failure to log... but at least this
        // makes the failure mode explicit if you step through in a debugger
        return;
    }
    log << msg << std::endl;
}