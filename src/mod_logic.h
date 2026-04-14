#pragma once

#include "runtime/damage_logic.h"
#include "runtime/durability_logic.h"
#include "runtime/stat_logic.h"

#include <cstdint>

void ResetRuntimeState();
void DisableRuntimeProcessing();
bool StartMountResolver();
void StopMountResolver();
bool TryScaleItemGain(int64_t amount, int64_t* value);
void UpdateTrackedMountFromHealthRoot(uintptr_t root);
void UpdateTrackedMountFromStaminaContext(uintptr_t stamina_entry, uintptr_t context_root);
void UpdateTrackedMountStatusComponent(uintptr_t actor, uintptr_t marker);
void UpdateTrackedPlayerStatusComponent(uintptr_t actor, uintptr_t component);
uintptr_t GetTrackedMountActor();
uintptr_t GetTrackedMountStatRoot();
uintptr_t GetTrackedMountStatusMarker();
uintptr_t GetTrackedPlayerStatRoot();
uintptr_t GetTrackedPlayerActor();
uintptr_t GetTrackedPlayerSpiritEntry();
uintptr_t GetTrackedPlayerStaminaEntry();
uintptr_t GetTrackedPlayerStatusMarker();
