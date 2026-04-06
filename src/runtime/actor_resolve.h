#pragma once

#include "runtime/runtime_state.h"

#include "config.h"

bool SelectConfig(const ModConfig& config, int32_t stat_type, StatConfig* selected);
int64_t ClampToRange(int64_t value, int64_t minimum, int64_t maximum);
int64_t ScaleDelta(int64_t delta, double multiplier);
bool IsTrackedStat(int32_t stat_type);
bool TryAssignPlayerResolvedEntry(uintptr_t entry, int32_t stat_type);
bool TryReadPointer(uintptr_t address, uintptr_t* value);
bool TryResolveActorResolveFromMarker(uintptr_t marker, ActorResolveSnapshot* resolved, uintptr_t actor_hint = 0);
bool TryResolveActorResolveFromActor(uintptr_t actor, ActorResolveSnapshot* resolved);
bool TryResolveActorResolveFromRoot(uintptr_t root, ActorResolveSnapshot* resolved);
bool TryResolveActorResolveFromContextRoot(uintptr_t context_root,
                                          ActorResolveSnapshot* resolved,
                                          const ActorResolveSnapshot& player_snapshot);
