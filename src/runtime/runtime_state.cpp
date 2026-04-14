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

TrackedStatEntryKind ClassifyTrackedStatEntry(const uintptr_t entry) {
    if (entry < kMinimumPointerAddress) {
        return TrackedStatEntryKind::None;
    }

    const ActorResolveSnapshot player_snapshot = g_player_resolve;
    if (entry == player_snapshot.health_entry) {
        return TrackedStatEntryKind::PlayerHealth;
    }

    if (entry == player_snapshot.stamina_entry) {
        return TrackedStatEntryKind::PlayerStamina;
    }

    if (entry == player_snapshot.spirit_entry) {
        return TrackedStatEntryKind::PlayerSpirit;
    }

    const ActorResolveSnapshot mount_snapshot = g_mount_resolve;
    if (entry == mount_snapshot.health_entry) {
        return TrackedStatEntryKind::MountHealth;
    }

    if (entry == mount_snapshot.stamina_entry) {
        return TrackedStatEntryKind::MountStamina;
    }

    if (entry == mount_snapshot.spirit_entry) {
        return TrackedStatEntryKind::MountSpirit;
    }

    return TrackedStatEntryKind::None;
}

int32_t ResolveTrackedStatType(const TrackedStatEntryKind kind) {
    switch (kind) {
    case TrackedStatEntryKind::PlayerHealth:
    case TrackedStatEntryKind::MountHealth:
        return kHealthId;
    case TrackedStatEntryKind::PlayerStamina:
    case TrackedStatEntryKind::MountStamina:
        return kStaminaId;
    case TrackedStatEntryKind::PlayerSpirit:
    case TrackedStatEntryKind::MountSpirit:
        return kSpiritId;
    default:
        return -1;
    }
}

bool IsPlayerTrackedStatEntry(const TrackedStatEntryKind kind) {
    return kind == TrackedStatEntryKind::PlayerHealth ||
           kind == TrackedStatEntryKind::PlayerStamina ||
           kind == TrackedStatEntryKind::PlayerSpirit;
}

bool IsMountTrackedStatEntry(const TrackedStatEntryKind kind) {
    return kind == TrackedStatEntryKind::MountHealth ||
           kind == TrackedStatEntryKind::MountStamina ||
           kind == TrackedStatEntryKind::MountSpirit;
}

bool IsPlayerRuntimeReady() {
    return g_player_resolve.valid();
}

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
