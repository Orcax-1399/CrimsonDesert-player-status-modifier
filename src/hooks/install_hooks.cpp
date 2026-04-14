#include "hooks.h"

#include "config.h"
#include "hooks/hooks_internal.h"

#include "logger.h"

std::mutex g_hook_mutex;

SafetyHookMid g_player_pointer_hook{};
SafetyHookMid g_stats_hook{};
SafetyHookMid g_stat_write_hook{};
SafetyHookMid g_spirit_delta_hook{};
SafetyHookMid g_stamina_ab00_hook{};
SafetyHookMid g_dragon_village_summon_hook{};
SafetyHookInline g_dragon_flying_restrict_hook{};
SafetyHookMid g_dragon_roof_restrict_hook{};
SafetyHookMid g_damage_hook{};
SafetyHookMid g_item_gain_hook{};
SafetyHookMid g_affinity_hook{};
SafetyHookMid g_affinity_current_hook{};
SafetyHookMid g_durability_hook{};
SafetyHookMid g_durability_delta_hook{};
SafetyHookMid g_abyss_durability_delta_hook{};
SafetyHookMid g_position_height_hook{};

std::atomic<bool> g_reported_stats_exception{false};
std::atomic<bool> g_reported_stat_write_exception{false};
std::atomic<bool> g_reported_spirit_delta_exception{false};
std::atomic<bool> g_reported_stamina_ab00_exception{false};
std::atomic<bool> g_reported_damage_exception{false};
std::atomic<bool> g_reported_item_gain_exception{false};
std::atomic<bool> g_reported_affinity_exception{false};
std::atomic<bool> g_reported_durability_exception{false};
std::atomic<bool> g_reported_durability_delta_exception{false};
std::atomic<bool> g_reported_abyss_durability_delta_exception{false};

std::atomic<std::uint32_t> g_player_pointer_samples{0};
std::atomic<std::uint32_t> g_stats_samples{0};
std::atomic<std::uint32_t> g_stat_write_samples{0};
std::atomic<std::uint32_t> g_spirit_delta_samples{0};
std::atomic<std::uint32_t> g_stamina_ab00_samples{0};
std::atomic<std::uint32_t> g_damage_samples{0};
std::atomic<std::uint32_t> g_item_gain_samples{0};
std::atomic<std::uint32_t> g_affinity_samples{0};
std::atomic<std::uint32_t> g_durability_samples{0};
std::atomic<std::uint32_t> g_durability_delta_samples{0};
std::atomic<std::uint32_t> g_abyss_durability_delta_samples{0};

namespace {

bool AreSharedStatHooksInstalled(const ModConfig& config) {
    return (!ShouldInstallSharedStatHooks(config) || g_stats_hook) &&
           (!ShouldInstallLegacyStatWriteHook(config) || g_stat_write_hook) &&
           (!ShouldInstallSpiritHook(config) || g_spirit_delta_hook) &&
           (!ShouldInstallMountStaminaHook(config) || g_stamina_ab00_hook);
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
    return !ShouldInstallAffinityHook(config) || g_affinity_hook || g_affinity_current_hook;
}

bool AreDurabilityHooksInstalled(const ModConfig& config) {
    return !ShouldInstallDurabilityHooks(config) ||
           (g_durability_hook && g_durability_delta_hook && g_abyss_durability_delta_hook);
}

void LogHookLoadout(const ModConfig& config) {
    Log("hooks: loadout player-pointer=%d shared-stats=%d stat-write=%d spirit-delta=%d stamina-ab00=%d damage=%d items=%d affinity-prepare=%d affinity-current=%d durability=%d dragon-village=%d dragon-flying=%d dragon-roof=%d position=%d",
        g_player_pointer_hook ? 1 : 0,
        AreSharedStatHooksInstalled(config) ? 1 : 0,
        g_stat_write_hook ? 1 : 0,
        g_spirit_delta_hook ? 1 : 0,
        g_stamina_ab00_hook ? 1 : 0,
        g_damage_hook ? 1 : 0,
        g_item_gain_hook ? 1 : 0,
        g_affinity_hook ? 1 : 0,
        g_affinity_current_hook ? 1 : 0,
        (g_durability_hook && g_durability_delta_hook && g_abyss_durability_delta_hook) ? 1 : 0,
        g_dragon_village_summon_hook ? 1 : 0,
        g_dragon_flying_restrict_hook ? 1 : 0,
        g_dragon_roof_restrict_hook ? 1 : 0,
        g_position_height_hook ? 1 : 0);
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

    InstallDragonLimitHooks();
    InstallEconomyHooks();

    if (ShouldInstallAffinityHook(config) &&
        !AreAffinityHooksInstalled(config) &&
        !InstallAffinityHooks()) {
        Log("hooks: affinity hook unavailable; continuing without affinity scaling");
    }

    InstallDurabilityHooks();

    if (ShouldInstallPositionHeightHook(config) && !g_position_height_hook && !InstallOptionalPositionHeightHook()) {
        Log("hooks: position-height hook unavailable; continuing without position control");
    }

    LogHookLoadout(config);

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
