#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include <safetyhook.hpp>

inline constexpr uintptr_t kMinimumPointerAddress = 0x10000000;

extern std::mutex g_hook_mutex;

extern SafetyHookMid g_player_pointer_hook;
extern SafetyHookMid g_stats_hook;
extern SafetyHookMid g_stat_write_hook;
extern SafetyHookMid g_spirit_delta_hook;
extern SafetyHookMid g_stamina_ab00_hook;
extern SafetyHookMid g_dragon_village_summon_hook;
extern SafetyHookInline g_dragon_flying_restrict_hook;
extern SafetyHookMid g_dragon_roof_restrict_hook;
extern SafetyHookMid g_damage_hook;
extern SafetyHookMid g_item_gain_hook;
extern SafetyHookMid g_affinity_hook;
extern SafetyHookMid g_affinity_current_hook;
extern SafetyHookMid g_durability_hook;
extern SafetyHookMid g_durability_delta_hook;
extern SafetyHookMid g_abyss_durability_delta_hook;
extern SafetyHookMid g_position_height_hook;

extern std::atomic<bool> g_reported_stats_exception;
extern std::atomic<bool> g_reported_stat_write_exception;
extern std::atomic<bool> g_reported_spirit_delta_exception;
extern std::atomic<bool> g_reported_stamina_ab00_exception;
extern std::atomic<bool> g_reported_damage_exception;
extern std::atomic<bool> g_reported_item_gain_exception;
extern std::atomic<bool> g_reported_affinity_exception;
extern std::atomic<bool> g_reported_durability_exception;
extern std::atomic<bool> g_reported_durability_delta_exception;
extern std::atomic<bool> g_reported_abyss_durability_delta_exception;

extern std::atomic<std::uint32_t> g_player_pointer_samples;
extern std::atomic<std::uint32_t> g_stats_samples;
extern std::atomic<std::uint32_t> g_stat_write_samples;
extern std::atomic<std::uint32_t> g_spirit_delta_samples;
extern std::atomic<std::uint32_t> g_stamina_ab00_samples;
extern std::atomic<std::uint32_t> g_damage_samples;
extern std::atomic<std::uint32_t> g_item_gain_samples;
extern std::atomic<std::uint32_t> g_affinity_samples;
extern std::atomic<std::uint32_t> g_durability_samples;
extern std::atomic<std::uint32_t> g_durability_delta_samples;
extern std::atomic<std::uint32_t> g_abyss_durability_delta_samples;

inline bool ShouldLogSample(std::atomic<std::uint32_t>& counter, const std::uint32_t limit) {
    const auto current = counter.fetch_add(1, std::memory_order_acq_rel);
    return current < limit;
}

bool InstallPlayerHooks();
bool InstallPlayerStatHooks();
void RemovePlayerStatHooks();
void RemovePlayerHooks();

bool InstallDragonLimitHooks();
void RemoveDragonLimitHooks();

bool InstallEconomyHooks();
void RemoveEconomyHooks();

bool InstallAffinityHooks();
void RemoveAffinityHooks();

bool InstallDurabilityHooks();
void RemoveDurabilityHooks();

bool InstallOptionalPositionHeightHook();
