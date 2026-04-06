#pragma once

#include <cstddef>
#include <cstdint>

struct PointerChainStep {
    uintptr_t offset = 0;
    bool dereference = true;
};

struct PointerChainDefinition {
    const char* name = nullptr;
    uintptr_t base_rva = 0;
    const PointerChainStep* steps = nullptr;
    size_t step_count = 0;
};

struct PointerChainPairDefinition {
    const char* name = nullptr;
    PointerChainDefinition primary{};
    PointerChainDefinition secondary{};
};

extern const PointerChainPairDefinition kDragonMarkerChain;
