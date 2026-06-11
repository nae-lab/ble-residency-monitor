#pragma once

#include <cstdint>

bool bleShutdownForModeSwitch();
bool bleEnsureReady(const char* deviceName, uint8_t maxAttempts = 3);
