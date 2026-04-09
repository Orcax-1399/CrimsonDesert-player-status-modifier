#include "runtime/runtime_state.h"

std::mutex g_state_mutex;
ActorResolveSnapshot g_player_resolve{};
ActorResolveSnapshot g_mount_resolve{};
std::atomic<bool> g_mount_resolver_running{false};
std::thread g_mount_resolver_thread{};
std::array<std::atomic<uintptr_t>, kTrackedDamageParticipantCount> g_tracked_damage_participants{};
std::atomic<uint32_t> g_tracked_damage_participant_cursor{0};
std::atomic<bool> g_runtime_enabled{true};
std::atomic<std::uint32_t> g_process_skip_logs{0};
std::atomic<std::uint32_t> g_process_apply_logs{0};
std::atomic<std::uint32_t> g_discovery_logs{0};
std::atomic<std::uint32_t> g_durability_logs{0};
std::atomic<std::uint32_t> g_damage_logs{0};
std::atomic<std::uint32_t> g_affinity_logs{0};
std::atomic<std::uint32_t> g_mount_logs{0};
std::atomic<std::uint32_t> g_actor_resolve_logs{0};
std::atomic<std::uint32_t> g_mount_candidate_logs{0};

void ResetTrackedEntriesLocked() {}

void ResetTrackedMountLocked() {
    g_mount_resolve = {};
}

void ResetTrackedDamageParticipantsLocked() {
    for (auto& participant : g_tracked_damage_participants) {
        participant.store(0, std::memory_order_release);
    }

    g_tracked_damage_participant_cursor.store(0, std::memory_order_release);
}
