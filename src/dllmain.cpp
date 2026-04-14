#include "config.h"
#include "config_watcher.h"
#include "hooks.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"

#include <Windows.h>

#include <cwchar>
#include <filesystem>
#include <string>

namespace {

HMODULE g_module = nullptr;
constexpr wchar_t kTargetProcessName[] = L"CrimsonDesert.exe";

std::wstring GetSiblingPath(const wchar_t* file_name) {
    std::wstring module_path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(g_module, module_path.data(), static_cast<DWORD>(module_path.size()));
    module_path.resize(length);

    auto path = std::filesystem::path(module_path);
    path.replace_filename(file_name);
    return path.wstring();
}

std::wstring GetHostProcessPath() {
    std::wstring process_path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, process_path.data(), static_cast<DWORD>(process_path.size()));
    process_path.resize(length);
    return process_path;
}

bool IsTargetHostProcess(const std::wstring& process_path) {
    if (process_path.empty()) {
        return false;
    }

    const auto file_name = std::filesystem::path(process_path).filename().wstring();
    return _wcsicmp(file_name.c_str(), kTargetProcessName) == 0;
}

DWORD WINAPI InitializeMod(LPVOID) {
    const auto host_process_path = GetHostProcessPath();
    if (!IsTargetHostProcess(host_process_path)) {
        return 0;
    }

    const auto config_path = GetSiblingPath(L"player-status-modifier.ini");
    const auto log_path = GetSiblingPath(L"player-status-modifier.log");

    LoadConfig(config_path);
    InitializeLogger(log_path, GetConfig().general.log_enabled);

    Log("dllmain: initialization started");
    Log("dllmain: host process = %ls", host_process_path.c_str());
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

        if (!StartMountResolver()) {
            Log("dllmain: mount resolver failed to start");
            RemoveHooks();
            return 0;
        }

    ModConfig config = GetConfig();
    if ((config.position_control.enabled || config.position_control.horizontal_enabled) && !IsPositionHeightHookInstalled()) {
        config.position_control.enabled = false;
        config.position_control.horizontal_enabled = false;
        SetConfigSnapshot(config_path, config);
        Log("dllmain: position control requested but position hook is unavailable; disabling position control");
    }

    if (!InitializePositionControl()) {
        Log("dllmain: position control initialization failed");
        RemoveHooks();
        return 0;
    }

    if (!StartConfigWatcher()) {
        Log("dllmain: config watcher failed to start");
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
        StopMountResolver();
        StopConfigWatcher();
        ShutdownPositionControl();
        RemoveHooks();
        ShutdownLogger();
    }

    return TRUE;
}
