#include "hooks.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"
#include "scanner.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

#include <safetyhook.hpp>

namespace {

constexpr uintptr_t kMinimumPointerAddress = 0x10000000;

std::mutex g_hook_mutex;
SafetyHookMid g_player_pointer_hook{};
SafetyHookMid g_stats_hook{};
SafetyHookMid g_stat_write_hook{};
SafetyHookMid g_damage_hook{};
SafetyHookMid g_item_gain_hook{};
SafetyHookMid g_position_height_hook{};
std::atomic<bool> g_reported_stats_exception{false};
std::atomic<bool> g_reported_stat_write_exception{false};
std::atomic<bool> g_reported_damage_exception{false};
std::atomic<bool> g_reported_item_gain_exception{false};
std::atomic<std::uint32_t> g_player_pointer_samples{0};
std::atomic<std::uint32_t> g_stats_samples{0};
std::atomic<std::uint32_t> g_stat_write_samples{0};
std::atomic<std::uint32_t> g_damage_samples{0};
std::atomic<std::uint32_t> g_item_gain_samples{0};

bool ShouldLogSample(std::atomic<std::uint32_t>& counter, const std::uint32_t limit) {
    const auto current = counter.fetch_add(1, std::memory_order_acq_rel);
    return current < limit;
}

void PlayerPointerCallback(SafetyHookContext& ctx) {
    const uintptr_t actor = ctx.rax;
    const uintptr_t status_marker = ctx.rsi;
    const bool log_sample = ShouldLogSample(g_player_pointer_samples, 16);
    if (log_sample) {
        Log("hooks: player-pointer callback actor=0x%p marker=0x%p rax=0x%p rsi=0x%p rdx=0x%p",
            reinterpret_cast<void*>(actor),
            reinterpret_cast<void*>(status_marker),
            reinterpret_cast<void*>(ctx.rax),
            reinterpret_cast<void*>(ctx.rsi),
            reinterpret_cast<void*>(ctx.rdx));
    }

    if (actor < kMinimumPointerAddress || status_marker < kMinimumPointerAddress) {
        if (log_sample) {
            Log("hooks: player-pointer skipped, actor/marker below threshold");
        }
        return;
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
    const bool log_sample = ShouldLogSample(g_stat_write_samples, 24);
    if (log_sample) {
        Log("hooks: stat-write callback entry=0x%p rbx=%lld rip=0x%p",
            reinterpret_cast<void*>(ctx.rdi),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.rip));
    }

    if (ctx.rdi < kMinimumPointerAddress) {
        return;
    }

    __try {
        int64_t adjusted_value = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStatWrite(ctx.rdi, &adjusted_value)) {
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

void DamageCallback(SafetyHookContext& ctx) {
    const bool log_sample = ShouldLogSample(g_damage_samples, 16);
    if (log_sample) {
        Log("hooks: damage callback r15=0x%p r12=%u rbx=0x%p rip=0x%p",
            reinterpret_cast<void*>(ctx.r15),
            static_cast<unsigned>(ctx.r12 & 0xFFFFFFFFu),
            reinterpret_cast<void*>(ctx.rbx),
            reinterpret_cast<void*>(ctx.rip));
    }

    if (ctx.r15 < kMinimumPointerAddress) {
        return;
    }

    __try {
        uintptr_t adjusted_value = ctx.rbx;
        if (TryScalePlayerDamage(ctx.r15, static_cast<int32_t>(ctx.r12 & 0xFFFFFFFFu), &adjusted_value)) {
            ctx.rbx = adjusted_value;
            if (log_sample) {
                Log("hooks: damage scaled to 0x%p", reinterpret_cast<void*>(ctx.rbx));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_damage_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside damage hook", GetExceptionCode());
        }
    }
}

void ItemGainCallback(SafetyHookContext& ctx) {
    const bool log_sample = ShouldLogSample(g_item_gain_samples, 16);
    if (log_sample) {
        Log("hooks: item-gain callback target=0x%p amount=%lld rip=0x%p",
            reinterpret_cast<void*>(ctx.r8 + ctx.rdi + 0x10),
            static_cast<long long>(ctx.rcx),
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        int64_t adjusted_amount = static_cast<int64_t>(ctx.rcx);
        if (TryScaleItemGain(static_cast<int64_t>(ctx.rcx), &adjusted_amount)) {
            ctx.rcx = static_cast<uintptr_t>(adjusted_amount);
            if (log_sample) {
                Log("hooks: item-gain scaled to %lld", static_cast<long long>(adjusted_amount));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_item_gain_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside item-gain hook", GetExceptionCode());
        }
    }
}

void PositionHeightCallback(SafetyHookContext& ctx) {
    if (ctx.r13 < kMinimumPointerAddress) {
        return;
    }

    float horizontal_multiplier = 1.0f;
    if (ConsumeHorizontalMultiplier(&horizontal_multiplier) && horizontal_multiplier != 1.0f) {
        __try {
            const auto* const current_position = reinterpret_cast<const float*>(ctx.r13);
            const float base_x = current_position[0];
            const float base_z = current_position[2];
            const float delta_x = ctx.xmm0.f32[0] - base_x;
            const float delta_z = ctx.xmm0.f32[2] - base_z;
            ctx.xmm0.f32[0] = base_x + delta_x * horizontal_multiplier;
            ctx.xmm0.f32[2] = base_z + delta_z * horizontal_multiplier;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    float height_delta = 0.0f;
    if (!ConsumeHeightAdjustment(&height_delta)) {
        return;
    }

    ctx.xmm0.f32[1] += height_delta;
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

bool InstallDamageHook() {
    const uintptr_t target = ScanForDamageValueAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DamageCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create damage mid hook");
        return false;
    }

    g_damage_hook = std::move(*hook_result);
    Log("hooks: installed damage hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallItemGainHook() {
    const uintptr_t target = ScanForItemGainAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), ItemGainCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create item-gain mid hook");
        return false;
    }

    g_item_gain_hook = std::move(*hook_result);
    Log("hooks: installed item-gain hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallPositionHeightHook() {
    const uintptr_t target = ScanForPositionHeightAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), PositionHeightCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create position-height mid hook");
        return false;
    }

    g_position_height_hook = std::move(*hook_result);
    Log("hooks: installed position-height hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallHooks() {
    std::lock_guard lock(g_hook_mutex);
    if (g_player_pointer_hook && g_stats_hook && g_stat_write_hook && g_damage_hook && g_item_gain_hook) {
        return true;
    }

    if (!InstallPlayerPointerHook()) {
        return false;
    }

    if (!InstallStatsHook()) {
        g_player_pointer_hook.reset();
        return false;
    }

    if (!InstallStatWriteHook()) {
        g_stats_hook.reset();
        g_player_pointer_hook.reset();
        return false;
    }

    if (!InstallDamageHook()) {
        g_stat_write_hook.reset();
        g_stats_hook.reset();
        g_player_pointer_hook.reset();
        return false;
    }

    if (!InstallItemGainHook()) {
        g_damage_hook.reset();
        g_stat_write_hook.reset();
        g_stats_hook.reset();
        g_player_pointer_hook.reset();
        return false;
    }

    if (!g_position_height_hook && !InstallPositionHeightHook()) {
        Log("hooks: position-height hook unavailable; continuing without position control");
    }

    return true;
}

bool IsPositionHeightHookInstalled() {
    std::lock_guard lock(g_hook_mutex);
    return static_cast<bool>(g_position_height_hook);
}

void RemoveHooks() {
    std::lock_guard lock(g_hook_mutex);

    if (g_item_gain_hook) {
        g_item_gain_hook.reset();
        Log("hooks: removed item-gain hook");
    }

    if (g_position_height_hook) {
        g_position_height_hook.reset();
        Log("hooks: removed position-height hook");
    }

    if (g_damage_hook) {
        g_damage_hook.reset();
        Log("hooks: removed damage hook");
    }

    if (g_stat_write_hook) {
        g_stat_write_hook.reset();
        Log("hooks: removed stat-write hook");
    }

    if (g_stats_hook) {
        g_stats_hook.reset();
        Log("hooks: removed stats hook");
    }

    if (g_player_pointer_hook) {
        g_player_pointer_hook.reset();
        Log("hooks: removed player-pointer hook");
    }
}
