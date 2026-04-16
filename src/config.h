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

struct AffinityConfig {
    double multiplier = 1.0;

    bool operator==(const AffinityConfig&) const = default;
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
    bool verbose = false;
    DWORD max_log_lines = 2000;
    DWORD init_delay_ms = 3000;
    DWORD stale_component_ms = 60000;
    DWORD relock_idle_ms = 10000;

    bool operator==(const GeneralConfig&) const = default;
};

struct ModConfig {
    GeneralConfig general;
    DamageConfig damage;
    ItemConfig items;
    AffinityConfig affinity;
    DurabilityConfig durability;
    MountConfig mount;
    DragonLimitConfig dragon_limit;
    PositionControlConfig position_control;
    StatConfig health{0.5, 2.0};
    StatConfig stamina{0.5, 1.0};
    StatConfig spirit{0.5, 2.0};

    bool operator==(const ModConfig&) const = default;
};

inline bool IsStatConfigNeutral(const StatConfig& config) {
    return config.consumption_multiplier == 1.0 && config.heal_multiplier == 1.0;
}

inline bool IsAnyPlayerStatMultiplierEnabled(const ModConfig& config) {
    return !IsStatConfigNeutral(config.health) ||
           !IsStatConfigNeutral(config.stamina) ||
           !IsStatConfigNeutral(config.spirit);
}

inline bool IsMountLockEnabled(const ModConfig& config) {
    return config.mount.enabled && (config.mount.lock_health || config.mount.lock_stamina);
}

inline bool ShouldInstallStaminaHook(const ModConfig& config) {
    return config.general.enabled &&
           (!IsStatConfigNeutral(config.stamina) ||
            (config.mount.enabled && config.mount.lock_stamina));
}

inline bool ShouldInstallPlayerStaminaHooks(const ModConfig& config) {
    static_cast<void>(config);
    return false;
}

inline bool ShouldInstallMountStaminaHook(const ModConfig& config) {
    return config.general.enabled && config.mount.enabled && config.mount.lock_stamina;
}

inline bool ShouldInstallSpiritHook(const ModConfig& config) {
    return config.general.enabled && !IsStatConfigNeutral(config.spirit);
}

inline bool ShouldInstallLegacyStatWriteHook(const ModConfig& config) {
    return config.general.enabled && !IsStatConfigNeutral(config.stamina);
}

inline bool ShouldInstallSharedStatHooks(const ModConfig& config) {
    return config.general.enabled &&
           (ShouldInstallMountStaminaHook(config) ||
            ShouldInstallLegacyStatWriteHook(config) ||
            ShouldInstallSpiritHook(config));
}

inline bool ShouldInstallDamageHook(const ModConfig& config) {
    return config.general.enabled &&
           ((config.damage.outgoing.enabled && config.damage.outgoing.multiplier != 1.0) ||
            (config.damage.incoming.enabled && config.damage.incoming.multiplier != 1.0) ||
            (config.mount.enabled && config.mount.lock_health) ||
            config.health.heal_multiplier != 1.0);
}

inline bool ShouldInstallItemGainHook(const ModConfig& config) {
    return config.general.enabled && config.items.gain_multiplier != 1.0;
}

inline bool ShouldInstallAffinityHook(const ModConfig& config) {
    return config.general.enabled && config.affinity.multiplier != 1.0;
}

inline bool ShouldInstallDurabilityHooks(const ModConfig& config) {
    return config.general.enabled && config.durability.consumption_chance < 100.0;
}

inline bool ShouldInstallDragonVillageSummonHook(const ModConfig& config) {
    return config.general.enabled && config.dragon_limit.village_summon;
}

inline bool ShouldInstallDragonFlyingRestrictHook(const ModConfig& config) {
    return config.general.enabled && config.dragon_limit.cancel_restrict_flying;
}

inline bool ShouldInstallDragonRoofRestrictHook(const ModConfig& config) {
    return config.general.enabled && config.dragon_limit.roof_summon_experimental;
}

inline bool ShouldInstallPositionHeightHook(const ModConfig& config) {
    return config.general.enabled && (config.position_control.enabled || config.position_control.horizontal_enabled);
}

bool LoadConfig(const std::wstring& config_path);
bool ReadConfigSnapshot(const std::wstring& config_path, ModConfig* config);
void SetConfigSnapshot(const std::wstring& config_path, const ModConfig& config);
ModConfig GetConfig();
std::wstring GetLoadedConfigPath();
