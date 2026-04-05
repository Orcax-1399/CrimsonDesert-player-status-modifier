#pragma once

#include <cstdint>

void ResetRuntimeState();
void DisableRuntimeProcessing();
void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryScalePlayerDamage(uintptr_t target, int32_t status_id, uintptr_t return_address, uintptr_t source_context, int64_t* value);
bool TryAdjustStatWrite(uintptr_t entry, bool player_context, uintptr_t context_root_a, uintptr_t context_root_b, int64_t* value);
bool TryAdjustDurabilityDelta(uintptr_t entry, uint16_t current_value, int16_t* delta);
bool TryAdjustDurabilityWrite(uintptr_t entry, uint16_t* value);
bool TryScaleItemGain(int64_t amount, int64_t* value);
void UpdateTrackedMountStatusComponent(uintptr_t actor, uintptr_t marker);
void UpdateTrackedPlayerStatusComponent(uintptr_t actor, uintptr_t component);
uintptr_t GetTrackedMountActor();
uintptr_t GetTrackedMountStatRoot();
uintptr_t GetTrackedMountStatusMarker();
uintptr_t GetTrackedPlayerStatRoot();
uintptr_t GetTrackedPlayerActor();
uintptr_t GetTrackedPlayerStatusMarker();
