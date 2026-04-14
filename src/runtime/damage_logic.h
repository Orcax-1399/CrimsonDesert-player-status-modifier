#pragma once

#include <cstdint>

bool IsRelevantDamageEvent(uintptr_t target,
                           int32_t status_id,
                           uintptr_t source_context,
                           int64_t delta);

bool TryScalePlayerDamage(uintptr_t target,
                          int32_t status_id,
                          uintptr_t return_address,
                          uintptr_t source_context,
                          int64_t* value);
