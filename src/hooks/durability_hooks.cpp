#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <utility>

namespace {

void DurabilityCallback(SafetyHookContext& ctx) {
    if (ctx.rbx < kMinimumPointerAddress) {
        return;
    }

    const bool log_sample = ShouldLogSample(g_durability_samples, 16);
    __try {
        if (log_sample) {
            Log("hooks: durability callback entry=0x%p old=%u requested=%u rip=0x%p",
                reinterpret_cast<void*>(ctx.rbx),
                static_cast<unsigned>(*reinterpret_cast<const uint16_t*>(ctx.rbx + 0x50)),
                static_cast<unsigned>(ctx.rdi & 0xFFFFu),
                reinterpret_cast<void*>(ctx.rip));
        }

        uint16_t adjusted_value = static_cast<uint16_t>(ctx.rdi & 0xFFFFu);
        if (TryAdjustDurabilityWrite(ctx.rbx, &adjusted_value)) {
            ctx.rdi = (ctx.rdi & ~static_cast<uintptr_t>(0xFFFFu)) | adjusted_value;
            if (log_sample) {
                Log("hooks: durability adjusted entry=0x%p final=%u",
                    reinterpret_cast<void*>(ctx.rbx),
                    static_cast<unsigned>(adjusted_value));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_durability_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside durability hook", GetExceptionCode());
        }
    }
}

void DurabilityDeltaCallback(SafetyHookContext& ctx) {
    if (ctx.rbp < kMinimumPointerAddress) {
        return;
    }

    const bool log_sample = ShouldLogSample(g_durability_delta_samples, 16);
    __try {
        const uint16_t current_value = static_cast<uint16_t>(ctx.rax & 0xFFFFu);
        int16_t adjusted_delta = static_cast<int16_t>(ctx.r13 & 0xFFFFu);

        if (log_sample) {
            Log("hooks: durability-delta callback entry=0x%p current=%u delta=%d rip=0x%p",
                reinterpret_cast<void*>(ctx.rbp),
                static_cast<unsigned>(current_value),
                static_cast<int>(adjusted_delta),
                reinterpret_cast<void*>(ctx.rip));
        }

        if (TryAdjustDurabilityDelta(ctx.rbp, current_value, &adjusted_delta)) {
            ctx.r13 = (ctx.r13 & ~static_cast<uintptr_t>(0xFFFFu)) |
                      static_cast<uintptr_t>(static_cast<uint16_t>(adjusted_delta));
            if (log_sample) {
                Log("hooks: durability-delta adjusted entry=0x%p final_delta=%d",
                    reinterpret_cast<void*>(ctx.rbp),
                    static_cast<int>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_durability_delta_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside durability-delta hook", GetExceptionCode());
        }
    }
}

void AbyssDurabilityDeltaCallback(SafetyHookContext& ctx) {
    if (ctx.rbx < kMinimumPointerAddress) {
        return;
    }

    const bool log_sample = ShouldLogSample(g_abyss_durability_delta_samples, 16);
    __try {
        const uint16_t current_value = static_cast<uint16_t>(ctx.rsi & 0xFFFFu);
        int16_t adjusted_delta = static_cast<int16_t>(ctx.r13 & 0xFFFFu);

        if (log_sample) {
            Log("hooks: abyss-durability-delta callback entry=0x%p current=%u delta=%d rip=0x%p",
                reinterpret_cast<void*>(ctx.rbx),
                static_cast<unsigned>(current_value),
                static_cast<int>(adjusted_delta),
                reinterpret_cast<void*>(ctx.rip));
        }

        if (TryAdjustDurabilityDelta(ctx.rbx, current_value, &adjusted_delta)) {
            ctx.r13 = (ctx.r13 & ~static_cast<uintptr_t>(0xFFFFu)) |
                      static_cast<uintptr_t>(static_cast<uint16_t>(adjusted_delta));
            if (log_sample) {
                Log("hooks: abyss-durability-delta adjusted entry=0x%p final_delta=%d",
                    reinterpret_cast<void*>(ctx.rbx),
                    static_cast<int>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_abyss_durability_delta_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside abyss-durability-delta hook", GetExceptionCode());
        }
    }
}

bool InstallDurabilityWriteHook() {
    const uintptr_t target = ScanForDurabilityWriteAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DurabilityCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create durability mid hook");
        return false;
    }

    g_durability_hook = std::move(*hook_result);
    Log("hooks: installed durability hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallDurabilityDeltaHook() {
    const uintptr_t target = ScanForDurabilityDeltaAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DurabilityDeltaCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create durability-delta mid hook");
        return false;
    }

    g_durability_delta_hook = std::move(*hook_result);
    Log("hooks: installed durability-delta hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallAbyssDurabilityDeltaHook() {
    const uintptr_t target = ScanForAbyssDurabilityDeltaAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), AbyssDurabilityDeltaCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create abyss-durability-delta mid hook");
        return false;
    }

    g_abyss_durability_delta_hook = std::move(*hook_result);
    Log("hooks: installed abyss-durability-delta hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallDurabilityHooks() {
    const auto config = GetConfig();
    if (!ShouldInstallDurabilityHooks(config)) {
        return true;
    }

    bool installed_all = true;
    if (!InstallDurabilityWriteHook()) {
        Log("hooks: durability hook unavailable; continuing without maintenance write scaling");
        installed_all = false;
    }

    if (!InstallDurabilityDeltaHook()) {
        Log("hooks: durability-delta hook unavailable; continuing without durability delta scaling");
        installed_all = false;
    }

    if (!InstallAbyssDurabilityDeltaHook()) {
        Log("hooks: abyss-durability-delta hook unavailable; continuing without abyss durability scaling");
        installed_all = false;
    }

    return installed_all || !g_durability_hook || !g_durability_delta_hook || !g_abyss_durability_delta_hook ? true : true;
}

void RemoveDurabilityHooks() {
    if (g_abyss_durability_delta_hook) {
        g_abyss_durability_delta_hook.reset();
        Log("hooks: removed abyss-durability-delta hook");
    }

    if (g_durability_delta_hook) {
        g_durability_delta_hook.reset();
        Log("hooks: removed durability-delta hook");
    }

    if (g_durability_hook) {
        g_durability_hook.reset();
        Log("hooks: removed durability hook");
    }
}
