#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

struct StatConfig {
    double consumption_multiplier = 1.0;
    double heal_multiplier = 1.0;

    bool operator==(const StatConfig&) const = default;
};

struct DamageChannelConfig {
    bool enabled = false;
    double multiplier = 1.0;

    bool operator==(const DamageChannelConfig&) const = default;
};

struct DamageConfig {
    DamageChannelConfig outgoing{true, 2.0};
    DamageChannelConfig incoming{false, 1.0};

    bool operator==(const DamageConfig&) const = default;
};

struct ItemConfig {
    double gain_multiplier = 2.0;

    bool operator==(const ItemConfig&) const = default;
};

struct DurabilityConfig {
    double consumption_chance = 100.0;

    bool operator==(const DurabilityConfig&) const = default;
};

struct PositionControlConfig {
    bool enabled = false;
    int key = VK_F6;
    float amplitude = 0.1f;
    bool horizontal_enabled = false;
    int horizontal_key = VK_F7;
    float horizontal_multiplier = 1.5f;

    bool operator==(const PositionControlConfig&) const = default;
};

struct MountConfig {
    bool enabled = false;
    bool lock_health = true;
    bool lock_stamina = true;
    int64_t lock_value = 9999999;

    bool operator==(const MountConfig&) const = default;
};

struct DragonLimitConfig {
    bool roof_summon_experimental = false;
    bool village_summon = true;
    bool cancel_restrict_flying = true;

    bool operator==(const DragonLimitConfig&) const = default;
};

struct GeneralConfig {
    bool enabled = true;
    bool log_enabled = true;
    DWORD init_delay_ms = 3000;
    DWORD stale_component_ms = 60000;
    DWORD relock_idle_ms = 10000;

    bool operator==(const GeneralConfig&) const = default;
};

struct ModConfig {
    GeneralConfig general;
    DamageConfig damage;
    ItemConfig items;
    DurabilityConfig durability;
    MountConfig mount;
    DragonLimitConfig dragon_limit;
    PositionControlConfig position_control;
    StatConfig health{0.5, 2.0};
    StatConfig stamina{0.5, 1.0};
    StatConfig spirit{0.5, 2.0};

    bool operator==(const ModConfig&) const = default;
};

bool LoadConfig(const std::wstring& config_path);
bool ReadConfigSnapshot(const std::wstring& config_path, ModConfig* config);
void SetConfigSnapshot(const std::wstring& config_path, const ModConfig& config);
ModConfig GetConfig();
std::wstring GetLoadedConfigPath();
