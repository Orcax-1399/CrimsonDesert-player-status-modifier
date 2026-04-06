#pragma once

#include <cstdint>

void ObserveStatEntry(uintptr_t entry, uintptr_t component);
bool TryAdjustStatWrite(uintptr_t entry,
                        bool player_context,
                        uintptr_t context_root_a,
                        uintptr_t context_root_b,
                        int64_t* value);
