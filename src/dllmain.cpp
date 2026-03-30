#include "config.h"
#include "hooks.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"

#include <Windows.h>

#include <filesystem>
#include <string>

namespace {

HMODULE g_module = nullptr;

std::wstring GetSiblingPath(const wchar_t* file_name) {
    std::wstring module_path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(g_module, module_path.data(), static_cast<DWORD>(module_path.size()));
    module_path.resize(length);

    auto path = std::filesystem::path(module_path);
    path.replace_filename(file_name);
    return path.wstring();
}

DWORD WINAPI InitializeMod(LPVOID) {
    const auto config_path = GetSiblingPath(L"player-status-modifier.ini");
    const auto log_path = GetSiblingPath(L"player-status-modifier.log");

    LoadConfig(config_path);
    InitializeLogger(log_path, GetConfig().general.log_enabled);

    Log("dllmain: initialization started");
    Log("dllmain: config path = %ls", config_path.c_str());

    const auto init_delay = GetConfig().general.init_delay_ms;
    if (init_delay > 0) {
        Sleep(init_delay);
    }

    ResetRuntimeState();

    if (!InstallHooks()) {
        Log("dllmain: hook installation failed");
        return 0;
    }

    if (!InitializePositionControl()) {
        Log("dllmain: position control initialization failed");
        RemoveHooks();
        return 0;
    }

    Log("dllmain: initialization finished");
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, const DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);

        HANDLE thread = CreateThread(nullptr, 0, InitializeMod, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        ShutdownPositionControl();
        RemoveHooks();
        ShutdownLogger();
    }

    return TRUE;
}
