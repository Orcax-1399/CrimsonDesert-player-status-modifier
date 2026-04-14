#pragma once

#include <cstdint>

bool TryScaleAffinityGain(uintptr_t record, int64_t old_value, int64_t* new_value);
bool TryScaleAffinityCurrentWrite(uintptr_t record, int64_t old_value, int64_t pending_delta, int64_t* new_value);
