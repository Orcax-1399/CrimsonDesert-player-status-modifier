#pragma once

#include <cstdint>

struct PlayerPointerCaptureTarget {
    uintptr_t address = 0;
};

struct MountPointerCaptureTarget {
    uintptr_t address = 0;
};

uintptr_t ScanForDamageBattleAccess();
uintptr_t ScanForDragonFlyingRestrictWrite();
uintptr_t ScanForDragonRoofRestrictTest();
uintptr_t ScanForDragonVillageSummonJump();
uintptr_t ScanForAbyssDurabilityDeltaAccess();
uintptr_t ScanForAffinityGainCommit();
uintptr_t ScanForDurabilityDeltaAccess();
uintptr_t ScanForDurabilityWriteAccess();
uintptr_t ScanForItemGainAccess();
MountPointerCaptureTarget ScanForMountPointerCapture();
PlayerPointerCaptureTarget ScanForPlayerPointerCapture();
uintptr_t ScanForPositionHeightAccess();
uintptr_t ScanForStatsAccess();
uintptr_t ScanForStatWriteAccess();
