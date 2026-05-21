#pragma once

#include "mcu_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif

extern productProperties_t productPropertiesTable[];
extern const int productPropertiesEntries;
extern ProductVariant productType;

extern const measurementScaleEntry measurementScaleTable[];
extern const int measurementScaleEntries;

extern online_devices_t online_devices;
extern settings_t settings;
extern TaskManager_t task;

#ifdef __cplusplus
}
#endif