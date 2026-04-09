#include "hooks/hooks_internal.h"

#include "config.h"
#include "logger.h"
#include "mod_logic.h"
#include "scanner.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <utility>

namespace {

constexpr uintptr_t kZeroFlagMask = 1ull << 6;
std::atomic<std::uint32_t> g_dragon_village_summon_samples{0};
std::atomic<std::uint32_t> g_dragon_roof_restrict_samples{0};
void* g_dragon_flying_restrict_cave = nullptr;

bool IsPlayerContextReady() {
    return GetTrackedPlayerActor() >= kMinimumPointerAddress &&
           GetTrackedPlayerStatusMarker() >= kMinimumPointerAddress;
}

void DragonVillageSummonCallback(SafetyHookContext& ctx) {
    const auto config = GetConfig();
    if (!config.general.enabled || !config.dragon_limit.village_summon || !IsPlayerContextReady()) {
        return;
    }

    ctx.rflags |= kZeroFlagMask;
    if (ShouldLogSample(g_dragon_village_summon_samples, 16)) {
        Log("hooks: dragon village summon bypassed rip=0x%p player=0x%p marker=0x%p",
            reinterpret_cast<void*>(ctx.rip),
            reinterpret_cast<void*>(GetTrackedPlayerActor()),
            reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()));
    }
}

void* AllocatePageNearAddress(const uintptr_t target, const std::size_t size) {
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);

    const uintptr_t granularity = static_cast<uintptr_t>(system_info.dwAllocationGranularity);
    const uintptr_t min_address = target > 0x7FFF0000ull ? target - 0x7FFF0000ull : granularity;
    const uintptr_t max_address = target + 0x7FFF0000ull;

    for (uintptr_t distance = 0; distance < 0x7FFF0000ull; distance += granularity) {
        const uintptr_t forward = target + distance;
        if (forward >= min_address && forward < max_address) {
            if (void* allocation = VirtualAlloc(reinterpret_cast<void*>(forward),
                                                size,
                                                MEM_RESERVE | MEM_COMMIT,
                                                PAGE_EXECUTE_READWRITE)) {
                return allocation;
            }
        }

        if (distance == 0 || target <= distance) {
            continue;
        }

        const uintptr_t backward = target - distance;
        if (backward >= min_address && backward < max_address) {
            if (void* allocation = VirtualAlloc(reinterpret_cast<void*>(backward),
                                                size,
                                                MEM_RESERVE | MEM_COMMIT,
                                                PAGE_EXECUTE_READWRITE)) {
                return allocation;
            }
        }
    }

    return nullptr;
}

bool WriteExecutableBytes(void* destination, const void* source, const std::size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return false;
    }

    std::memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);

    DWORD restore_protect = 0;
    VirtualProtect(destination, size, old_protect, &restore_protect);
    return true;
}

void DragonRoofRestrictCallback(SafetyHookContext& ctx) {
    const auto config = GetConfig();
    if (!config.general.enabled || !config.dragon_limit.roof_summon_experimental || !IsPlayerContextReady()) {
        return;
    }

    ctx.rbx = 0;
    if (ShouldLogSample(g_dragon_roof_restrict_samples, 16)) {
        Log("hooks: dragon roof summon experimental bypassed rip=0x%p player=0x%p marker=0x%p",
            reinterpret_cast<void*>(ctx.rip),
            reinterpret_cast<void*>(GetTrackedPlayerActor()),
            reinterpret_cast<void*>(GetTrackedPlayerStatusMarker()));
    }
}

bool InstallDragonVillageSummonHook() {
    const uintptr_t target = ScanForDragonVillageSummonJump();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DragonVillageSummonCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create dragon-village-summon mid hook");
        return false;
    }

    g_dragon_village_summon_hook = std::move(*hook_result);
    Log("hooks: installed dragon-village-summon hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallDragonFlyingRestrictHook() {
    const uintptr_t target = ScanForDragonFlyingRestrictWrite();
    if (target == 0) {
        return false;
    }

    auto* const cave = static_cast<std::uint8_t*>(AllocatePageNearAddress(target, 0x100));
    if (cave == nullptr) {
        Log("hooks: failed to allocate dragon-flying-restrict code cave");
        return false;
    }

    constexpr std::size_t kReturnOffset = 31;
    const auto return_address = static_cast<std::int64_t>(target + 7);
    const auto next_ip = reinterpret_cast<std::int64_t>(cave + kReturnOffset);
    const auto rel32 = static_cast<std::int32_t>(return_address - next_ip);

    std::array<std::uint8_t, 31> code{
        0x81, 0xFF, 0x47, 0x02, 0x00, 0x00,
        0x75, 0x0B,
        0x84, 0xC0,
        0x75, 0x07,
        0x83, 0x3B, 0x06,
        0x75, 0x02,
        0xB0, 0x01,
        0x41, 0x88, 0x47, 0x04,
        0x41, 0x89, 0x3F,
        0xE9, 0x00, 0x00, 0x00, 0x00,
    };
    std::memcpy(code.data() + 27, &rel32, sizeof(rel32));

    if (!WriteExecutableBytes(cave, code.data(), code.size())) {
        VirtualFree(cave, 0, MEM_RELEASE);
        Log("hooks: failed to write dragon-flying-restrict code cave");
        return false;
    }

    auto hook_result = SafetyHookInline::create(reinterpret_cast<void*>(target), cave);
    if (!hook_result.has_value()) {
        VirtualFree(cave, 0, MEM_RELEASE);
        Log("hooks: failed to create dragon-flying-restrict inline hook");
        return false;
    }

    g_dragon_flying_restrict_hook = std::move(*hook_result);
    g_dragon_flying_restrict_cave = cave;
    Log("hooks: installed dragon-flying-restrict hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

bool InstallDragonRoofRestrictHook() {
    const uintptr_t target = ScanForDragonRoofRestrictTest();
    if (target == 0) {
        return false;
    }

    auto hook_result = SafetyHookMid::create(reinterpret_cast<void*>(target), DragonRoofRestrictCallback);
    if (!hook_result.has_value()) {
        Log("hooks: failed to create dragon-roof-restrict mid hook");
        return false;
    }

    g_dragon_roof_restrict_hook = std::move(*hook_result);
    Log("hooks: installed dragon-roof-restrict hook at 0x%p", reinterpret_cast<void*>(target));
    return true;
}

}  // namespace

bool InstallDragonLimitHooks() {
    const auto config = GetConfig();

    if (ShouldInstallDragonVillageSummonHook(config) && !InstallDragonVillageSummonHook()) {
        return false;
    }

    if (ShouldInstallDragonFlyingRestrictHook(config) && !InstallDragonFlyingRestrictHook()) {
        return false;
    }

    if (ShouldInstallDragonRoofRestrictHook(config) && !InstallDragonRoofRestrictHook()) {
        return false;
    }

    return true;
}

void RemoveDragonLimitHooks() {
    if (g_dragon_roof_restrict_hook) {
        g_dragon_roof_restrict_hook.reset();
        Log("hooks: removed dragon-roof-restrict hook");
    }

    if (g_dragon_flying_restrict_hook) {
        g_dragon_flying_restrict_hook.reset();
        Log("hooks: removed dragon-flying-restrict hook");
    }

    if (g_dragon_flying_restrict_cave != nullptr) {
        VirtualFree(g_dragon_flying_restrict_cave, 0, MEM_RELEASE);
        g_dragon_flying_restrict_cave = nullptr;
    }

    if (g_dragon_village_summon_hook) {
        g_dragon_village_summon_hook.reset();
        Log("hooks: removed dragon-village-summon hook");
    }
}
