#pragma once

#include "runtime/runtime_state.h"

bool StartMountResolverLoop();
void StopMountResolverLoop();
bool TryResolveMountContext(uintptr_t context_root_a,
                            uintptr_t context_root_b,
                            ActorResolveSnapshot* mount_snapshot);
void RefreshTrackedMountFromPlayerActor();
