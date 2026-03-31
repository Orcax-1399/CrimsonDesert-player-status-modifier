#pragma once

#include <cstdint>

struct PlayerPointerCaptureTarget {
    uintptr_t address = 0;
};

uintptr_t ScanForDamageSlotAccess();
uintptr_t ScanForDamageValueAccess();
uintptr_t ScanForAbyssDurabilityDeltaAccess();
uintptr_t ScanForDurabilityDeltaAccess();
uintptr_t ScanForDurabilityWriteAccess();
uintptr_t ScanForItemGainAccess();
PlayerPointerCaptureTarget ScanForPlayerPointerCapture();
uintptr_t ScanForPositionHeightAccess();
uintptr_t ScanForStatsAccess();
uintptr_t ScanForStatWriteAccess();
