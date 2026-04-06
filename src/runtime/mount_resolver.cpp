#include "runtime/mount_resolver.h"

#include "logger.h"
#include "mod_logic.h"
#include "ptrchain.h"
#include "ptrchain_resources.h"
#include "runtime/actor_resolve.h"

#include <Windows.h>

#include <thread>

namespace {

bool TryResolveCurrentMountCandidateMarker(const ActorResolveSnapshot& player_snapshot, uintptr_t* const marker) {
    if (marker == nullptr || !player_snapshot.valid()) {
        return false;
    }

    uintptr_t resolved_marker = 0;
    if (!TryResolveMountedDragonMarker(&resolved_marker) ||
        resolved_marker < kMinimumPointerAddress ||
        resolved_marker == player_snapshot.marker) {
        return false;
    }

    const auto current = g_mount_candidate_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: mount chain matched name=%s marker=0x%p",
            kDragonMarkerChain.name,
            reinterpret_cast<void*>(resolved_marker));
    }

    *marker = resolved_marker;
    return true;
}

bool TryResolveCurrentMountFromPlayer(const ActorResolveSnapshot& player_snapshot,
                                      ActorResolveSnapshot* const mount_snapshot) {
    if (mount_snapshot == nullptr) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryResolveCurrentMountCandidateMarker(player_snapshot, &marker)) {
        return false;
    }

    if (!TryResolveActorResolveFromMarker(marker, mount_snapshot)) {
        return false;
    }

    if (mount_snapshot->marker == player_snapshot.marker || mount_snapshot->root == player_snapshot.root) {
        return false;
    }

    return true;
}

void MountResolverLoop() {
    while (g_mount_resolver_running.load(std::memory_order_acquire)) {
        RefreshTrackedMountFromPlayerActor();
        Sleep(kMountResolvePollMs);
    }
}

}  // namespace

bool TryResolveMountContext(const uintptr_t context_root_a,
                            const uintptr_t context_root_b,
                            ActorResolveSnapshot* const mount_snapshot) {
    if (mount_snapshot == nullptr) {
        return false;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return false;
    }

    if (TryResolveActorResolveFromContextRoot(context_root_a, mount_snapshot, player_snapshot)) {
        return true;
    }

    if (TryResolveActorResolveFromContextRoot(context_root_b, mount_snapshot, player_snapshot)) {
        return true;
    }

    const ActorResolveSnapshot current_mount = g_mount_resolve;
    if (!current_mount.valid()) {
        return false;
    }

    if (current_mount.marker == player_snapshot.marker || current_mount.root == player_snapshot.root) {
        return false;
    }

    *mount_snapshot = current_mount;
    return true;
}

void RefreshTrackedMountFromPlayerActor() {
    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (!player_snapshot.valid()) {
        return;
    }

    ActorResolveSnapshot mount_snapshot{};
    if (!TryResolveCurrentMountFromPlayer(player_snapshot, &mount_snapshot)) {
        if (!g_mount_resolve.valid()) {
            return;
        }

        std::lock_guard lock(g_state_mutex);
        if (g_mount_resolve.valid()) {
            ResetTrackedMountLocked();
        }
        return;
    }

    UpdateTrackedMountStatusComponent(mount_snapshot.actor, mount_snapshot.marker);
}

bool StartMountResolverLoop() {
    if (g_mount_resolver_running.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }

    try {
        g_mount_resolver_thread = std::thread(MountResolverLoop);
    } catch (...) {
        g_mount_resolver_running.store(false, std::memory_order_release);
        return false;
    }

    return true;
}

void StopMountResolverLoop() {
    if (!g_mount_resolver_running.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (g_mount_resolver_thread.joinable()) {
        g_mount_resolver_thread.join();
    }
}
