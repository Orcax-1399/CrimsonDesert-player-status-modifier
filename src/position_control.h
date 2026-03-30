#pragma once

bool InitializePositionControl();
void ShutdownPositionControl();
bool IsPositionControlEnabled();
bool ConsumeHeightAdjustment(float* delta);
