#include "config_watcher.h"

#include "config.h"
#include "hooks.h"
#include "logger.h"
#include "position_control.h"

#include <Windows.h>

#include <atomic>
#include <thread>

namespace {

constexpr DWORD kConfigWatchPollMs = 1000;
constexpr DWORD kConfigReloadSettleMs = 200;

std::atomic<bool> g_config_watcher_running{false};
std::thread g_config_watcher_thread{};

bool TryGetLastWriteTimestamp(const std::wstring& path, ULONGLONG* const timestamp) {
    if (timestamp == nullptr) {
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }

    ULARGE_INTEGER last_write{};
    last_write.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    last_write.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    *timestamp = last_write.QuadPart;
    return true;
}

void ApplyLoggerReload(const bool previous_enabled, const bool current_enabled) {
    if (previous_enabled && !current_enabled) {
        Log("config-watcher: config reloaded, disabling logger");
        SetLoggerEnabled(false);
        return;
    }

    SetLoggerEnabled(current_enabled);
    if (current_enabled) {
        Log("config-watcher: config reloaded");
    }
}

void ConfigWatcherLoop() {
    const std::wstring config_path = GetLoadedConfigPath();
    ULONGLONG last_write_timestamp = 0;
    TryGetLastWriteTimestamp(config_path, &last_write_timestamp);

    while (g_config_watcher_running.load(std::memory_order_acquire)) {
        Sleep(kConfigWatchPollMs);
        if (!g_config_watcher_running.load(std::memory_order_acquire)) {
            break;
        }

        ULONGLONG observed_timestamp = 0;
        if (!TryGetLastWriteTimestamp(config_path, &observed_timestamp) || observed_timestamp == last_write_timestamp) {
            continue;
        }

        Sleep(kConfigReloadSettleMs);

        ULONGLONG settled_timestamp = 0;
        if (!TryGetLastWriteTimestamp(config_path, &settled_timestamp)) {
            continue;
        }

        last_write_timestamp = settled_timestamp;

        const ModConfig previous = GetConfig();
        ModConfig next{};
        if (!ReadConfigSnapshot(config_path, &next)) {
            if (previous.general.log_enabled) {
                Log("config-watcher: failed to read updated config");
            }
            continue;
        }

        bool disabled_position_control = false;
        if ((next.position_control.enabled || next.position_control.horizontal_enabled) && !IsPositionHeightHookInstalled()) {
            next.position_control.enabled = false;
            next.position_control.horizontal_enabled = false;
            disabled_position_control = true;
        }

        if (next == previous) {
            if (disabled_position_control && previous.general.log_enabled) {
                Log("config-watcher: position control requested but position hook is unavailable");
            }
            continue;
        }

        if (!ApplyPositionControlConfig(previous.position_control, next.position_control)) {
            if (previous.general.log_enabled) {
                Log("config-watcher: failed to apply position control changes");
            }
            continue;
        }

        SetConfigSnapshot(config_path, next);
        ApplyLoggerReload(previous.general.log_enabled, next.general.log_enabled);

        if (disabled_position_control && next.general.log_enabled) {
            Log("config-watcher: position control requested but position hook is unavailable");
        }
    }
}

}  // namespace

bool StartConfigWatcher() {
    if (g_config_watcher_running.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    const std::wstring config_path = GetLoadedConfigPath();
    if (config_path.empty()) {
        g_config_watcher_running.store(false, std::memory_order_release);
        return false;
    }

    g_config_watcher_thread = std::thread(ConfigWatcherLoop);
    return true;
}

void StopConfigWatcher() {
    if (!g_config_watcher_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (g_config_watcher_thread.joinable()) {
        g_config_watcher_thread.join();
    }
}
