#include "config.h"

#include <cmath>
#include <cwchar>

namespace {

ModConfig g_config{};
std::wstring g_config_path;

bool ReadBool(const wchar_t* section, const wchar_t* key, const bool default_value, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, default_value ? 1 : 0, path.c_str()) != 0;
}

DWORD ReadDword(const wchar_t* section, const wchar_t* key, const DWORD default_value, const std::wstring& path) {
    return static_cast<DWORD>(GetPrivateProfileIntW(section, key, static_cast<int>(default_value), path.c_str()));
}

double ReadDouble(const wchar_t* section, const wchar_t* key, const double default_value, const std::wstring& path) {
    wchar_t buffer[64]{};
    const auto default_text = std::to_wstring(default_value);
    GetPrivateProfileStringW(section, key, default_text.c_str(), buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());

    wchar_t* end = nullptr;
    const double parsed = std::wcstod(buffer, &end);
    if (end == buffer || !std::isfinite(parsed) || parsed < 0.0) {
        return default_value;
    }

    return parsed;
}

}  // namespace

bool LoadConfig(const std::wstring& config_path) {
    ModConfig next{};

    next.general.enabled = ReadBool(L"General", L"Enabled", next.general.enabled, config_path);
    next.general.log_enabled = ReadBool(L"General", L"LogEnabled", next.general.log_enabled, config_path);
    next.general.init_delay_ms = ReadDword(L"General", L"InitDelayMs", next.general.init_delay_ms, config_path);
    next.general.stale_component_ms = ReadDword(L"General", L"StaleComponentMs", next.general.stale_component_ms, config_path);
    next.general.relock_idle_ms = ReadDword(L"General", L"RelockIdleMs", next.general.relock_idle_ms, config_path);

    next.damage.multiplier = ReadDouble(L"Damage", L"Multiplier", next.damage.multiplier, config_path);
    next.items.gain_multiplier = ReadDouble(L"Items", L"GainMultiplier", next.items.gain_multiplier, config_path);
    next.position_control.enabled =
        ReadBool(L"Position Control(Height)", L"Enable", next.position_control.enabled, config_path);
    next.position_control.key =
        static_cast<int>(ReadDword(L"Position Control(Height)", L"Key", static_cast<DWORD>(next.position_control.key), config_path));
    next.position_control.amplitude = static_cast<float>(
        ReadDouble(L"Position Control(Height)", L"Amplitude", next.position_control.amplitude, config_path));

    next.health.consumption_multiplier = ReadDouble(L"Health", L"ConsumptionMultiplier", next.health.consumption_multiplier, config_path);
    next.health.heal_multiplier = ReadDouble(L"Health", L"HealMultiplier", next.health.heal_multiplier, config_path);

    next.stamina.consumption_multiplier = ReadDouble(L"Stamina", L"ConsumptionMultiplier", next.stamina.consumption_multiplier, config_path);
    next.stamina.heal_multiplier = ReadDouble(L"Stamina", L"HealMultiplier", next.stamina.heal_multiplier, config_path);

    next.spirit.consumption_multiplier = ReadDouble(L"Spirit", L"ConsumptionMultiplier", next.spirit.consumption_multiplier, config_path);
    next.spirit.heal_multiplier = ReadDouble(L"Spirit", L"HealMultiplier", next.spirit.heal_multiplier, config_path);

    if (next.general.stale_component_ms == 0) {
        next.general.stale_component_ms = 60000;
    }

    if (next.general.relock_idle_ms == 0) {
        next.general.relock_idle_ms = 10000;
    }

    if (next.position_control.key <= 0) {
        next.position_control.key = VK_F6;
    }

    if (!std::isfinite(next.position_control.amplitude) || next.position_control.amplitude < 0.0f) {
        next.position_control.amplitude = 0.1f;
    }

    g_config = next;
    g_config_path = config_path;
    return true;
}

const ModConfig& GetConfig() {
    return g_config;
}

const std::wstring& GetLoadedConfigPath() {
    return g_config_path;
}
