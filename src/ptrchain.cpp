#include "ptrchain.h"

#include "ptrchain_resources.h"

#include <Windows.h>

#include <cstdint>

namespace {

constexpr uintptr_t kMinimumPointerAddress = 0x10000000;

bool TryReadPointer(const uintptr_t address, uintptr_t* const value) {
    if (value == nullptr || address < kMinimumPointerAddress) {
        return false;
    }

    __try {
        *value = *reinterpret_cast<const uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryReadPointerChain(const PointerChainDefinition& definition, uintptr_t* const value) {
    if (value == nullptr || definition.base_rva == 0 || definition.steps == nullptr || definition.step_count == 0) {
        return false;
    }

    const uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (module_base < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t current = module_base + definition.base_rva;
    if (current < kMinimumPointerAddress) {
        return false;
    }

    for (size_t index = 0; index < definition.step_count; ++index) {
        const auto& step = definition.steps[index];
        if (current < kMinimumPointerAddress) {
            return false;
        }

        const uintptr_t next_address = current + step.offset;
        if (!step.dereference) {
            current = next_address;
            continue;
        }

        if (!TryReadPointer(next_address, &current)) {
            return false;
        }
    }

    *value = current;
    return true;
}

}  // namespace

bool TryResolvePointerChainPair(const PointerChainPairDefinition& definition, uintptr_t* const value) {
    if (value == nullptr) {
        return false;
    }

    uintptr_t primary = 0;
    if (!TryReadPointerChain(definition.primary, &primary) || primary < kMinimumPointerAddress) {
        return false;
    }

    uintptr_t secondary = 0;
    if (!TryReadPointerChain(definition.secondary, &secondary) || secondary < kMinimumPointerAddress) {
        return false;
    }

    if (primary != secondary) {
        return false;
    }

    *value = primary;
    return true;
}

bool TryResolveMountedDragonMarker(uintptr_t* const marker) {
    return TryResolvePointerChainPair(kDragonMarkerChain, marker);
}
