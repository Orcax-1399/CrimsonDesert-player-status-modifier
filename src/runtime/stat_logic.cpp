#include "runtime/stat_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/mount_resolver.h"
#include "runtime/runtime_state.h"

#include <cstdint>

namespace {

bool TryResolvePlayerStatTypeFromWrite(const uintptr_t entry,
                                       const int32_t actual_type,
                                       const bool player_context,
                                       int32_t* const stat_type) {
    if (stat_type == nullptr) {
        return false;
    }

    *stat_type = -1;

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    const uintptr_t health_entry = player_snapshot.health_entry;
    const uintptr_t stamina_entry = player_snapshot.stamina_entry;
    const uintptr_t spirit_entry = player_snapshot.spirit_entry;
    if (entry == health_entry) {
        *stat_type = kHealthId;
    } else if (entry == stamina_entry) {
        *stat_type = kStaminaId;
    } else if (entry == spirit_entry) {
        *stat_type = kSpiritId;
    } else if (player_context) {
        if (IsTrackedStat(actual_type)) {
            *stat_type = actual_type;
            std::lock_guard lock(g_state_mutex);
            TryAssignPlayerResolvedEntry(entry, *stat_type);

            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: inferred stat entry from write context type=%d entry=0x%p",
                    *stat_type,
                    reinterpret_cast<void*>(entry));
            }
        }
    } else if (entry == health_entry + kStaminaEntryOffsetFromHealth) {
        *stat_type = kStaminaId;
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: inferred stat entry from health layout type=%d entry=0x%p health=0x%p",
                *stat_type,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(health_entry));
        }
    } else if (spirit_entry >= kMinimumPointerAddress && entry == spirit_entry) {
        *stat_type = kSpiritId;
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: inferred stat entry from health layout type=%d entry=0x%p health=0x%p",
                *stat_type,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(health_entry));
        }
    } else if (spirit_entry == 0 && entry == health_entry + kSpiritEntryOffsetFromHealth && actual_type == kSpiritId) {
        *stat_type = kSpiritId;
        {
            std::lock_guard lock(g_state_mutex);
            TryAssignPlayerResolvedEntry(entry, *stat_type);
        }
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: inferred stat entry from health layout type=%d entry=0x%p health=0x%p",
                *stat_type,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(health_entry));
        }
    }

    return *stat_type >= 0;
}

bool TryAdjustMountStatWrite(const ModConfig& config,
                             const uintptr_t entry,
                             const int32_t actual_type,
                             const int64_t old_value,
                             const int64_t requested_value,
                             const int64_t max_value,
                             const uintptr_t context_root_a,
                             const uintptr_t context_root_b,
                             int64_t* const value) {
    if (value == nullptr) {
        return false;
    }

    ActorResolveSnapshot mount_snapshot{};
    if (!TryResolveMountContext(context_root_a, context_root_b, &mount_snapshot)) {
        return false;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    const uintptr_t player_health_entry = player_snapshot.health_entry;
    const uintptr_t player_stamina_entry = player_snapshot.stamina_entry;
    const uintptr_t player_spirit_entry = player_snapshot.spirit_entry;
    if (entry == player_health_entry || entry == player_stamina_entry || entry == player_spirit_entry) {
        return false;
    }

    const bool is_mount_health = entry == mount_snapshot.health_entry && actual_type == kHealthId;
    const bool is_mount_stamina = entry == mount_snapshot.stamina_entry && actual_type == kStaminaId;
    const bool is_mount_spirit = entry == mount_snapshot.spirit_entry && actual_type == kSpiritId;
    if (!is_mount_health && !is_mount_stamina && !is_mount_spirit) {
        return false;
    }

    bool should_lock = false;
    if (config.mount.enabled) {
        if (config.mount.lock_health && is_mount_health) {
            should_lock = true;
        } else if (config.mount.lock_stamina && is_mount_stamina) {
            should_lock = true;
        }
    }

    if (should_lock) {
        const int64_t locked_value = ClampToRange(config.mount.lock_value, 0, max_value);
        *value = locked_value;

        const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 32) {
            Log("runtime: locked mount stat type=%d root=0x%p entry=0x%p old=%lld requested=%lld final=%lld max=%lld",
                actual_type,
                reinterpret_cast<void*>(mount_snapshot.root),
                reinterpret_cast<void*>(entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(locked_value),
                static_cast<long long>(max_value));
        }

        return locked_value != requested_value;
    }

    const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        Log("runtime: skipped mount stat write type=%d root=0x%p entry=0x%p old=%lld requested=%lld",
            actual_type,
            reinterpret_cast<void*>(mount_snapshot.root),
            reinterpret_cast<void*>(entry),
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value));
    }
    return false;
}

}  // namespace

