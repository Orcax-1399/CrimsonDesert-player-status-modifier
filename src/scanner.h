#pragma once

#include <cstdint>

struct PlayerPointerCaptureTarget {
    uintptr_t address = 0;
};

uintptr_t ScanForDamageValueAccess();
uintptr_t ScanForItemGainAccess();
PlayerPointerCaptureTarget ScanForPlayerPointerCapture();
uintptr_t ScanForStatsAccess();
uintptr_t ScanForStatWriteAccess();
