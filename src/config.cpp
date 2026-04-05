#include "config.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <mutex>

namespace {

std::atomic<std::shared_ptr<const ModConfig>> g_config{std::make_shared<const ModConfig>(ModConfig{})};
std::mutex g_config_path_mutex;
std::wstring g_config_path;

bool ReadBool(const wchar_t* section, const wchar_t* key, const bool default_value, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, default_value ? 1 : 0, path.c_str()) != 0;
}

bool HasIniKey(const wchar_t* section, const wchar_t* key, const std::wstring& path) {
    wchar_t buffer[2]{};
    return GetPrivateProfileStringW(
               section,
               key,
               L"",
               buffer,
               static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
               path.c_str()) > 0;
}

DWORD ReadDword(const wchar_t* section, const wchar_t* key, const DWORD default_value, const std::wstring& path) {
    return static_cast<DWORD>(GetPrivateProfileIntW(section, key, static_cast<int>(default_value), path.c_str()));
}

double ReadDoubleRaw(const wchar_t* section, const wchar_t* key, const double default_value, const std::wstring& path) {
    wchar_t buffer[64]{};
    const auto default_text = std::to_wstring(default_value);
    GetPrivateProfileStringW(section, key, default_text.c_str(), buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])), path.c_str());

    wchar_t* end = nullptr;
    const double parsed = std::wcstod(buffer, &end);
    if (end == buffer || !std::isfinite(parsed)) {
        return default_value;
    }

    return parsed;
}

double ReadDouble(const wchar_t* section, const wchar_t* key, const double default_value, const std::wstring& path) {
    const double parsed = ReadDoubleRaw(section, key, default_value, path);
    if (parsed < 0.0) {
        return default_value;
    }

    return parsed;
}

double ClampDouble(const double value, const double minimum, const double maximum, const double fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }

    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}

void SanitizeConfig(ModConfig* const next) {
    if (next == nullptr) {
        return;
    }

    if (next->general.stale_component_ms == 0) {
        next->general.stale_component_ms = 60000;
    }

    if (next->general.relock_idle_ms == 0) {
        next->general.relock_idle_ms = 10000;
    }

    if (next->position_control.key <= 0) {
        next->position_control.key = VK_F6;
    }

    if (!std::isfinite(next->position_control.amplitude) || next->position_control.amplitude < 0.0f) {
        next->position_control.amplitude = 0.1f;
    }

    if (next->position_control.horizontal_key <= 0) {
        next->position_control.horizontal_key = VK_F7;
    }

    if (!std::isfinite(next->position_control.horizontal_multiplier) || next->position_control.horizontal_multiplier < 0.0f) {
        next->position_control.horizontal_multiplier = 1.5f;
    }

    next->durability.consumption_chance =
        ClampDouble(next->durability.consumption_chance, 0.0, 100.0, 100.0);

    if (next->mount.lock_value <= 0) {
        next->mount.lock_value = 9999999;
    }
}

}  // namespace

