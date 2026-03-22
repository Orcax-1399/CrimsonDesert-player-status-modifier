#pragma once

#include <cstdint>

void ResetRuntimeState();
void DisableRuntimeProcessing();
void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryScalePlayerDamage(uintptr_t source_component, int32_t slot, uintptr_t* value);
bool TryAdjustStatWrite(uintptr_t entry, int64_t* value);
bool TryScaleItemGain(int64_t amount, int64_t* value);
void UpdateTrackedPlayerStatusComponent(uintptr_t actor, uintptr_t component);
