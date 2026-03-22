#pragma once

#include <cstdint>

uintptr_t ScanForDamageValueAccess();
uintptr_t ScanForItemGainAccess();
uintptr_t ScanForPlayerPointerCapture();
uintptr_t ScanForStatsAccess();
uintptr_t ScanForStatWriteAccess();
