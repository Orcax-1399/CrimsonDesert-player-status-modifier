#include "runtime/stat_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/runtime_state.h"

#include <cstdint>

namespace {

      bool TryScalePlayerStaminaConsume(const ModConfig& config,
                                        const int64_t original_value,
                                        int64_t* const adjusted_value) {
          if (adjusted_value == nullptr || original_value >= 0) {
              return false;
          }

          if (config.stamina.consumption_multiplier == 1.0) {
              return false;
          }

          const int64_t scaled = -ScaleDelta(-original_value, config.stamina.consumption_multiplier);
          if (scaled == original_value) {
              return false;
          }

          *adjusted_value = scaled;
          return true;
      }

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

bool TryAdjustStatWrite(const uintptr_t entry,
                        const bool player_context,
                        const uintptr_t context_root_a,
                        const uintptr_t context_root_b,
                        int64_t* const value) {
    static_cast<void>(context_root_a);
    static_cast<void>(context_root_b);

    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr || !IsPlayerRuntimeReady()) {
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

    const TrackedStatEntryKind entry_kind = ClassifyTrackedStatEntry(entry);

    if (IsMountTrackedStatEntry(entry_kind)) {
        // Mount stat locking no longer uses the shared stat-write path.
        return false;
    }

    int32_t stat_type = ResolveTrackedStatType(entry_kind);
    if (stat_type < 0 &&
        !TryResolvePlayerStatTypeFromWrite(entry, actual_type, player_context, &stat_type)) {
        return false;
    }

    const TrackedStatEntryKind resolved_entry_kind = ClassifyTrackedStatEntry(entry);
    if (stat_type == kHealthId || stat_type == kSpiritId) {
        // Player health and player spirit no longer adjust at the shared stat-write path.
        return false;
    }

    if (stat_type == kStaminaId && resolved_entry_kind != TrackedStatEntryKind::PlayerStamina) {
        // Player stamina may use stat-write, but only for the tracked player stamina entry.
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
