#include "hooks/hooks_internal.h"

#include "logger.h"
#include "runtime/affinity_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

std::atomic<bool> g_reported_affinity_current_exception{false};
std::atomic<std::uint32_t> g_affinity_current_samples{0};

template <std::size_t N>
bool ExpectBytes(const uintptr_t address, const std::array<std::uint8_t, N>& bytes) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), bytes.data(), bytes.size()) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryStoreAffinityValue(const uintptr_t address, const int64_t value) {
    if (address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *reinterpret_cast<int64_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void AffinityPrepareCallback(SafetyHookContext& ctx) {
    const uintptr_t record = ctx.rdi;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        const uintptr_t value_address = record + 0x10;
        const int64_t old_value = static_cast<int64_t>(ctx.r12);
        int64_t new_value = *reinterpret_cast<const int64_t*>(value_address);
            if (!TryScaleAffinityGain(record, old_value, &new_value)) {
                return;
            }

            if (!TryStoreAffinityValue(value_address, new_value)) {
                return;
            }

            if (ShouldLogSample(g_affinity_samples, 24)) {
                Log("hooks: affinity-prepare scaled record=0x%p old=%lld final=%lld rip=0x%p",
                    reinterpret_cast<void*>(record),
                    old_value,
                    new_value,
                    reinterpret_cast<void*>(ctx.rip));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (!g_reported_affinity_exception.exchange(true, std::memory_order_acq_rel)) {
                Log("hooks: exception 0x%08lX inside affinity-prepare hook", GetExceptionCode());
            }
        }
}

void AffinityCurrentCallback(SafetyHookContext& ctx) {
    const uintptr_t record = ctx.rbx;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        const int64_t old_value = *reinterpret_cast<const int64_t*>(record + 0x48);
        const int64_t pending_delta = *reinterpret_cast<const int64_t*>(record + 0x28);
        int64_t new_value = static_cast<int64_t>(ctx.rax);

        if (!TryScaleAffinityCurrentWrite(record, old_value, pending_delta, &new_value)) {
            return;
        }

        ctx.rax = static_cast<uintptr_t>(new_value);
        if (ShouldLogSample(g_affinity_current_samples, 24)) {
            Log("hooks: affinity-current scaled record=0x%p old=%lld delta=%lld final=%lld rip=0x%p",
                reinterpret_cast<void*>(record),
                old_value,
                pending_delta,
                new_value,
                reinterpret_cast<void*>(ctx.rip));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_current_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity-current hook", GetExceptionCode());
        }
    }
}

bool InstallAffinityMidHook(const uintptr_t target,
                            const char* const name,
                            SafetyHookMid* const storage,
                            void (*callback)(SafetyHookContext&)) {
    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), callback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create %s mid hook", name);
        return false;
    }

    *storage = std::move(*hook_result);
    Log("hooks: installed %s hook at 0x%p", name, reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallAffinityHooks() {
    static constexpr std::array<std::uint8_t, 8> kPrepareBytes = {
        0x48, 0x8B, 0x47, 0x10, 0x48, 0x89, 0x45, 0x18,
    };
    static constexpr std::array<std::uint8_t, 20> kCurrentBytes = {
        0x48, 0x89, 0x43, 0x48, 0x41, 0x8B, 0xCD, 0x83, 0xE9, 0x01,
        0x74, 0x0A, 0x83, 0xF9, 0x01, 0x75, 0x09, 0x88, 0x4B, 0x3F,
    };

    RemoveAffinityHooks();

    bool installed_any = false;

    const uintptr_t prepare_target = ScanForAffinityGainPrepare();
    if (prepare_target != 0) {
        if (!ExpectBytes(prepare_target, kPrepareBytes)) {
            Log("hooks: affinity-prepare target did not match expected bytes target=0x%p",
                reinterpret_cast<void*>(prepare_target));
        } else if (InstallAffinityMidHook(prepare_target, "affinity-prepare", &g_affinity_hook, AffinityPrepareCallback)) {
            installed_any = true;
        }
    }

    const uintptr_t current_target = ScanForAffinityCurrentWrite();
    if (current_target != 0) {
        if (!ExpectBytes(current_target, kCurrentBytes)) {
            Log("hooks: affinity-current target did not match expected bytes target=0x%p",
                reinterpret_cast<void*>(current_target));
        } else if (InstallAffinityMidHook(current_target, "affinity-current", &g_affinity_current_hook, AffinityCurrentCallback)) {
            installed_any = true;
        }
    }

    if (!installed_any) {
        RemoveAffinityHooks();
    }

    return installed_any;
}

void RemoveAffinityHooks() {
    if (g_affinity_hook) {
        g_affinity_hook.reset();
        Log("hooks: removed affinity-prepare hook");
    }

    if (g_affinity_current_hook) {
        g_affinity_current_hook.reset();
        Log("hooks: removed affinity-current hook");
    }
}
