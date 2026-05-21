#include "mcu_settings.h"
#include "mcu_typedef.h"

productProperties_t productPropertiesTable[] = {
    {RTK_S20, BRAND_SINGULARXYZ, "S20", "", "s20", true, "s20", "20260429", STATE_ROVER_NOT_STARTED},
};

const int productPropertiesEntries = sizeof(productPropertiesTable) / sizeof(productPropertiesTable[0]);

const measurementScaleEntry measurementScaleTable[] = {
    {MEASUREMENT_UNITS_METERS, "meters", "m", "m", 1.0, 1.0, 1.0, 30.0},
    {MEASUREMENT_UNITS_FEET_INCHES, "feet and inches", "ft", "in", FEET_IN_A_METER, 3.0, 12.0, 100.0}};

const int measurementScaleEntries = sizeof(measurementScaleTable) / sizeof(measurementScaleTable[0]);

ProductVariant productType = RTK_S20;
// Online devices
online_devices_t online_devices;

// Settings
settings_t settings;

// Task Manager
TaskManager_t task;
