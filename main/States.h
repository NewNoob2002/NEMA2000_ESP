#pragma once

#include <stdint.h>
#include "mcu_typedef.h"

//----------------------------------------
// Types
//----------------------------------------
#if defined(__cplusplus)
class UnicoreUM980;

void stateInit();
void stateUpdate(UnicoreUM980* pUm980);
#endif // __cplusplus

void requestChangeState(SystemState_t requestedState);
const char* getState(SystemState_t state);
void changeState(SystemState_t newState);
const char* stateToRtkMode(SystemState_t state);

bool inRoverMode();
bool inBaseMode();
bool inWebConfigMode();