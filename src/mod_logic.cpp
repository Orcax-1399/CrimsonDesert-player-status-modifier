#include "mod_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/mount_resolver.h"
#include "runtime/runtime_state.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr int64_t kDragonHealthMaxThreshold = 2500000;
constexpr int64_t kDragonStaminaMaxThreshold = 300000;

bool TryReadStatMaxValue(const uintptr_t entry, int64_t* const max_value) {
    if (max_value == nullptr || entry < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsDragonMountProfile(const ActorResolveSnapshot& snapshot) {
    if (!snapshot.valid()) {
        return false;
    }

    int64_t health_max = 0;
    int64_t stamina_max = 0;
    if (!TryReadStatMaxValue(snapshot.health_entry, &health_max) ||
        !TryReadStatMaxValue(snapshot.stamina_entry, &stamina_max)) {
        return false;
    }

    return health_max >= kDragonHealthMaxThreshold &&
           stamina_max >= kDragonStaminaMaxThreshold;
}

bool TryResolveMountSnapshotFromContextRoot(const uintptr_t context_root,
                                           ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || context_root < kMinimumPointerAddress) {
        return false;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return false;
    }

    if (!TryResolveActorResolveFromContextRoot(context_root, resolved, player_snapshot)) {
        return false;
    }

    return IsDragonMountProfile(*resolved);
}

}  // namespace

void ResetRuntimeState() {
    std::lock_guard lock(g_state_mutex);
    g_player_resolve = {};
    g_mount_resolve = {};
    ResetTrackedEntriesLocked();
    ResetTrackedDamageParticipantsLocked();
    g_runtime_enabled.store(true, std::memory_order_release);
}

void DisableRuntimeProcessing() {
    g_runtime_enabled.store(false, std::memory_order_release);
}

bool StartMountResolver() {
    return StartMountResolverLoop();
}

void StopMountResolver() {
    StopMountResolverLoop();
}

bool TryScaleItemGain(const int64_t amount, int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr || !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || config.items.gain_multiplier == 1.0 || amount <= 0) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(amount) * config.items.gain_multiplier);
    if (scaled <= 0.0) {
        *value = 0;
        return true;
    }

    if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        *value = std::numeric_limits<int64_t>::max();
        return true;
    }

    *value = static_cast<int64_t>(scaled);
    return *value != amount;
}

void UpdateTrackedMountFromHealthRoot(const uintptr_t root) {
    ActorResolveSnapshot resolved{};
    if (!TryResolveMountSnapshotFromContextRoot(root, &resolved)) {
        return;
    }

    UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
}

void UpdateTrackedMountFromStaminaContext(const uintptr_t stamina_entry, const uintptr_t context_root) {
    if (stamina_entry < kMinimumPointerAddress) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveMountSnapshotFromContextRoot(context_root, &resolved)) {
        return;
    }

    if (resolved.stamina_entry != stamina_entry) {
        return;
    }

    UpdateTrackedMountStatusComponent(resolved.actor, resolved.marker);
}

void UpdateTrackedMountStatusComponent(const uintptr_t actor, const uintptr_t marker) {
    if (marker < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t player_marker = g_player_resolve.marker;
    if (marker == player_marker) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(marker, &resolved, actor)) {
        return;
    }

    if (resolved.marker == g_player_resolve.marker || resolved.root == g_player_resolve.root) {
        return;
    }

    const ActorResolveSnapshot current_mount = g_mount_resolve;
    if (current_mount.actor == resolved.actor && current_mount.marker == resolved.marker) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (g_mount_resolve.actor == resolved.actor && g_mount_resolve.marker == resolved.marker) {
        return;
    }

    g_mount_resolve = resolved;

    const auto current = g_actor_resolve_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: tracked mount actor=0x%p root=0x%p status_marker=0x%p health=0x%p stamina=0x%p spirit=0x%p",
            reinterpret_cast<void*>(resolved.actor),
            reinterpret_cast<void*>(resolved.root),
            reinterpret_cast<void*>(resolved.marker),
            reinterpret_cast<void*>(resolved.health_entry),
            reinterpret_cast<void*>(resolved.stamina_entry),
            reinterpret_cast<void*>(resolved.spirit_entry));
    }
}

void UpdateTrackedPlayerStatusComponent(const uintptr_t actor, const uintptr_t component) {
    if (component < kMinimumPointerAddress) {
        return;
    }

    const auto current_marker = g_player_resolve.marker;
    if (current_marker == component) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (g_player_resolve.marker == component) {
        return;
    }

    ActorResolveSnapshot resolved{};
    if (!TryResolveActorResolveFromMarker(component, &resolved, actor)) {
        return;
    }

    ResetTrackedEntriesLocked();
    ResetTrackedMountLocked();
    g_player_resolve = resolved;
    ResetTrackedDamageParticipantsLocked();

    Log("runtime: tracked player actor=0x%p status_marker=0x%p stat_root=0x%p",
        reinterpret_cast<void*>(resolved.actor),
        reinterpret_cast<void*>(resolved.marker),
        reinterpret_cast<void*>(resolved.root));

    RefreshTrackedMountFromPlayerActor();
}

uintptr_t GetTrackedPlayerStatRoot() {
    return g_player_resolve.root;
}

uintptr_t GetTrackedMountActor() {
    return g_mount_resolve.actor;
}

uintptr_t GetTrackedMountStatRoot() {
    return g_mount_resolve.root;
}

uintptr_t GetTrackedMountStatusMarker() {
    return g_mount_resolve.marker;
}

uintptr_t GetTrackedPlayerActor() {
    return g_player_resolve.actor;
}

uintptr_t GetTrackedPlayerSpiritEntry() {
    return g_player_resolve.spirit_entry;
}

uintptr_t GetTrackedPlayerStaminaEntry() {
    return g_player_resolve.stamina_entry;
}

uintptr_t GetTrackedPlayerStatusMarker() {
    return g_player_resolve.marker;
}
