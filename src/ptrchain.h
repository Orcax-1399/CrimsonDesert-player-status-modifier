#pragma once

#include <cstdint>

struct PointerChainPairDefinition;

bool TryResolveMountedDragonMarker(uintptr_t* marker);
bool TryResolvePointerChainPair(const PointerChainPairDefinition& definition, uintptr_t* value);
