#pragma once

#include "mcu_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

extern productProperties_t productPropertiesTable[];

extern online_devices_t online_devices;
extern settings_t settings;

// System state
extern volatile SystemState_t systemState;
extern volatile SystemState_t lastSystemState;
extern volatile SystemState_t requestedSystemState;
extern bool newSystemStateRequested;

#ifdef __cplusplus
}
#endif