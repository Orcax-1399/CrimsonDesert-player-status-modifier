#include "runtime/durability_logic.h"

#include "config.h"
#include "logger.h"
#include "runtime/runtime_state.h"

#include <Windows.h>

#include <cstdint>

namespace {

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

bool TryAdjustDurabilityWrite(const uintptr_t entry, uint16_t* const value) {
    if (!g_runtime_enabled.load(std::memory_order_acquire) || value == nullptr || !IsPlayerRuntimeReady()) {
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
    if (!g_runtime_enabled.load(std::memory_order_acquire) || delta == nullptr || !IsPlayerRuntimeReady()) {
        return false;
    }

    const auto& config = GetConfig();
    if (!config.general.enabled || entry < kMinimumPointerAddress) {
        return false;
    }

    const double chance = config.durability.consumption_chance;
    if (chance >= 100.0 || *delta >= 0) {
        return false;
    }

    if (!ShouldSkipDurabilityConsumption(entry, chance)) {
        return false;
    }

    const int16_t original_delta = *delta;
    *delta = 0;

    const auto current = g_durability_logs.fetch_add(1, std::memory_order_acq_rel);
    if (current < 24) {
        Log("runtime: skipped durability delta entry=0x%p current=%u original=%d chance=%.2f",
            reinterpret_cast<void*>(entry),
            static_cast<unsigned>(current_value),
            static_cast<int>(original_delta),
            chance);
    }

    return true;
}
