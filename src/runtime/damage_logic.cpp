#include "runtime/damage_logic.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

uintptr_t TryGetTrackedPlayerTargetOwner() {
    return g_player_resolve.damage_target;
}

uintptr_t TryGetTrackedMountTargetOwner() {
    return g_mount_resolve.damage_target;
}

bool IsTrackedDamageParticipant(const uintptr_t candidate) {
    if (candidate < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t actor = g_player_resolve.actor;
    if (actor >= kMinimumPointerAddress && candidate == actor) {
        return true;
    }

    const uintptr_t marker = g_player_resolve.marker;
    if (marker >= kMinimumPointerAddress && candidate == marker) {
        return true;
    }

    for (const auto& participant : g_tracked_damage_participants) {
        if (participant.load(std::memory_order_acquire) == candidate) {
            return true;
        }
    }

    return false;
}

void TrackDamageParticipant(const uintptr_t candidate) {
    if (candidate < kMinimumPointerAddress || IsTrackedDamageParticipant(candidate)) {
        return;
    }

    const uint32_t index =
        g_tracked_damage_participant_cursor.fetch_add(1, std::memory_order_acq_rel) % kTrackedDamageParticipantCount;
    g_tracked_damage_participants[index].store(candidate, std::memory_order_release);
}

bool IsRelatedDamageParticipant(const uintptr_t candidate, const int depth) {
    if (candidate < kMinimumPointerAddress || depth < 0) {
        return false;
    }

    if (IsTrackedDamageParticipant(candidate)) {
        return true;
    }

    std::array<uintptr_t, 8> related{};
    for (size_t index = 0; index < related.size(); ++index) {
        const uintptr_t nested = *reinterpret_cast<const uintptr_t*>(candidate + index * sizeof(uintptr_t));
        related[index] = nested;
        if (IsTrackedDamageParticipant(nested)) {
            return true;
        }
    }

    if (depth == 0) {
        return false;
    }

    for (size_t index = 0; index < related.size(); ++index) {
        const uintptr_t nested = related[index];
        if (nested < kMinimumPointerAddress || nested == candidate) {
            continue;
        }

        bool already_seen = false;
        for (size_t previous = 0; previous < index; ++previous) {
            if (related[previous] == nested) {
                already_seen = true;
                break;
            }
        }

        if (!already_seen && IsRelatedDamageParticipant(nested, depth - 1)) {
            return true;
        }
    }

    return false;
}

bool IsOutgoingPlayerDamageSource(const uintptr_t source_context) {
    if (source_context < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t source_actor = *reinterpret_cast<const uintptr_t*>(source_context + 0x68);
    if (source_actor < kMinimumPointerAddress) {
        return false;
    }

    TrackDamageParticipant(source_actor);

    const uintptr_t player_actor = g_player_resolve.damage_source;
    if (player_actor >= kMinimumPointerAddress && source_actor == player_actor) {
        return true;
    }

    const uintptr_t mount_marker = g_mount_resolve.marker;
    if (mount_marker < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t mount_actor = g_mount_resolve.damage_source;
    if (mount_actor >= kMinimumPointerAddress && source_actor == mount_actor) {
        TrackDamageParticipant(mount_actor);
        return true;
    }

    const uintptr_t source_marker = *reinterpret_cast<const uintptr_t*>(source_actor + 0x20);
    if (source_marker >= kMinimumPointerAddress) {
        TrackDamageParticipant(source_marker);
    }

    if (source_marker == mount_marker) {
        return true;
    }

    return IsRelatedDamageParticipant(source_context, kDamageRelationDepth);
}

}  // namespace

bool IsRelevantDamageEvent(const uintptr_t target,
                           const int32_t status_id,
                           const uintptr_t source_context,
                           const int64_t delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) ||
        !IsPlayerRuntimeReady() ||
        status_id != kHealthId ||
        delta == 0) {
        return false;
    }

    const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
    if (target >= kMinimumPointerAddress &&
        player_target_owner >= kMinimumPointerAddress &&
        target == player_target_owner) {
        return true;
    }

    const uintptr_t mount_target_owner = TryGetTrackedMountTargetOwner();
    if (target >= kMinimumPointerAddress &&
        mount_target_owner >= kMinimumPointerAddress &&
        target == mount_target_owner) {
        return true;
    }

    return IsOutgoingPlayerDamageSource(source_context);
}

bool TryScalePlayerDamage(const uintptr_t target,
                          const int32_t status_id,
                          const uintptr_t return_address,
                          const uintptr_t source_context,
                          int64_t* const value) {
    static_cast<void>(return_address);

    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr || !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || status_id != kHealthId || *value == 0) {
        return false;
    }

    UpdateTrackedMountFromHealthRoot(target);

    const uintptr_t mount_target_owner = TryGetTrackedMountTargetOwner();
    if (config.mount.enabled &&
        config.mount.lock_health &&
        *value < 0 &&
        target >= kMinimumPointerAddress &&
        mount_target_owner >= kMinimumPointerAddress &&
        target == mount_target_owner) {
        if (*value == std::numeric_limits<int64_t>::min()) {
            *value = std::numeric_limits<int64_t>::max();
        } else {
            *value = -*value;
        }

        g_damage_logs.fetch_add(1, std::memory_order_acq_rel);
        Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p final=%lld",
            "mount-lock-health",
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(source_context),
            static_cast<long long>(*value));
        return true;
    }

    const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
    if (target >= kMinimumPointerAddress &&
        player_target_owner >= kMinimumPointerAddress &&
        target == player_target_owner &&
        *value > 0) {
        if (config.health.heal_multiplier == 1.0) {
            return false;
        }

        const double scaled = std::floor(static_cast<double>(*value) * config.health.heal_multiplier);
        if (scaled <= 0.0) {
            *value = 0;
        } else if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
            *value = std::numeric_limits<int64_t>::max();
        } else {
            *value = static_cast<int64_t>(scaled);
        }

        g_damage_logs.fetch_add(1, std::memory_order_acq_rel);
        Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p final=%lld multiplier=%.3f",
            "incoming-heal",
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(source_context),
            static_cast<long long>(*value),
            config.health.heal_multiplier);

        return true;
    }

    if (*value > 0) {
        return false;
    }

    DamageChannelConfig channel{};
    const char* direction = nullptr;
    if (target >= kMinimumPointerAddress && player_target_owner >= kMinimumPointerAddress && target == player_target_owner) {
        channel = config.damage.incoming;
        direction = "incoming-damage";
    } else if (IsOutgoingPlayerDamageSource(source_context)) {
        channel = config.damage.outgoing;
        direction = "outgoing-damage";
    } else {
        return false;
    }

    if (!channel.enabled || channel.multiplier == 1.0) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(*value) * channel.multiplier);
    if (scaled <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
        *value = std::numeric_limits<int64_t>::min();
    } else if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        *value = std::numeric_limits<int64_t>::max();
    } else {
        *value = static_cast<int64_t>(scaled);
    }

    g_damage_logs.fetch_add(1, std::memory_order_acq_rel);
    Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p final=%lld multiplier=%.3f",
        direction,
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(source_context),
        static_cast<long long>(*value),
        channel.multiplier);

    return true;
}
