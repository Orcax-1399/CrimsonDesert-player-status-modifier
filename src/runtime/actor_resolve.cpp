#include "runtime/actor_resolve.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

bool IsValidStatEntry(const uintptr_t entry, const int32_t expected_type) {
    if (entry < kMinimumPointerAddress) {
        return false;
    }

    if (*reinterpret_cast<const int32_t*>(entry) != expected_type) {
        return false;
    }

    const int64_t current_value = *reinterpret_cast<const int64_t*>(entry + 0x08);
    const int64_t max_value = *reinterpret_cast<const int64_t*>(entry + 0x18);
    return max_value > 0 && current_value >= 0 && current_value <= max_value;
}

bool TryResolveActorFromMarker(const uintptr_t marker, uintptr_t* const actor) {
    if (actor == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker_owner = 0;
    if (!TryReadPointer(marker + 0x8, &marker_owner) || marker_owner < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t resolved_actor = 0;
    if (!TryReadPointer(marker_owner + 0x68, &resolved_actor) || resolved_actor < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t confirmed_marker = 0;
    if (!TryReadPointer(resolved_actor + 0x20, &confirmed_marker) || confirmed_marker != marker) {
        return false;
    }

    *actor = resolved_actor;
    return true;
}

}  // namespace

bool SelectConfig(const ModConfig& config, const int32_t stat_type, StatConfig* const selected) {
    if (selected == nullptr) {
        return false;
    }

    switch (stat_type) {
    case kHealthId:
        *selected = config.health;
        return true;
    case kStaminaId:
        *selected = config.stamina;
        return true;
    case kSpiritId:
        *selected = config.spirit;
        return true;
    default:
        return false;
    }
}

int64_t ClampToRange(const int64_t value, const int64_t minimum, const int64_t maximum) {
    return std::max(minimum, std::min(value, maximum));
}

int64_t ScaleDelta(const int64_t delta, const double multiplier) {
    const double scaled = std::floor(static_cast<double>(delta) * multiplier);
    if (scaled <= 0.0) {
        return 0;
    }

    if (scaled >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return std::numeric_limits<int64_t>::max();
    }

    return static_cast<int64_t>(scaled);
}

bool IsTrackedStat(const int32_t stat_type) {
    return stat_type == kHealthId || stat_type == kStaminaId || stat_type == kSpiritId;
}

bool TryAssignPlayerResolvedEntry(const uintptr_t entry, const int32_t stat_type) {
    if (entry < kMinimumPointerAddress || !IsTrackedStat(stat_type)) {
        return false;
    }

    auto resolved = g_player_resolve;
    if (!resolved.valid()) {
        return false;
    }

    bool changed = false;
    switch (stat_type) {
    case kHealthId:
        if (resolved.health_entry != entry) {
            resolved.health_entry = entry;
            changed = true;
        }
        break;
    case kStaminaId:
        if (resolved.stamina_entry != entry) {
            resolved.stamina_entry = entry;
            changed = true;
        }
        break;
    case kSpiritId:
        if (resolved.spirit_entry != entry) {
            resolved.spirit_entry = entry;
            changed = true;
        }
        break;
    default:
        break;
    }

    if (!changed) {
        return false;
    }

    g_player_resolve = resolved;
    return true;
}

bool TryReadPointer(const uintptr_t address, uintptr_t* const value) {
    if (value == nullptr || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryResolveActorResolveFromMarker(const uintptr_t marker,
                                      ActorResolveSnapshot* const resolved,
                                      const uintptr_t actor_hint) {
    if (resolved == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t root = 0;
    if (!TryReadPointer(marker + 0x18, &root) || root < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t root_marker = 0;
    if (!TryReadPointer(root, &root_marker) || root_marker != marker) {
        return false;
    }

    uintptr_t health_entry = 0;
    if (!TryReadPointer(root + 0x58, &health_entry) || !IsValidStatEntry(health_entry, kHealthId)) {
        return false;
    }

    const uintptr_t stamina_entry = health_entry + kStaminaEntryOffsetFromHealth;
    if (!IsValidStatEntry(stamina_entry, kStaminaId)) {
        return false;
    }

    uintptr_t spirit_entry = 0;
    const uintptr_t candidate_spirit_entry = health_entry + kSpiritEntryOffsetFromHealth;
    if (IsValidStatEntry(candidate_spirit_entry, kSpiritId)) {
        spirit_entry = candidate_spirit_entry;
    }

    uintptr_t resolved_actor = 0;
    if (actor_hint >= kMinimumPointerAddress) {
        uintptr_t hint_marker = 0;
        if (TryReadPointer(actor_hint + 0x20, &hint_marker) && hint_marker == marker) {
            resolved_actor = actor_hint;
        }
    }

    if (resolved_actor < kMinimumPointerAddress && !TryResolveActorFromMarker(marker, &resolved_actor)) {
        return false;
    }

    resolved->actor = resolved_actor;
    resolved->marker = marker;
    resolved->root = root;
    resolved->health_entry = health_entry;
    resolved->stamina_entry = stamina_entry;
    resolved->spirit_entry = spirit_entry;
    resolved->damage_source = resolved_actor;
    resolved->damage_target = root;
    return true;
}

bool TryResolveActorResolveFromActor(const uintptr_t actor, ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || actor < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryReadPointer(actor + 0x20, &marker) || marker < kMinimumPointerAddress) {
        return false;
    }

    return TryResolveActorResolveFromMarker(marker, resolved, actor);
}

bool TryResolveActorResolveFromRoot(const uintptr_t root, ActorResolveSnapshot* const resolved) {
    if (resolved == nullptr || root < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t marker = 0;
    if (!TryReadPointer(root, &marker) || marker < kMinimumPointerAddress) {
        return false;
    }

    return TryResolveActorResolveFromMarker(marker, resolved);
}

bool TryResolveActorResolveFromContextRoot(const uintptr_t context_root,
                                          ActorResolveSnapshot* const resolved,
                                          const ActorResolveSnapshot& player_snapshot) {
    if (!TryResolveActorResolveFromRoot(context_root, resolved)) {
        return false;
    }

    if (!resolved->valid()) {
        return false;
    }

    if (player_snapshot.valid() &&
        (resolved->marker == player_snapshot.marker || resolved->root == player_snapshot.root)) {
        return false;
    }

    return true;
}
