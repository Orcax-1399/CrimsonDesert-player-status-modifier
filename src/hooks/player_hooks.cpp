#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <utility>

namespace {

std::atomic<std::uint32_t> g_player_pointer_change_logs{0};
std::atomic<uintptr_t> g_last_player_pointer_actor{0};
std::atomic<uintptr_t> g_last_player_pointer_marker{0};
std::atomic<uintptr_t> g_last_stats_entry{0};
std::atomic<uintptr_t> g_last_stats_component{0};
std::atomic<uintptr_t> g_last_stats_rip{0};

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
    const uintptr_t last_logged_actor = g_last_player_pointer_actor.load(std::memory_order_acquire);
    const uintptr_t last_logged_marker = g_last_player_pointer_marker.load(std::memory_order_acquire);
    if ((tracked_actor != actor || tracked_marker != status_marker) &&
        (last_logged_actor != actor || last_logged_marker != status_marker) &&
        ShouldLogSample(g_player_pointer_change_logs, 8)) {
        g_last_player_pointer_actor.store(actor, std::memory_order_release);
        g_last_player_pointer_marker.store(status_marker, std::memory_order_release);
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
    const uintptr_t rip = ctx.rip;
    const bool player_ready = GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
    const uintptr_t last_entry = g_last_stats_entry.load(std::memory_order_acquire);
    const uintptr_t last_component = g_last_stats_component.load(std::memory_order_acquire);
    const uintptr_t last_rip = g_last_stats_rip.load(std::memory_order_acquire);
    if (player_ready &&
        (last_entry != entry || last_component != component || last_rip != rip) &&
        ShouldLogSample(g_stats_samples, 24)) {
        g_last_stats_entry.store(entry, std::memory_order_release);
        g_last_stats_component.store(component, std::memory_order_release);
        g_last_stats_rip.store(rip, std::memory_order_release);
        Log("hooks: stats callback entry=0x%p component=0x%p rip=0x%p",
            reinterpret_cast<void*>(entry),
            reinterpret_cast<void*>(component),
            reinterpret_cast<void*>(rip));
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
    if (ctx.rdi < kMinimumPointerAddress) {
        return;
    }

    const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
    const bool player_stamina_context =
        tracked_player_stamina >= kMinimumPointerAddress && ctx.rdi == tracked_player_stamina;
    const bool log_sample = player_stamina_context && ShouldLogSample(g_stat_write_samples, 24);
    if (log_sample) {
        Log("hooks: stat-write callback entry=0x%p rbx=%lld tracked_player_stamina=0x%p matched=%d rip=0x%p",
            reinterpret_cast<void*>(ctx.rdi),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(tracked_player_stamina),
            player_stamina_context ? 1 : 0,
            reinterpret_cast<void*>(ctx.rip));
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

      void SpiritDeltaCallback(SafetyHookContext& ctx) {
          const uintptr_t entry = ctx.rcx;
          const uintptr_t tracked_player_spirit = GetTrackedPlayerSpiritEntry();
    if (entry < kMinimumPointerAddress || tracked_player_spirit < kMinimumPointerAddress) {
        return;
    }

    __try {
        if (entry != tracked_player_spirit) {
            return;
        }

        const int32_t actual_type = *reinterpret_cast<const int32_t*>(entry);
        if (actual_type != 18) {
            return;
        }

        const bool log_sample = ShouldLogSample(g_spirit_delta_samples, 24);
        if (log_sample) {
            Log("hooks: spirit-delta callback entry=0x%p delta=%lld rip=0x%p",
                reinterpret_cast<void*>(entry),
                static_cast<long long>(ctx.r8),
                reinterpret_cast<void*>(ctx.rip));
        }

        int64_t adjusted_delta = static_cast<int64_t>(ctx.r8);
        if (TryAdjustSpiritDelta(entry, &adjusted_delta)) {
            ctx.r8 = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: spirit-delta adjusted entry=0x%p final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_spirit_delta_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside spirit-delta hook, disabling runtime processing", GetExceptionCode());
        }

              DisableRuntimeProcessing();
          }
      }

      void StaminaAb00Callback(SafetyHookContext& ctx) {
          const uintptr_t entry = ctx.rax;
          const uintptr_t tracked_player_stamina = GetTrackedPlayerStaminaEntry();
    if (entry < kMinimumPointerAddress || tracked_player_stamina < kMinimumPointerAddress) {
        return;
    }

    const bool log_sample = ShouldLogSample(g_stamina_ab00_samples, 24);
    if (log_sample) {
        Log("hooks: stamina-ab00 callback entry=0x%p delta=%lld owner=0x%p status=%u rip=0x%p",
            reinterpret_cast<void*>(entry),
            static_cast<long long>(ctx.rbx),
            reinterpret_cast<void*>(ctx.r14),
            static_cast<unsigned>(ctx.rdi & 0xFFFFu),
            reinterpret_cast<void*>(ctx.rip));
    }

    __try {
        if (entry != tracked_player_stamina) {
            UpdateTrackedMountFromStaminaContext(entry, ctx.r14);
        }

        int64_t adjusted_delta = static_cast<int64_t>(ctx.rbx);
        if (TryAdjustStaminaDelta(entry, &adjusted_delta)) {
            ctx.rbx = static_cast<uintptr_t>(adjusted_delta);
            if (log_sample) {
                Log("hooks: stamina-ab00 adjusted entry=0x%p final-delta=%lld",
                    reinterpret_cast<void*>(entry),
                    static_cast<long long>(adjusted_delta));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!g_reported_stamina_ab00_exception.exchange(true, std::memory_order_acq_rel)) {
            Log("hooks: exception 0x%08lX inside stamina-ab00 hook, disabling runtime processing", GetExceptionCode());
        }

        DisableRuntimeProcessing();
    }
}

bool InstallSpiritDeltaHook() {
    const uintptr_t target = ScanForSpiritDeltaAccess();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), SpiritDeltaCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create spirit-delta mid hook");
        return false;
    }

    g_spirit_delta_hook = std::move(*hook_result);
    Log("hooks: installed spirit-delta hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
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

      bool InstallStaminaAb00Hook() {
          const uintptr_t target = ScanForStaminaAb00Access();
          if (target == 0) {
              return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), StaminaAb00Callback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create stamina-ab00 mid hook");
        return false;
    }

          g_stamina_ab00_hook = std::move(*hook_result);
          Log("hooks: installed stamina-ab00 hook at 0x%p", reinterpret_cast<void*>(target));
          return true;
      }

}  // namespace

bool InstallPlayerHooks() {
    return InstallPlayerPointerHook();
}

bool InstallPlayerStatHooks() {
    const auto config = GetConfig();
    if (!InstallStatsHook()) {
        return false;
    }

      if (ShouldInstallSpiritHook(config) && !InstallSpiritDeltaHook()) {
          return false;
      }

      if (ShouldInstallLegacyStatWriteHook(config) && !InstallStatWriteHook()) {
          return false;
      }

      if (ShouldInstallMountStaminaHook(config) && !InstallStaminaAb00Hook()) {
          return false;
          }
      
          return true;
      }
      
void RemovePlayerStatHooks() {
      if (g_stamina_ab00_hook) {
          g_stamina_ab00_hook.reset();
          Log("hooks: removed stamina-ab00 hook");
    }

    if (g_spirit_delta_hook) {
        g_spirit_delta_hook.reset();
        Log("hooks: removed spirit-delta hook");
    }

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
