#include "mod_logic.h"

#include "config.h"
#include "logger.h"

#include <Windows.h>

#include <algorithm>
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

std::mutex g_state_mutex;
uintptr_t g_player_actor = 0;
std::atomic<uintptr_t> g_player_status_marker{0};
std::atomic<uintptr_t> g_health_entry{0};
std::atomic<uintptr_t> g_stamina_entry{0};
std::atomic<uintptr_t> g_spirit_entry{0};
std::atomic<bool> g_runtime_enabled{true};
std::atomic<std::uint32_t> g_process_skip_logs{0};
std::atomic<std::uint32_t> g_process_apply_logs{0};
std::atomic<std::uint32_t> g_discovery_logs{0};

const StatConfig* SelectConfig(const int32_t stat_type) {
    const auto& config = GetConfig();
    switch (stat_type) {
    case kHealthId:
        return &config.health;
    case kStaminaId:
        return &config.stamina;
    case kSpiritId:
        return &config.spirit;
    default:
        return nullptr;
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

void ResetTrackedEntriesLocked() {
    g_health_entry.store(0, std::memory_order_release);
    g_stamina_entry.store(0, std::memory_order_release);
    g_spirit_entry.store(0, std::memory_order_release);
}

}  // namespace

void ResetRuntimeState() {
    std::lock_guard lock(g_state_mutex);
    g_player_actor = 0;
    g_player_status_marker.store(0, std::memory_order_release);
    ResetTrackedEntriesLocked();
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

bool TryScalePlayerDamage(const uintptr_t source_component, const int32_t slot, uintptr_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || config.damage.multiplier == 1.0) {
        return false;
    }

    if (source_component < kMinimumPointerAddress || slot != 3) {
        return false;
    }

    const auto tracked_marker = g_player_status_marker.load(std::memory_order_acquire);
    if (tracked_marker < kMinimumPointerAddress) {
        return false;
    }

    const auto source_marker = *reinterpret_cast<const uintptr_t*>(source_component);
    if (source_marker != tracked_marker) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(*value) * config.damage.multiplier);
    if (scaled < 0.0) {
        *value = 0;
        return true;
    }

    if (scaled >= static_cast<double>(std::numeric_limits<uintptr_t>::max())) {
        *value = std::numeric_limits<uintptr_t>::max();
        return true;
    }

    *value = static_cast<uintptr_t>(scaled);
    return true;
}

void UpdateTrackedPlayerStatusComponent(const uintptr_t actor, const uintptr_t component) {
    if (component < kMinimumPointerAddress) {
        return;
    }

    std::lock_guard lock(g_state_mutex);
    const auto current_marker = g_player_status_marker.load(std::memory_order_acquire);
    if (current_marker == component) {
        return;
    }

    g_player_actor = actor;
    g_player_status_marker.store(component, std::memory_order_release);
    ResetTrackedEntriesLocked();

    Log("runtime: tracked player actor=0x%p status_marker=0x%p",
        reinterpret_cast<void*>(actor),
        reinterpret_cast<void*>(component));
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
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: stats marker mismatch tracked=0x%p actual=0x%p component=0x%p entry=0x%p",
                reinterpret_cast<void*>(tracked_marker),
                reinterpret_cast<void*>(component_marker),
                reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(entry));
        }
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

bool TryAdjustStatWrite(const uintptr_t entry, int64_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    int32_t stat_type = -1;
    const uintptr_t health_entry = g_health_entry.load(std::memory_order_acquire);
    const uintptr_t stamina_entry = g_stamina_entry.load(std::memory_order_acquire);
    const uintptr_t spirit_entry = g_spirit_entry.load(std::memory_order_acquire);
    if (entry == health_entry) {
        stat_type = kHealthId;
    } else if (entry == stamina_entry) {
        stat_type = kStaminaId;
    } else if (entry == spirit_entry) {
        stat_type = kSpiritId;
    } else {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped for unknown entry=0x%p", reinterpret_cast<void*>(entry));
        }
        return false;
    }

    if (*reinterpret_cast<const int32_t*>(entry) != stat_type) {
        const auto current = g_process_skip_logs.fetch_add(1, std::memory_order_acq_rel);
        if (current < 24) {
            Log("runtime: write skipped, type mismatch entry=0x%p expected=%d actual=%d",
                reinterpret_cast<void*>(entry),
                stat_type,
                *reinterpret_cast<const int32_t*>(entry));
        }
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

    const auto* const stat_config = SelectConfig(stat_type);
    if (stat_config == nullptr) {
        return false;
    }

    int64_t adjusted_value = requested_value;
    const int64_t delta = requested_value - old_value;
    if (delta == 0) {
        return false;
    }

    if (delta < 0) {
        const int64_t consumed = -delta;
        const int64_t target_consumption = ScaleDelta(consumed, stat_config->consumption_multiplier);
        const int64_t adjustment = consumed - target_consumption;
        adjusted_value = ClampToRange(requested_value + adjustment, 0, max_value);
    } else {
        const int64_t healed = delta;
        const int64_t target_heal = ScaleDelta(healed, stat_config->heal_multiplier);
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
