#include "runtime/affinity_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/runtime_state.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr int64_t kAffinityDeltaClamp = 100;
constexpr int64_t kAffinityValueClamp = 100;

bool TryScalePositiveAffinityDelta(const char* const path_name,
                                   const uintptr_t record,
                                   const int64_t old_value,
                                   const int64_t delta,
                                   int64_t* const new_value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || new_value == nullptr || !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto config = GetConfig();
    if (!config.general.enabled || config.affinity.multiplier == 1.0) {
        return false;
    }

    if (record < kMinimumPointerAddress || old_value < 0 || *new_value < 0) {
        return false;
    }

    if (old_value > kAffinityValueClamp || delta <= 0 || delta > kAffinityDeltaClamp) {
        return false;
    }

    const double scaled_delta = std::floor(static_cast<double>(delta) * config.affinity.multiplier);
    int64_t adjusted_delta = 0;
    if (scaled_delta <= 0.0) {
        adjusted_delta = 0;
    } else if (scaled_delta >= static_cast<double>(kAffinityDeltaClamp)) {
        adjusted_delta = kAffinityDeltaClamp;
    } else if (scaled_delta >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
        adjusted_delta = std::numeric_limits<int64_t>::max();
    } else {
        adjusted_delta = static_cast<int64_t>(scaled_delta);
    }

    int64_t adjusted_value = old_value + adjusted_delta;
    if (adjusted_value > kAffinityValueClamp) {
        adjusted_value = kAffinityValueClamp;
    }

    if (adjusted_value == *new_value) {
        return false;
    }

    *new_value = adjusted_value;

    const auto current = g_affinity_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: scaled affinity path=%s record=0x%p old=%lld delta=%lld final=%lld multiplier=%.3f",
            path_name,
            reinterpret_cast<void*>(record),
            old_value,
            delta,
            *new_value,
            config.affinity.multiplier);
    }

    return true;
}

}  // namespace

bool TryScaleAffinityGain(const uintptr_t record, const int64_t old_value, int64_t* const new_value) {
    if (new_value == nullptr || *new_value <= old_value) {
        return false;
    }

    const int64_t delta = *new_value - old_value;
    return TryScalePositiveAffinityDelta("prepare", record, old_value, delta, new_value);
}

bool TryScaleAffinityCurrentWrite(const uintptr_t record,
                                  const int64_t old_value,
                                  const int64_t pending_delta,
                                  int64_t* const new_value) {
    return TryScalePositiveAffinityDelta("current", record, old_value, pending_delta, new_value);
}
