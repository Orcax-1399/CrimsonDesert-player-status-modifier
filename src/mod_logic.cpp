#include "mod_logic.h"

#include "config.h"
#include "logger.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

namespace {

constexpr int32_t kHealthId = 0;
constexpr int32_t kStaminaId = 17;
constexpr int32_t kSpiritId = 18;
constexpr uintptr_t kMinimumPointerAddress = 0x10000000;
constexpr uintptr_t kBattleDamageReturnAddressRva = 0x1A50BB0;
constexpr uintptr_t kStaminaEntryOffsetFromHealth = 0x480;
constexpr uintptr_t kSpiritEntryOffsetFromHealth = 0x510;
constexpr uintptr_t kMountedDragonMarkerOffsetFromPlayerActor = 0xA20;
constexpr int64_t kMinimumDragonHealthMax = 1000000;
constexpr size_t kTrackedDamageParticipantCount = 16;
constexpr int kDamageRelationDepth = 6;

std::mutex g_state_mutex;
std::atomic<uintptr_t> g_player_actor{0};
std::atomic<uintptr_t> g_player_status_marker{0};
std::atomic<uintptr_t> g_player_stat_root{0};
std::atomic<uintptr_t> g_mount_actor{0};
std::atomic<uintptr_t> g_health_entry{0};
std::atomic<uintptr_t> g_stamina_entry{0};
std::atomic<uintptr_t> g_spirit_entry{0};
std::atomic<uintptr_t> g_mount_status_marker{0};
std::atomic<uintptr_t> g_mount_stat_root{0};
std::atomic<uintptr_t> g_mount_health_entry{0};
std::atomic<uintptr_t> g_mount_stamina_entry{0};
std::array<std::atomic<uintptr_t>, kTrackedDamageParticipantCount> g_tracked_damage_participants{};
std::atomic<uint32_t> g_tracked_damage_participant_cursor{0};
std::atomic<bool> g_runtime_enabled{true};
std::atomic<std::uint32_t> g_process_skip_logs{0};
std::atomic<std::uint32_t> g_process_apply_logs{0};
std::atomic<std::uint32_t> g_discovery_logs{0};
std::atomic<std::uint32_t> g_durability_logs{0};
std::atomic<std::uint32_t> g_damage_logs{0};
std::atomic<std::uint32_t> g_mount_logs{0};
std::atomic<std::uint32_t> g_mount_actor_mismatch_logs{0};

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

std::atomic<uintptr_t>* SelectEntrySlot(const int32_t stat_type) {
    switch (stat_type) {
    case kHealthId:
        return &g_health_entry;
    case kStaminaId:
        return &g_stamina_entry;
    case kSpiritId:
        return &g_spirit_entry;
    default:
        return nullptr;
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

bool TryResolveEntryFromHealthLayout(const uintptr_t entry, int32_t* const stat_type) {
    if (stat_type == nullptr) {
        return false;
    }

    const uintptr_t health_entry = g_health_entry.load(std::memory_order_acquire);
    if (health_entry < kMinimumPointerAddress) {
        return false;
    }

    if (entry == health_entry + kStaminaEntryOffsetFromHealth) {
        g_stamina_entry.store(entry, std::memory_order_release);
        *stat_type = kStaminaId;
        return true;
    }

    if (entry == health_entry + kSpiritEntryOffsetFromHealth) {
        g_spirit_entry.store(entry, std::memory_order_release);
        *stat_type = kSpiritId;
        return true;
    }

    return false;
}

uintptr_t TryResolveHealthEntryFromTrackedRoot() {
    const uintptr_t stat_root = g_player_stat_root.load(std::memory_order_acquire);
    if (stat_root < kMinimumPointerAddress) {
        return 0;
    }

    const uintptr_t health_entry = *reinterpret_cast<const uintptr_t*>(stat_root + 0x58);
    if (health_entry < kMinimumPointerAddress) {
        return 0;
    }

    if (*reinterpret_cast<const int32_t*>(health_entry) != kHealthId) {
        return 0;
    }

    g_health_entry.store(health_entry, std::memory_order_release);
    return health_entry;
}

void ResetTrackedEntriesLocked() {
    g_health_entry.store(0, std::memory_order_release);
    g_stamina_entry.store(0, std::memory_order_release);
    g_spirit_entry.store(0, std::memory_order_release);
    g_mount_actor.store(0, std::memory_order_release);
    g_mount_status_marker.store(0, std::memory_order_release);
    g_mount_stat_root.store(0, std::memory_order_release);
    g_mount_health_entry.store(0, std::memory_order_release);
    g_mount_stamina_entry.store(0, std::memory_order_release);
}

void ResetTrackedMountLocked() {
    g_mount_actor.store(0, std::memory_order_release);
    g_mount_status_marker.store(0, std::memory_order_release);
    g_mount_stat_root.store(0, std::memory_order_release);
    g_mount_health_entry.store(0, std::memory_order_release);
    g_mount_stamina_entry.store(0, std::memory_order_release);
}

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

bool TryResolveMountEntriesFromRoot(const uintptr_t root,
                                    uintptr_t* const health_entry,
                                    uintptr_t* const stamina_entry,
                                    uintptr_t* const marker) {
    if (health_entry == nullptr || stamina_entry == nullptr || marker == nullptr || root < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const uintptr_t candidate_marker = *reinterpret_cast<const uintptr_t*>(root);
        if (candidate_marker < kMinimumPointerAddress) {
            return false;
        }

        if (*reinterpret_cast<const uintptr_t*>(candidate_marker + 0x18) != root) {
            return false;
        }

        const uintptr_t candidate_health_entry = *reinterpret_cast<const uintptr_t*>(root + 0x58);
        if (!IsValidStatEntry(candidate_health_entry, kHealthId)) {
            return false;
        }

        const uintptr_t candidate_stamina_entry = candidate_health_entry + kStaminaEntryOffsetFromHealth;
        if (!IsValidStatEntry(candidate_stamina_entry, kStaminaId)) {
            return false;
        }

        *health_entry = candidate_health_entry;
        *stamina_entry = candidate_stamina_entry;
        *marker = candidate_marker;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryResolveMountEntriesFromMarker(const uintptr_t marker,
                                      uintptr_t* const stat_root,
                                      uintptr_t* const health_entry,
                                      uintptr_t* const stamina_entry) {
    if (stat_root == nullptr || health_entry == nullptr || stamina_entry == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    __try {
        const uintptr_t candidate_root = *reinterpret_cast<const uintptr_t*>(marker + 0x18);
        if (candidate_root < kMinimumPointerAddress) {
            return false;
        }

        const uintptr_t candidate_health_entry = *reinterpret_cast<const uintptr_t*>(candidate_root + 0x58);
        const uintptr_t candidate_stamina_entry = candidate_health_entry + kStaminaEntryOffsetFromHealth;
        if (!IsValidStatEntry(candidate_health_entry, kHealthId) || !IsValidStatEntry(candidate_stamina_entry, kStaminaId)) {
            return false;
        }

        const int64_t candidate_health_max = *reinterpret_cast<const int64_t*>(candidate_health_entry + 0x18);
        if (candidate_health_max < kMinimumDragonHealthMax) {
            return false;
        }

        *stat_root = candidate_root;
        *health_entry = candidate_health_entry;
        *stamina_entry = candidate_stamina_entry;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryResolveMountContext(const uintptr_t context_root_a,
                            const uintptr_t context_root_b,
                            uintptr_t* const resolved_root,
                            uintptr_t* const health_entry,
                            uintptr_t* const stamina_entry) {
    if (resolved_root == nullptr || health_entry == nullptr || stamina_entry == nullptr) {
        return false;
    }

    const uintptr_t player_root = g_player_stat_root.load(std::memory_order_acquire);
    const uintptr_t player_marker = g_player_status_marker.load(std::memory_order_acquire);
    const uintptr_t mount_root = g_mount_stat_root.load(std::memory_order_acquire);
    const uintptr_t mount_marker = g_mount_status_marker.load(std::memory_order_acquire);
    const uintptr_t mount_health_entry = g_mount_health_entry.load(std::memory_order_acquire);
    const uintptr_t mount_stamina_entry = g_mount_stamina_entry.load(std::memory_order_acquire);
    if (player_root < kMinimumPointerAddress || player_marker < kMinimumPointerAddress) {
        return false;
    }

    if (mount_root < kMinimumPointerAddress ||
        mount_marker < kMinimumPointerAddress ||
        mount_health_entry < kMinimumPointerAddress ||
        mount_stamina_entry < kMinimumPointerAddress) {
        return false;
    }

    if (mount_root == player_root ||
        mount_marker == player_marker ||
        context_root_a == player_root ||
        context_root_b == player_root) {
        return false;
    }

    *resolved_root = mount_root;
    *health_entry = mount_health_entry;
    *stamina_entry = mount_stamina_entry;
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

bool TryResolveMountActorCandidate(const uintptr_t marker,
                                   const uintptr_t actor_hint,
                                   uintptr_t* const resolved_actor,
                                   const char** const validation_mode) {
    if (resolved_actor == nullptr || validation_mode == nullptr || marker < kMinimumPointerAddress) {
        return false;
    }

    *resolved_actor = 0;
    *validation_mode = "unverified";

    uintptr_t marker_owner = 0;
    if (!TryReadPointer(marker + 0x8, &marker_owner) || marker_owner < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t resolved = 0;
    if (!TryReadPointer(marker_owner + 0x68, &resolved) || resolved < kMinimumPointerAddress) {
        return false;
    }

    if (actor_hint >= kMinimumPointerAddress && actor_hint != resolved) {
        const auto current = g_mount_actor_mismatch_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 8) {
            Log("runtime: mount actor hint mismatch marker=0x%p hint=0x%p resolved=0x%p",
                reinterpret_cast<void*>(marker),
                reinterpret_cast<void*>(actor_hint),
                reinterpret_cast<void*>(resolved));
        }
    }

    *resolved_actor = resolved;
    *validation_mode = "marker+8->68";
    return true;
}

void RefreshTrackedMountFromPlayerActor() {
    const uintptr_t player_actor = g_player_actor.load(std::memory_order_acquire);
    const uintptr_t player_marker = g_player_status_marker.load(std::memory_order_acquire);
    if (player_actor < kMinimumPointerAddress || player_marker < kMinimumPointerAddress) {
        return;
    }

    uintptr_t marker = 0;
    if (!TryReadPointer(player_actor + kMountedDragonMarkerOffsetFromPlayerActor, &marker)) {
        return;
    }

    if (marker < kMinimumPointerAddress || marker == player_marker) {
        const uintptr_t current_marker = g_mount_status_marker.load(std::memory_order_acquire);
        if (current_marker < kMinimumPointerAddress) {
            return;
        }

        std::lock_guard lock(g_state_mutex);
        if (g_mount_status_marker.load(std::memory_order_acquire) >= kMinimumPointerAddress) {
            ResetTrackedMountLocked();
        }
        return;
    }

    UpdateTrackedMountStatusComponent(0, marker);
}

uintptr_t GetModuleBaseAddress() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

uintptr_t TryGetTrackedPlayerTargetOwner() {
    const uintptr_t marker = g_player_status_marker.load(std::memory_order_acquire);
    if (marker < kMinimumPointerAddress) {
        return 0;
    }

    const uintptr_t target_owner = *reinterpret_cast<const uintptr_t*>(marker + 0x18);
    if (target_owner < kMinimumPointerAddress) {
        return 0;
    }

    return target_owner;
}

bool IsOutgoingPlayerDamageSource(const uintptr_t source_context) {
    if (source_context < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t source_actor = *reinterpret_cast<const uintptr_t*>(source_context + 0x68);
    if (source_actor < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t player_actor = g_player_actor.load(std::memory_order_acquire);
    if (player_actor >= kMinimumPointerAddress && source_actor == player_actor) {
        return true;
    }

    const uintptr_t mount_marker = g_mount_status_marker.load(std::memory_order_acquire);
    if (mount_marker < kMinimumPointerAddress) {
        return false;
    }

        const uintptr_t mount_actor = g_mount_actor.load(std::memory_order_acquire);
        if (mount_actor >= kMinimumPointerAddress && source_actor == mount_actor) {
            return true;
        }

        return *reinterpret_cast<const uintptr_t*>(source_actor + 0x20) == mount_marker;
}

void ResetTrackedDamageParticipantsLocked() {
    for (auto& participant : g_tracked_damage_participants) {
        participant.store(0, std::memory_order_release);
    }

    g_tracked_damage_participant_cursor.store(0, std::memory_order_release);
}

bool IsTrackedDamageParticipant(const uintptr_t candidate) {
    if (candidate < kMinimumPointerAddress) {
        return false;
    }

    const uintptr_t actor = g_player_actor.load(std::memory_order_acquire);
    if (actor >= kMinimumPointerAddress && candidate == actor) {
        return true;
    }

    const uintptr_t marker = g_player_status_marker.load(std::memory_order_acquire);
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

std::uint64_t SeedDurabilityRng(const uintptr_t entry) {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);

    return static_cast<std::uint64_t>(counter.QuadPart) ^ static_cast<std::uint64_t>(GetCurrentThreadId()) ^
           static_cast<std::uint64_t>(GetTickCount64()) ^ static_cast<std::uint64_t>(entry);
}

std::uint32_t NextDurabilityRoll(const uintptr_t entry) {
    thread_local std::uint64_t state = 0;
    if (state == 0) {
        state = SeedDurabilityRng(entry);
        if (state == 0) {
            state = 0x9E3779B97F4A7C15ull;
        }
    }

    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return static_cast<std::uint32_t>(state % 10000ull);
}

bool ShouldSkipDurabilityConsumption(const uintptr_t entry, const double chance) {
    if (chance <= 0.0) {
        return true;
    }

    if (chance >= 100.0) {
        return false;
    }

    const double roll = static_cast<double>(NextDurabilityRoll(entry)) / 100.0;
    return roll >= chance;
}

}  // namespace

void ResetRuntimeState() {
    std::lock_guard lock(g_state_mutex);
    g_player_actor.store(0, std::memory_order_release);
    g_player_status_marker.store(0, std::memory_order_release);
    g_player_stat_root.store(0, std::memory_order_release);
    ResetTrackedEntriesLocked();
    ResetTrackedDamageParticipantsLocked();
    g_runtime_enabled.store(true, std::memory_order_release);
}

void DisableRuntimeProcessing() {
    g_runtime_enabled.store(false, std::memory_order_release);
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

void UpdateTrackedMountStatusComponent(const uintptr_t actor, const uintptr_t marker) {
    if (marker < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t player_marker = g_player_status_marker.load(std::memory_order_acquire);
    if (marker == player_marker) {
        return;
    }

    uintptr_t stat_root = 0;
    uintptr_t health_entry = 0;
    uintptr_t stamina_entry = 0;
    if (!TryResolveMountEntriesFromMarker(marker, &stat_root, &health_entry, &stamina_entry)) {
        return;
    }

    uintptr_t resolved_actor = 0;
    const char* validation_mode = "unverified";
    if (!TryResolveMountActorCandidate(marker, actor, &resolved_actor, &validation_mode)) {
        return;
    }

    const uintptr_t current_actor = g_mount_actor.load(std::memory_order_acquire);
    const uintptr_t current_marker = g_mount_status_marker.load(std::memory_order_acquire);
    if (current_actor == resolved_actor && current_marker == marker) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (g_mount_actor.load(std::memory_order_acquire) == resolved_actor &&
        g_mount_status_marker.load(std::memory_order_acquire) == marker) {
        return;
    }

    g_mount_actor.store(resolved_actor, std::memory_order_release);
    g_mount_status_marker.store(marker, std::memory_order_release);
    g_mount_stat_root.store(stat_root, std::memory_order_release);
    g_mount_health_entry.store(health_entry, std::memory_order_release);
    g_mount_stamina_entry.store(stamina_entry, std::memory_order_release);

    Log("runtime: tracked mount actor=0x%p verify=%s root=0x%p status_marker=0x%p health=0x%p stamina=0x%p",
        reinterpret_cast<void*>(resolved_actor),
        validation_mode,
        reinterpret_cast<void*>(stat_root),
        reinterpret_cast<void*>(marker),
        reinterpret_cast<void*>(health_entry),
        reinterpret_cast<void*>(stamina_entry));
}

void UpdateTrackedPlayerStatusComponent(const uintptr_t actor, const uintptr_t component) {
    if (component < kMinimumPointerAddress) {
        return;
    }

    const auto current_marker = g_player_status_marker.load(std::memory_order_acquire);
    if (current_marker == component) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (g_player_status_marker.load(std::memory_order_acquire) == component) {
        return;
    }

    const uintptr_t stat_root = *reinterpret_cast<const uintptr_t*>(component + 0x18);

    g_player_actor.store(actor, std::memory_order_release);
    g_player_status_marker.store(component, std::memory_order_release);
    g_player_stat_root.store(stat_root, std::memory_order_release);
    ResetTrackedEntriesLocked();
    ResetTrackedDamageParticipantsLocked();

    Log("runtime: tracked player actor=0x%p status_marker=0x%p stat_root=0x%p",
        reinterpret_cast<void*>(actor),
        reinterpret_cast<void*>(component),
        reinterpret_cast<void*>(stat_root));

    RefreshTrackedMountFromPlayerActor();
}

uintptr_t GetTrackedPlayerStatRoot() {
    return g_player_stat_root.load(std::memory_order_acquire);
}

uintptr_t GetTrackedMountActor() {
    return g_mount_actor.load(std::memory_order_acquire);
}

uintptr_t GetTrackedMountStatRoot() {
    return g_mount_stat_root.load(std::memory_order_acquire);
}

uintptr_t GetTrackedMountStatusMarker() {
    return g_mount_status_marker.load(std::memory_order_acquire);
}

uintptr_t GetTrackedPlayerActor() {
    return g_player_actor.load(std::memory_order_acquire);
}

uintptr_t GetTrackedPlayerStatusMarker() {
    return g_player_status_marker.load(std::memory_order_acquire);
}

void ObserveStatEntry(const uintptr_t entry, const uintptr_t component) {
    if (!g_runtime_enabled.load(std::memory_order_acquire)) {
        return;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    const auto tracked_marker = g_player_status_marker.load(std::memory_order_acquire);
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

    auto* const entry_slot = SelectEntrySlot(stat_type);
    if (entry_slot == nullptr) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    if (*reinterpret_cast<const uintptr_t*>(component) != g_player_status_marker.load(std::memory_order_acquire)) {
        return;
    }

    const auto current_entry = entry_slot->load(std::memory_order_acquire);
    if (current_entry == entry) {
        return;
    }

    entry_slot->store(entry, std::memory_order_release);

    const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 16) {
        Log("runtime: discovered stat entry type=%d entry=0x%p current=%lld max=%lld",
            stat_type,
            reinterpret_cast<void*>(entry),
            static_cast<long long>(current_value),
            static_cast<long long>(max_value));
    }
}

bool TryAdjustStatWrite(const uintptr_t entry,
                        const bool player_context,
                        const uintptr_t context_root_a,
                        const uintptr_t context_root_b,
                        int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    RefreshTrackedMountFromPlayerActor();

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

    uintptr_t mount_root = 0;
    uintptr_t mount_health_entry = 0;
    uintptr_t mount_stamina_entry = 0;
    if (TryResolveMountContext(context_root_a, context_root_b, &mount_root, &mount_health_entry, &mount_stamina_entry)) {
        uintptr_t player_health_entry = g_health_entry.load(std::memory_order_acquire);
        if (player_health_entry < kMinimumPointerAddress) {
            player_health_entry = TryResolveHealthEntryFromTrackedRoot();
        }
        const uintptr_t player_stamina_entry = g_stamina_entry.load(std::memory_order_acquire);
        const uintptr_t player_spirit_entry = g_spirit_entry.load(std::memory_order_acquire);
        if (entry == player_health_entry || entry == player_stamina_entry || entry == player_spirit_entry) {
            return false;
        }

        const bool is_mount_health = entry == mount_health_entry && actual_type == kHealthId;
        const bool is_mount_stamina = entry == mount_stamina_entry && actual_type == kStaminaId;
        if (is_mount_health || is_mount_stamina) {
            bool should_lock = false;
            if (config.mount.enabled) {
                if (config.mount.lock_health && is_mount_health) {
                    should_lock = true;
                } else if (config.mount.lock_stamina && is_mount_stamina) {
                    should_lock = true;
                }
            }

            if (should_lock) {
                const int64_t locked_value = ClampToRange(config.mount.lock_value, 0, max_value);
                *value = locked_value;

                const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
                if (current < 32) {
                    Log("runtime: locked mount stat type=%d root=0x%p entry=0x%p old=%lld requested=%lld final=%lld max=%lld",
                        actual_type,
                        reinterpret_cast<void*>(mount_root),
                        reinterpret_cast<void*>(entry),
                        static_cast<long long>(old_value),
                        static_cast<long long>(requested_value),
                        static_cast<long long>(locked_value),
                        static_cast<long long>(max_value));
                }

                return locked_value != requested_value;
            }

            const auto current = g_mount_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 32) {
                Log("runtime: skipped mount stat write type=%d root=0x%p entry=0x%p old=%lld requested=%lld",
                    actual_type,
                    reinterpret_cast<void*>(mount_root),
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(old_value),
                    static_cast<long long>(requested_value));
            }
            return false;
        }
    }

    int32_t stat_type = -1;
    uintptr_t health_entry = g_health_entry.load(std::memory_order_acquire);
    if (health_entry < kMinimumPointerAddress) {
        health_entry = TryResolveHealthEntryFromTrackedRoot();
    }

    const uintptr_t stamina_entry = g_stamina_entry.load(std::memory_order_acquire);
    const uintptr_t spirit_entry = g_spirit_entry.load(std::memory_order_acquire);
    if (entry == health_entry) {
        stat_type = kHealthId;
    } else if (entry == stamina_entry) {
        stat_type = kStaminaId;
    } else if (entry == spirit_entry) {
        stat_type = kSpiritId;
    } else if (player_context) {
        if (IsTrackedStat(actual_type)) {
            stat_type = actual_type;

            if (auto* const entry_slot = SelectEntrySlot(stat_type); entry_slot != nullptr) {
                entry_slot->store(entry, std::memory_order_release);
            }

            const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
            if (current < 24) {
                Log("runtime: inferred stat entry from write context type=%d entry=0x%p",
                    stat_type,
                    reinterpret_cast<void*>(entry));
            }
        }
    } else if (TryResolveEntryFromHealthLayout(entry, &stat_type)) {
        const auto current = g_discovery_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: inferred stat entry from health layout type=%d entry=0x%p health=0x%p",
                stat_type,
                reinterpret_cast<void*>(entry),
                reinterpret_cast<void*>(health_entry));
        }
    } else {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped for unknown entry=0x%p", reinterpret_cast<void*>(entry));
        }
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

bool TryAdjustDurabilityWrite(const uintptr_t entry, uint16_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    const double chance = config.durability.consumption_chance;
    if (chance >= 100.0) {
        return false;
    }

    const uint16_t old_value = *reinterpret_cast<const uint16_t*>(entry + 0x50);
    const uint16_t requested_value = *value;
    if (requested_value >= old_value) {
        return false;
    }

    if (!ShouldSkipDurabilityConsumption(entry, chance)) {
        return false;
    }

    *value = old_value;

    const auto current = g_durability_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: skipped maintenance consumption entry=0x%p old=%u requested=%u chance=%.2f",
            reinterpret_cast<void*>(entry),
            static_cast<unsigned>(old_value),
            static_cast<unsigned>(requested_value),
            chance);
    }

    return true;
}

bool TryAdjustDurabilityDelta(const uintptr_t entry, const uint16_t current_value, int16_t* const delta) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || delta == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    if (*delta >= 0) {
        return false;
    }

    const double chance = config.durability.consumption_chance;
    if (chance >= 100.0) {
        return false;
    }

    if (!ShouldSkipDurabilityConsumption(entry, chance)) {
        return false;
    }

    const int16_t requested_delta = *delta;
    *delta = 0;

    const auto current = g_durability_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: skipped durability consumption entry=0x%p current=%u delta=%d chance=%.2f",
            reinterpret_cast<void*>(entry),
            static_cast<unsigned>(current_value),
            static_cast<int>(requested_delta),
            chance);
    }

    return true;
}
