#pragma once

#include <stdint.h>
#include "mcu_typedef.h"

void stateUpdate();
void requestChangeState(SystemState_t requestedState);
const char* getState(SystemState_t state);
void changeState(SystemState_t newState);