#pragma once

#include "config.h"

bool InitializePositionControl();
bool ApplyPositionControlConfig(const PositionControlConfig& previous, const PositionControlConfig& current);
void ShutdownPositionControl();
bool IsPositionControlEnabled();
bool ConsumeHeightAdjustment(float* delta);
bool ConsumeHorizontalMultiplier(float* multiplier);
