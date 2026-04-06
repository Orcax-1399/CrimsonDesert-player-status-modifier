#include "ptrchain_resources.h"

namespace {

constexpr uintptr_t kMountedDragonChainBaseRva = 0x05D613B8;

constexpr PointerChainStep kDragonMarkerChainPrimarySteps[] = {
    {0x150, true},
    {0x8, true},
    {0x68, true},
    {0x20, true},
};

constexpr PointerChainStep kDragonMarkerChainSecondarySteps[] = {
    {0xA0, true},
    {0x8, true},
    {0x78, true},
    {0x0, true},
};

}  // namespace

const PointerChainPairDefinition kDragonMarkerChain = {
    "dragon-marker",
    {
        "dragon-marker.primary",
        kMountedDragonChainBaseRva,
        kDragonMarkerChainPrimarySteps,
        sizeof(kDragonMarkerChainPrimarySteps) / sizeof(kDragonMarkerChainPrimarySteps[0]),
    },
    {
        "dragon-marker.secondary",
        kMountedDragonChainBaseRva,
        kDragonMarkerChainSecondarySteps,
        sizeof(kDragonMarkerChainSecondarySteps) / sizeof(kDragonMarkerChainSecondarySteps[0]),
    },
};
