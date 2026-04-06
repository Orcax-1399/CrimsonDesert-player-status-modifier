#pragma once

#include <cstdint>

bool TryAdjustDurabilityDelta(uintptr_t entry, uint16_t current_value, int16_t* delta);
bool TryAdjustDurabilityWrite(uintptr_t entry, uint16_t* value);
