#pragma once

#include <cstdint>

void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryAdjustSpiritDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustStaminaDelta(uintptr_t entry, int64_t* delta);
bool TryAdjustStatWrite(uintptr_t entry, int64_t* value);
