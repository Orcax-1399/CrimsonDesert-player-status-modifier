#include "runtime/stat_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"

#include <cstdint>

namespace {

bool TryAdjustPlayerStaminaDelta(const ModConfig& config,
                                 const uintptr_t entry,
                                 const TrackedStatEntryKind entry_kind,
                                 const int32_t actual_type,
                                 int64_t* const delta) {
    static_cast<void>(config);
    static_cast<void>(entry);

          if (delta == nullptr ||
              entry_kind != TrackedStatEntryKind::PlayerStamina ||
              actual_type != kStaminaId ||
              *delta >= 0) {
              return false;
          }

          // Player stamina no longer adjusts at AB00. This site only remains for mount locking.
          return false;
      }

bool TryAdjustMountStaminaDelta(const ModConfig& config,
                                const TrackedStatEntryKind entry_kind,
                                const int32_t actual_type,
                                int64_t* const delta) {
    if (delta == nullptr ||
        !config.mount.enabled ||
        !config.mount.lock_stamina ||
        entry_kind != TrackedStatEntryKind::MountStamina ||
        actual_type != kStaminaId ||
        *delta >= 0) {
        return false;
    }

    const int64_t original_delta = *delta;
    *delta = -*delta;

    const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 32) {
        const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
        Log("runtime: locked mount stamina root=0x%p entry=0x%p delta=%lld final=%lld",
            reinterpret_cast<void*>(mount_snapshot.root),
            reinterpret_cast<void*>(mount_snapshot.stamina_entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(*delta));
    }

    return true;
}

bool TryAdjustPlayerSpiritDelta(const ModConfig& config,
                                const uintptr_t entry,
                                const TrackedStatEntryKind entry_kind,
                                const int32_t actual_type,
                                int64_t* const delta) {
    if (delta == nullptr ||
        entry_kind != TrackedStatEntryKind::PlayerSpirit ||
        actual_type != kSpiritId ||
        *delta == 0) {
        return false;
    }

    const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    if (max_value <= 0 || current_value < 0 || current_value > max_value) {
        return false;
    }

    StatConfig stat_config{};
    if (!SelectConfig(config, kSpiritId, &stat_config)) {
        return false;
    }

    const int64_t original_delta = *delta;
    int64_t adjusted_delta = original_delta;
    if (original_delta < 0) {
        adjusted_delta = -ScaleDelta(-original_delta, stat_config.consumption_multiplier);
    } else {
        adjusted_delta = ScaleDelta(original_delta, stat_config.heal_multiplier);
    }

    if (adjusted_delta == original_delta) {
        return false;
    }

    *delta = adjusted_delta;

    const auto current = g_process_apply_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: adjusted spirit delta entry=0x%p old=%lld final=%lld current=%lld max=%lld",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(original_delta),
            static_cast<long long>(adjusted_delta),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }

    return true;
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

bool TryAdjustStaminaDelta(const uintptr_t entry, int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        entry < kMinimumPointerAddress ||
        !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled) {
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    if (actual_type != kStaminaId || *delta == 0) {
        return false;
    }

    const TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    if (TryAdjustMountStaminaDelta(config, entry_kind, actual_type, delta)) {
        return true;
    }

              return TryAdjustPlayerStaminaDelta(config, entry, entry_kind, actual_type, delta);
            }

bool TryAdjustSpiritDelta(const uintptr_t entry, int64_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        delta == nullptr ||
        entry < kMinimumPointerAddress ||
        !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!ShouldInstallSpiritHook(config)) {
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    const TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    return TryAdjustPlayerSpiritDelta(config, entry, entry_kind, actual_type, delta);
}

bool TryAdjustStatWrite(const uintptr_t entry, int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr || !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
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

    const TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);
    if (entry_kind != TrackedStatEntryKind::PlayerStamina) {
        // Shared stat-write now only serves the tracked player stamina entry.
        return false;
    }

    const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
    constexpr int32_t stat_type = kStaminaId;
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

    int64_t adjusted_value = requested_value;
    const int64_t delta = requested_value - old_value;
    if (delta == 0) {
        return false;
    }

    if (delta < 0) {
        const int64_t consumed = -delta;
        const int64_t target_consumption = ScaleDelta(consumed, config.stamina.consumption_multiplier);
        const int64_t adjustment = consumed - target_consumption;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    } else {
        const int64_t healed = delta;
        const int64_t target_heal = ScaleDelta(healed, config.stamina.heal_multiplier);
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
