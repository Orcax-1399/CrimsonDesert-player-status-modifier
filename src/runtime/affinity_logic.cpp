#include "runtime/affinity_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr uintptr_t kAffinityGainReturnAddressRva = 0x5B3567;
constexpr int32_t kAffinityValueClamp = 100;

uintptr_t GetModuleBaseAddress() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
}

}  // namespace

bool TryScaleAffinityGain(const uintptr_t return_address, const uintptr_t record, int32_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr) {
        return false;
    }

    const auto config = GetConfig();
    if (!config.general.enabled || config.affinity.multiplier == 1.0) {
        return false;
    }

    const uintptr_t module_base = GetModuleBaseAddress();
    if (module_base < kMinimumPointerAddress || return_address != module_base + kAffinityGainReturnAddressRva) {
        return false;
    }

    if (record < kMinimumPointerAddress || *value <= 0) {
        return false;
    }

    const double scaled = std::floor(static_cast<double>(*value) * config.affinity.multiplier);
    int32_t adjusted = 0;
    if (scaled <= 0.0) {
        adjusted = 0;
    } else if (scaled >= static_cast<double>(kAffinityValueClamp)) {
        adjusted = kAffinityValueClamp;
    } else if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max())) {
        adjusted = std::numeric_limits<int32_t>::max();
    } else {
        adjusted = static_cast<int32_t>(scaled);
    }

    if (adjusted == *value) {
        return false;
    }

    *value = adjusted;

    const auto current = g_affinity_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        const auto record_id = *reinterpret_cast<const uint32_t*>(record);
        const auto group_key = *reinterpret_cast<const uint16_t*>(record + 4);
        Log("runtime: scaled affinity record=0x%p id=%u group=%u final=%d multiplier=%.3f ret=0x%p",
            reinterpret_cast<void*>(record),
            record_id,
            static_cast<unsigned>(group_key),
            *value,
            config.affinity.multiplier,
            reinterpret_cast<void*>(return_address));
    }

    return true;
}
