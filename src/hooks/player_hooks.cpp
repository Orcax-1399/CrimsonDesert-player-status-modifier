#include "hooks/hooks_internal.h"

#include "logger.h"
#include "mod_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <utility>

namespace {

void PlayerPointerCallback(SafetyHookContext& ctx) {
    const uintptr_t actor = ctx.rax;
    const uintptr_t status_marker = ctx.rsi;
    if (actor < kMinimumPointerAddress || status_marker < kMinimumPointerAddress) {
        if (ShouldLogSample(g_player_pointer_samples, 8)) {
            Log("hooks: player-pointer skipped, actor/marker below threshold rax=0x%p rsi=0x%p",
                reinterpret_cast<void*>(ctx.rax),
                reinterpret_cast<void*>(ctx.rsi));
        }
        return;
    }

    const uintptr_t tracked_actor = GetTrackedPlayerActor();
    const uintptr_t tracked_marker = GetTrackedPlayerStatusMarker();
    if (tracked_actor != actor || tracked_marker != status_marker) {
        Log("hooks: player-pointer callback actor=0x%p marker=0x%p rax=0x%p rsi=0x%p rdx=0x%p",
            reinterpret_cast<void*>(actor),
            reinterpret_cast<void*>(status_marker),
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx));
    }

    UpdateTrackedPlayerStatusComponent(actor, status_marker);
}

void StatsCallback(SafetyHookContext& ctx) {
    const uintptr_t entry = ctx.rax;
    const uintptr_t component = ctx.rsi;
    if (ShouldLogSample(g_stats_samples, 24)) {
        Log("hooks: stats callback entry=0x%p component=0x%p rip=0x%p",
            reinterpret_cast<void*>(entry),
            reinterpret_cast<void*>(component),
            reinterpret_cast<void*>(ctx.rip));
    }

    if (entry < kMinimumPointerAddress || component < kMinimumPointerAddress) {
        return;
    }

    __try {
        ObserveStatEntry(entry, component);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stats_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stats hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

void StatWriteCallback(SafetyHookContext& ctx) {
    const uintptr_t tracked_root = GetTrackedPlayerStatRoot();
    const uintptr_t tracked_marker = GetTrackedPlayerStatusMarker();
    if (tracked_root < kMinimumPointerAddress || tracked_marker < kMinimumPointerAddress) {
        return;
    }

    const bool log_sample = ShouldLogSample(g_stat_write_samples, 24);
    const bool player_context =
        tracked_root >= kMinimumPointerAddress &&
        (ctx.r14 == tracked_root || ctx.r15 == tracked_root);
    if (log_sample) {
        Log("hooks: stat-write callback entry=0x%p rbx=%lld r14=0x%p r15=0x%p tracked_root=0x%p player_context=%d rip=0x%p",
            reinterpret_cast<void*>(ctx.rdi),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.r14),
            reinterpret_cast<void*>(ctx.r15),
            reinterpret_cast<void*>(tracked_root),
            player_context ? 1 : 0,
            reinterpret_cast<void*>(ctx.rip));
    }

    if (ctx.rdi < kMinimumPointerAddress) {
        return;
    }

    __try {
        int64_t adjusted_value = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStatWrite(ctx.rdi, player_context, ctx.r14, ctx.r15, &adjusted_value)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_value);
            if (log_sample) {
                Log("hooks: stat-write adjusted entry=0x%p final=%lld",
                    reinterpret_cast<void*>(ctx.rdi),
                    static_cast<long long>(adjusted_value));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stat_write_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stat-write hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

bool InstallPlayerPointerHook() {
    const auto target = ScanForPlayerPointerCapture();
    if (target.address == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target.address), PlayerPointerCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create player-pointer mid hook");
        return false;
    }

    g_player_pointer_hook = std::move(*hook_result);
    Log("hooks: installed player-pointer hook at 0x%p", reinterpret_cast<void*>(target.address));
    return true;
}

bool InstallStatsHook() {
    const uintptr_t target = ScanForStatsAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StatsCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stats mid hook");
        return false;
    }

    g_stats_hook = std::move(*hook_result);
    Log("hooks: installed stats hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallStatWriteHook() {
    const uintptr_t target = ScanForStatWriteAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StatWriteCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stat-write mid hook");
        return false;
    }

    g_stat_write_hook = std::move(*hook_result);
    Log("hooks: installed stat-write hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallPlayerHooks() {
    return InstallPlayerPointerHook();
}

bool InstallPlayerStatHooks() {
    return InstallStatsHook() &&
           InstallStatWriteHook();
}

void RemovePlayerStatHooks() {
    if (g_stat_write_hook) {
        g_stat_write_hook.reset();
        Log("hooks: removed stat-write hook");
    }

    if (g_stats_hook) {
        g_stats_hook.reset();
        Log("hooks: removed stats hook");
    }
}

void RemovePlayerHooks() {
    RemovePlayerStatHooks();

    if (g_player_pointer_hook) {
        g_player_pointer_hook.reset();
        Log("hooks: removed player-pointer hook");
    }
}
