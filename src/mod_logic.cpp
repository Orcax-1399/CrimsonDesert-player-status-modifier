#include "mod_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/actor_resolve.h"
#include "runtime/mount_resolver.h"
#include "runtime/runtime_state.h"

#include <cmath>
#include <cstdint>
#include <limits>

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
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
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

uintptr_t GetTrackedPlayerStatusMarker() {
    return g_player_resolve.marker;
}