bool ReadConfigSnapshot(const std::wstring& config_path, ModConfig* const config) {
    if (config == nullptr) {
        return false;
    }

    ModConfig next{};

    next.general.enabled = ReadBool(L"General", L"Enabled", next.general.enabled, config_path);
    next.general.log_enabled = ReadBool(L"General", L"LogEnabled", next.general.log_enabled, config_path);
    next.general.init_delay_ms = ReadDword(L"General", L"InitDelayMs", next.general.init_delay_ms, config_path);
    next.general.stale_component_ms = ReadDword(L"General", L"StaleComponentMs", next.general.stale_component_ms, config_path);
    next.general.relock_idle_ms = ReadDword(L"General", L"RelockIdleMs", next.general.relock_idle_ms, config_path);

    const bool has_legacy_damage_multiplier = HasIniKey(L"Damage", L"Multiplier", config_path);
    const double legacy_damage_multiplier =
        ReadDouble(L"Damage", L"Multiplier", next.damage.outgoing.multiplier, config_path);
    next.damage.outgoing.enabled =
        ReadBool(L"OutgoingDamage", L"Enabled", has_legacy_damage_multiplier, config_path);
    next.damage.outgoing.multiplier =
        ReadDouble(L"OutgoingDamage", L"Multiplier", legacy_damage_multiplier, config_path);
    next.damage.incoming.enabled =
        ReadBool(L"IncomingDamage", L"Enabled", next.damage.incoming.enabled, config_path);
    next.damage.incoming.multiplier =
        ReadDouble(L"IncomingDamage", L"Multiplier", next.damage.incoming.multiplier, config_path);
    next.items.gain_multiplier = ReadDouble(L"Items", L"GainMultiplier", next.items.gain_multiplier, config_path);
    next.durability.consumption_chance =
        ReadDoubleRaw(L"Durability", L"ConsumptionChance", next.durability.consumption_chance, config_path);
    next.mount.enabled = ReadBool(L"Mount", L"Enabled", next.mount.enabled, config_path);
    next.mount.lock_health = ReadBool(L"Mount", L"LockHealth", next.mount.lock_health, config_path);
    next.mount.lock_stamina = ReadBool(L"Mount", L"LockStamina", next.mount.lock_stamina, config_path);
    next.mount.lock_value = static_cast<int64_t>(
        ReadDword(L"Mount", L"LockValue", static_cast<DWORD>(next.mount.lock_value), config_path));
    next.position_control.enabled =
        ReadBool(L"Position Control(Height)", L"Enable", next.position_control.enabled, config_path);
    next.position_control.key =
        static_cast<int>(ReadDword(L"Position Control(Height)", L"Key", static_cast<DWORD>(next.position_control.key), config_path));
    next.position_control.amplitude = static_cast<float>(
        ReadDouble(L"Position Control(Height)", L"Amplitude", next.position_control.amplitude, config_path));
    next.position_control.horizontal_enabled =
        ReadBool(L"Position Control(Horizontal)", L"Enable", next.position_control.horizontal_enabled, config_path);
    next.position_control.horizontal_key = static_cast<int>(
        ReadDword(L"Position Control(Horizontal)", L"Key", static_cast<DWORD>(next.position_control.horizontal_key), config_path));
    next.position_control.horizontal_multiplier = static_cast<float>(ReadDouble(
        L"Position Control(Horizontal)", L"Multiplier", next.position_control.horizontal_multiplier, config_path));

    next.health.consumption_multiplier = ReadDouble(L"Health", L"ConsumptionMultiplier", next.health.consumption_multiplier, config_path);
    next.health.heal_multiplier = ReadDouble(L"Health", L"HealMultiplier", next.health.heal_multiplier, config_path);

    next.stamina.consumption_multiplier = ReadDouble(L"Stamina", L"ConsumptionMultiplier", next.stamina.consumption_multiplier, config_path);
    next.stamina.heal_multiplier = ReadDouble(L"Stamina", L"HealMultiplier", next.stamina.heal_multiplier, config_path);

    next.spirit.consumption_multiplier = ReadDouble(L"Spirit", L"ConsumptionMultiplier", next.spirit.consumption_multiplier, config_path);
    next.spirit.heal_multiplier = ReadDouble(L"Spirit", L"HealMultiplier", next.spirit.heal_multiplier, config_path);

    SanitizeConfig(&next);
    *config = next;
    return true;
}

void SetConfigSnapshot(const std::wstring& config_path, const ModConfig& config) {
    g_config.store(std::make_shared<const ModConfig>(config), std::memory_order_release);

    std::lock_guard lock(g_config_path_mutex);
    g_config_path = config_path;
}

bool LoadConfig(const std::wstring& config_path) {
    ModConfig next{};
    if (!ReadConfigSnapshot(config_path, &next)) {
        return false;
    }

    SetConfigSnapshot(config_path, next);
    return true;
}

ModConfig GetConfig() {
    const auto snapshot = g_config.load(std::memory_order_acquire);
    if (!snapshot) {
        return {};
    }

    return *snapshot;
}

std::wstring GetLoadedConfigPath() {
    std::lock_guard lock(g_config_path_mutex);
    return g_config_path;
}
