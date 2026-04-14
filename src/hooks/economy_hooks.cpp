#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "position_control.h"
#include "scanner.h"

#include <Windows.h>

#include <utility>

namespace {

constexpr int32_t kHealthStatusId = 0;

void DamageCallback(SafetyHookContext& ctx) {
    uintptr_t return_address = 0;
    uintptr_t source_context = 0;
    __try {
        return_address = *reinterpret_cast<const uintptr_t*>(ctx.rsp);
        source_context = *reinterpret_cast<const uintptr_t*>(ctx.rsp + 0x28);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (ctx.rcx < kMinimumPointerAddress) {
        return;
    }

    __try {
            const int32_t status_id = static_cast<int32_t>(ctx.rdx & 0xFFFFu);
            int64_t adjusted_delta = static_cast<int64_t>(ctx.r9);
            const bool relevant_event =
                IsRelevantDamageEvent(ctx.rcx, status_id, source_context, adjusted_delta);
            if (relevant_event) {
                Log("hooks: damage callback target=0x%p delta=%lld sourceCtx=0x%p ret=0x%p",
                    reinterpret_cast<void*>(ctx.rcx),
                    static_cast<long long>(adjusted_delta),
                    reinterpret_cast<void*>(source_context),
                    reinterpret_cast<void*>(return_address));
        }

            if (TryScalePlayerDamage(ctx.rcx,
                                     status_id,
                                     return_address,
                                     source_context,
                                     &adjusted_delta)) {
                ctx.r9 = static_cast<uintptr_t>(adjusted_delta);
                if (relevant_event) {
                    Log("hooks: damage scaled target=0x%p final=%lld sourceCtx=0x%p ret=0x%p",
                        reinterpret_cast<void*>(ctx.rcx),
                        static_cast<long long>(adjusted_delta),
                        reinterpret_cast<void*>(source_context),
                        reinterpret_cast<void*>(return_address));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_damage_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside damage hook", GetExceptionCode());
        }
    }
}

void ItemGainCallback(SafetyHookContext& ctx) {
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const bool log_sample = player_ready && ShouldLogSample(g_item_gain_samples, 16);
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

bool InstallDamageHook() {
    const uintptr_t target = ScanForDamageBattleAccess();
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

}  // namespace

bool InstallEconomyHooks() {
    const auto config = GetConfig();

    if (ShouldInstallDamageHook(config) && !InstallDamageHook()) {
        Log("hooks: damage hook unavailable; continuing without damage scaling");
    }

    if (ShouldInstallItemGainHook(config) && !InstallItemGainHook()) {
        Log("hooks: item-gain hook unavailable; continuing without item gain scaling");
    }

    return true;
}

void RemoveEconomyHooks() {
    if (g_position_height_hook) {
        g_position_height_hook.reset();
        Log("hooks: removed position-height hook");
    }

    if (g_item_gain_hook) {
        g_item_gain_hook.reset();
        Log("hooks: removed item-gain hook");
    }

    if (g_damage_hook) {
        g_damage_hook.reset();
        Log("hooks: removed damage hook");
    }
}

bool InstallOptionalPositionHeightHook() {
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
