#include "hooks.h"

#include "config.h"
#include "hooks/hooks_internal.h"

#include "logger.h"

std::mutex g_hook_mutex;

SafetyHookMid g_player_pointer_hook{};
SafetyHookMid g_stats_hook{};
SafetyHookMid g_stat_write_hook{};
SafetyHookMid g_dragon_village_summon_hook{};
SafetyHookInline g_dragon_flying_restrict_hook{};
SafetyHookMid g_dragon_roof_restrict_hook{};
SafetyHookMid g_damage_hook{};
SafetyHookMid g_item_gain_hook{};
SafetyHookMid g_affinity_existing_hook{};
SafetyHookMid g_affinity_append_hook{};
SafetyHookMid g_affinity_single_hook{};
SafetyHookMid g_durability_hook{};
SafetyHookMid g_durability_delta_hook{};
SafetyHookMid g_abyss_durability_delta_hook{};
SafetyHookMid g_position_height_hook{};

std::atomic<bool> g_reported_stats_exception{false};
std::atomic<bool> g_reported_stat_write_exception{false};
std::atomic<bool> g_reported_damage_exception{false};
std::atomic<bool> g_reported_item_gain_exception{false};
std::atomic<bool> g_reported_affinity_exception{false};
std::atomic<bool> g_reported_durability_exception{false};
std::atomic<bool> g_reported_durability_delta_exception{false};
std::atomic<bool> g_reported_abyss_durability_delta_exception{false};

std::atomic<std::uint32_t> g_player_pointer_samples{0};
std::atomic<std::uint32_t> g_stats_samples{0};
std::atomic<std::uint32_t> g_stat_write_samples{0};
std::atomic<std::uint32_t> g_damage_samples{0};
std::atomic<std::uint32_t> g_item_gain_samples{0};
std::atomic<std::uint32_t> g_affinity_samples{0};
std::atomic<std::uint32_t> g_durability_samples{0};
std::atomic<std::uint32_t> g_durability_delta_samples{0};
std::atomic<std::uint32_t> g_abyss_durability_delta_samples{0};

namespace {

bool AreSharedStatHooksInstalled(const ModConfig& config) {
    return !ShouldInstallSharedStatHooks(config) || (g_stats_hook && g_stat_write_hook);
}

bool AreDragonHooksInstalled(const ModConfig& config) {
    return (!ShouldInstallDragonVillageSummonHook(config) || g_dragon_village_summon_hook) &&
           (!ShouldInstallDragonFlyingRestrictHook(config) || g_dragon_flying_restrict_hook) &&
           (!ShouldInstallDragonRoofRestrictHook(config) || g_dragon_roof_restrict_hook);
}

bool AreEconomyHooksInstalled(const ModConfig& config) {
    return (!ShouldInstallDamageHook(config) || g_damage_hook) &&
           (!ShouldInstallItemGainHook(config) || g_item_gain_hook);
}

bool AreAffinityHooksInstalled(const ModConfig& config) {
    return !ShouldInstallAffinityHook(config) ||
           (g_affinity_existing_hook && g_affinity_append_hook && g_affinity_single_hook);
}

bool AreDurabilityHooksInstalled(const ModConfig& config) {
    return !ShouldInstallDurabilityHooks(config) ||
           (g_durability_hook && g_durability_delta_hook && g_abyss_durability_delta_hook);
}

bool AreCoreHooksInstalled() {
    const auto config = GetConfig();
    return g_player_pointer_hook &&
           AreSharedStatHooksInstalled(config) &&
           AreDragonHooksInstalled(config) &&
           AreEconomyHooksInstalled(config) &&
           AreAffinityHooksInstalled(config) &&
           AreDurabilityHooksInstalled(config);
}

void RemoveHooksLocked() {
    RemoveDurabilityHooks();
    RemoveAffinityHooks();
    RemoveEconomyHooks();
    RemoveDragonLimitHooks();
    RemovePlayerHooks();
}

}  // namespace

bool InstallHooks() {
    const auto config = GetConfig();
    std::lock_guard lock(g_hook_mutex);
    if (AreCoreHooksInstalled()) {
        return true;
    }

    RemoveHooksLocked();

    if (!InstallPlayerHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (ShouldInstallSharedStatHooks(config) && !InstallPlayerStatHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (!InstallDragonLimitHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (!InstallEconomyHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (ShouldInstallAffinityHook(config) &&
        (!g_affinity_existing_hook || !g_affinity_append_hook || !g_affinity_single_hook) &&
        !InstallAffinityHooks()) {
        Log("hooks: affinity hook unavailable; continuing without affinity scaling");
    }

    if (!InstallDurabilityHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (ShouldInstallPositionHeightHook(config) && !g_position_height_hook && !InstallOptionalPositionHeightHook()) {
        Log("hooks: position-height hook unavailable; continuing without position control");
    }

    return true;
}

bool IsPositionHeightHookInstalled() {
    std::lock_guard lock(g_hook_mutex);
    return static_cast<bool>(g_position_height_hook);
}

void RemoveHooks() {
    std::lock_guard lock(g_hook_mutex);
    RemoveHooksLocked();
}
