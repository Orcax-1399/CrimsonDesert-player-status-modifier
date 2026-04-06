#include "hooks.h"

#include "hooks/hooks_internal.h"

#include "logger.h"

std::mutex g_hook_mutex;

SafetyHookMid g_player_pointer_hook{};
SafetyHookMid g_stats_hook{};
SafetyHookMid g_stat_write_hook{};
SafetyHookMid g_damage_hook{};
SafetyHookMid g_item_gain_hook{};
SafetyHookMid g_durability_hook{};
SafetyHookMid g_durability_delta_hook{};
SafetyHookMid g_abyss_durability_delta_hook{};
SafetyHookMid g_position_height_hook{};

std::atomic<bool> g_reported_stats_exception{false};
std::atomic<bool> g_reported_stat_write_exception{false};
std::atomic<bool> g_reported_damage_exception{false};
std::atomic<bool> g_reported_item_gain_exception{false};
std::atomic<bool> g_reported_durability_exception{false};
std::atomic<bool> g_reported_durability_delta_exception{false};
std::atomic<bool> g_reported_abyss_durability_delta_exception{false};

std::atomic<std::uint32_t> g_player_pointer_samples{0};
std::atomic<std::uint32_t> g_stats_samples{0};
std::atomic<std::uint32_t> g_stat_write_samples{0};
std::atomic<std::uint32_t> g_damage_samples{0};
std::atomic<std::uint32_t> g_item_gain_samples{0};
std::atomic<std::uint32_t> g_durability_samples{0};
std::atomic<std::uint32_t> g_durability_delta_samples{0};
std::atomic<std::uint32_t> g_abyss_durability_delta_samples{0};

namespace {

bool AreCoreHooksInstalled() {
    return g_player_pointer_hook &&
           g_stats_hook &&
           g_stat_write_hook &&
           g_damage_hook &&
           g_item_gain_hook &&
           g_durability_hook &&
           g_durability_delta_hook &&
           g_abyss_durability_delta_hook;
}

void RemoveHooksLocked() {
    RemoveDurabilityHooks();
    RemoveEconomyHooks();
    RemovePlayerHooks();
}

}  // namespace

bool InstallHooks() {
    std::lock_guard lock(g_hook_mutex);
    if (AreCoreHooksInstalled()) {
        return true;
    }

    RemoveHooksLocked();

    if (!InstallPlayerHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (!InstallEconomyHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (!InstallDurabilityHooks()) {
        RemoveHooksLocked();
        return false;
    }

    if (!g_position_height_hook && !InstallOptionalPositionHeightHook()) {
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