void ObserveStatEntry(const uintptr_t entry, const uintptr_t component) {
    if (!g_runtime_enabled.load(std::memory_order_acquire)) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    const auto tracked_marker = g_player_resolve.marker;
    const auto component_marker = *reinterpret_cast<const uintptr_t*>(component);
    if (component_marker != tracked_marker) {
        return;
    }

    const auto stat_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsTrackedStat(stat_type)) {
        return;
    }

    const auto* const current_value_ptr = reinterpret_cast<const int64_t*>(entry + 0x08);
    const auto* const max_value_ptr = reinterpret_cast<const int64_t*>(entry + 0x18);
    const int64_t current_value = *current_value_ptr;
    const int64_t max_value = *max_value_ptr;
    if (max_value <= 0 || current_value < 0 || current_value > max_value) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (*reinterpret_cast<const uintptr_t*>(component) != g_player_resolve.marker) {
        return;
    }

    if (!TryAssignPlayerResolvedEntry(entry, stat_type)) {
        return;
    }

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 16) {
        Log("runtime: discovered stat entry type=%d entry=0x%p current=%lld max=%lld",
            stat_type,
            reinterpret_cast<void*>(entry),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }
}

bool TryAdjustStatWrite(const uintptr_t entry,
                        const bool player_context,
                        const uintptr_t context_root_a,
                        const uintptr_t context_root_b,
                        int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    if (!IsTrackedStat(actual_type)) {
        return false;
    }

    const int64_t old_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    const int64_t requested_value = *value;
    if (max_value <= 0 || old_value < 0 || old_value > max_value || requested_value < 0) {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped, invalid range entry=0x%p old=%lld new=%lld max=%lld",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(old_value),
                static_cast<long long>(requested_value),
                static_cast<long long>(max_value));
        }
        return false;
    }

    if (TryAdjustMountStatWrite(config,
                                entry,
                                actual_type,
                                old_value,
                                requested_value,
                                max_value,
                                context_root_a,
                                context_root_b,
                                value)) {
        return true;
    }

    int32_t stat_type = -1;
    if (!TryResolvePlayerStatTypeFromWrite(entry, actual_type, player_context, &stat_type)) {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped for unknown entry=0x%p", reinterpret_cast<void*>(entry));
        }
        return false;
    }

    if (actual_type != stat_type) {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped, type mismatch entry=0x%p expected=%d actual=%d",
                reinterpret_cast<void*>(entry),
                stat_type,
                actual_type);
        }
        return false;
    }

    StatConfig stat_config{};
    if (!SelectConfig(config, stat_type, &stat_config)) {
        return false;
    }

    int64_t adjusted_value = requested_value;
    const int64_t delta = requested_value - old_value;
    if (delta == 0) {
        return false;
    }

    if (delta < 0) {
        const int64_t consumed = -delta;
        const int64_t target_consumption = ScaleDelta(consumed, stat_config.consumption_multiplier);
        const int64_t adjustment = consumed - target_consumption;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    } else {
        const int64_t healed = delta;
        const int64_t target_heal = ScaleDelta(healed, stat_config.heal_multiplier);
        const int64_t adjustment = target_heal - healed;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    }

    *value = adjusted_value;

    const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adjusted stat write type=%d old=%lld requested=%lld final=%lld max=%lld",
            stat_type,
            static_cast<long long>(old_value),
            static_cast<long long>(requested_value),
            static_cast<long long>(adjusted_value),
            static_cast<long long>(max_value));
    }

    return adjusted_value != requested_value;
}
