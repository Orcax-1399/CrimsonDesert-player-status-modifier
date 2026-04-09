#include "hooks/hooks_internal.h"

#include "logger.h"
#include "runtime/affinity_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <cstring>
#include <utility>

namespace {

constexpr uintptr_t kAffinityReturnStackOffset = 0x88;
constexpr uintptr_t kAffinityExistingWriteOffset = 0x14D;
constexpr uintptr_t kAffinityAppendWriteOffset = 0x1A2;
constexpr uintptr_t kAffinitySingleWriteOffset = 0x1EC;

enum class AffinityWritePath {
    Existing,
    Append,
    Single,
};

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

uintptr_t ReadAffinityReturnAddress(const SafetyHookContext& ctx) {
    __try {
        return *reinterpret_cast<const uintptr_t*>(ctx.rsp + kAffinityReturnStackOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* DescribeAffinityWritePath(const AffinityWritePath path) {
    switch (path) {
    case AffinityWritePath::Existing:
        return "existing";
    case AffinityWritePath::Append:
        return "append";
    case AffinityWritePath::Single:
        return "single";
    default:
        return "unknown";
    }
}

bool ScaleAffinityRegisterValue(const AffinityWritePath path,
                                const uintptr_t gated_return,
                                const uintptr_t record,
                                uint32_t* const value) {
    if (value == nullptr) {
        return false;
    }

    int32_t adjusted = static_cast<int32_t>(*value);
    if (!TryScaleAffinityGain(gated_return, record, &adjusted)) {
        return false;
    }

    *value = static_cast<uint32_t>(adjusted);
    if (ShouldLogSample(g_affinity_samples, 24)) {
        Log("hooks: affinity scaled path=%s record=0x%p final=%d ret=0x%p",
            DescribeAffinityWritePath(path),
            reinterpret_cast<void*>(record),
            adjusted,
            reinterpret_cast<void*>(gated_return));
    }

    return true;
}

void AffinityExistingCallback(SafetyHookContext& ctx) {
    const uintptr_t gated_return = ReadAffinityReturnAddress(ctx);
    const uintptr_t record = ctx.rdx;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        ScaleAffinityRegisterValue(AffinityWritePath::Existing, gated_return, record, &ctx.xmm1.u32[0]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity existing hook", GetExceptionCode());
        }
    }
}

void AffinityAppendCallback(SafetyHookContext& ctx) {
    const uintptr_t gated_return = ReadAffinityReturnAddress(ctx);
    const uintptr_t record = ctx.rax + ctx.rcx * 8;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        ScaleAffinityRegisterValue(AffinityWritePath::Append, gated_return, record, &ctx.xmm1.u32[0]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity append hook", GetExceptionCode());
        }
    }
}

void AffinitySingleCallback(SafetyHookContext& ctx) {
    const uintptr_t gated_return = ReadAffinityReturnAddress(ctx);
    const uintptr_t record = ctx.rax;
    if (record < kMinimumPointerAddress) {
        return;
    }

    __try {
        ScaleAffinityRegisterValue(AffinityWritePath::Single, gated_return, record, &ctx.xmm2.u32[0]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_affinity_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside affinity single hook", GetExceptionCode());
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
    static constexpr std::array<std::uint8_t, 8> kExistingBytes = {
        0x0F, 0x11, 0x4A, 0x10, 0x0F, 0x10, 0x47, 0x20,
    };
    static constexpr std::array<std::uint8_t, 9> kAppendBytes = {
        0x0F, 0x11, 0x4C, 0xC8, 0x10, 0x0F, 0x10, 0x47, 0x20,
    };
    static constexpr std::array<std::uint8_t, 8> kSingleBytes = {
        0x0F, 0x11, 0x50, 0x10, 0x0F, 0x11, 0x58, 0x20,
    };

    const uintptr_t entry = ScanForAffinityGainCommit();
    if (entry == 0) {
        return false;
    }

    const uintptr_t existing_target = entry + kAffinityExistingWriteOffset;
    const uintptr_t append_target = entry + kAffinityAppendWriteOffset;
    const uintptr_t single_target = entry + kAffinitySingleWriteOffset;
    if (!ExpectBytes(existing_target, kExistingBytes) ||
        !ExpectBytes(append_target, kAppendBytes) ||
        !ExpectBytes(single_target, kSingleBytes)) {
        Log("hooks: affinity write targets did not match expected bytes entry=0x%p", reinterpret_cast<void*>(entry));
        return false;
    }

    RemoveAffinityHooks();

    if (!InstallAffinityMidHook(existing_target, "affinity-existing", &g_affinity_existing_hook, AffinityExistingCallback) ||
        !InstallAffinityMidHook(append_target, "affinity-append", &g_affinity_append_hook, AffinityAppendCallback) ||
        !InstallAffinityMidHook(single_target, "affinity-single", &g_affinity_single_hook, AffinitySingleCallback)) {
        RemoveAffinityHooks();
        return false;
    }

    return true;
}

void RemoveAffinityHooks() {
    if (g_affinity_single_hook) {
        g_affinity_single_hook.reset();
        Log("hooks: removed affinity-single hook");
    }

    if (g_affinity_append_hook) {
        g_affinity_append_hook.reset();
        Log("hooks: removed affinity-append hook");
    }

    if (g_affinity_existing_hook) {
        g_affinity_existing_hook.reset();
        Log("hooks: removed affinity-existing hook");
    }
}
