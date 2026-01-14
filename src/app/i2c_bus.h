#pragma once

#include <stdint.h>

namespace aiw {

void i2cBusInit(int sda, int scl, uint32_t freqHz);
bool i2cBusLock(uint32_t timeoutMs);
void i2cBusUnlock();

}  // namespace aiw

