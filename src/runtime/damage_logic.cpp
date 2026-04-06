#include "runtime/damage_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

uintptr_t GetModuleBaseAddress() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

uintptr_t TryGetTrackedPlayerTargetOwner() {
    return g_player_resolve.damage_target;
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

bool TryScalePlayerDamage(const uintptr_t target,
                          const int32_t status_id,
                          const uintptr_t return_address,
                          const uintptr_t source_context,
                          int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled) {
        return false;
    }

    if (status_id != kHealthId || *value >= 0) {
        return false;
    }

    const uintptr_t module_base = GetModuleBaseAddress();
    if (module_base < kMinimumPointerAddress || return_address != module_base + kBattleDamageReturnAddressRva) {
        return false;
    }

    DamageChannelConfig channel{};
    const char* direction = nullptr;

    const uintptr_t player_target_owner = TryGetTrackedPlayerTargetOwner();
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

    const auto current = g_damage_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: scaled damage direction=%s target=0x%p sourceCtx=0x%p ret=0x%p final=%lld multiplier=%.3f",
            direction,
            reinterpret_cast<void*>(target),
            reinterpret_cast<void*>(source_context),
            reinterpret_cast<void*>(return_address),
            static_cast<long long>(*value),
            channel.multiplier);
    }

    return true;
}
