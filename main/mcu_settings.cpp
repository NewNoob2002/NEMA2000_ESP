#include "mcu_settings.h"
#include "mcu_typedef.h"

productProperties_t productPropertiesTable[] = {
    {RTK_S20, BRAND_SINGULARXYZ, "S20", "", "s20", true, "s20", "20260429", STATE_ROVER_NOT_STARTED},
};
const int productPropertiesEntries = sizeof(productPropertiesTable) / sizeof(productPropertiesTable[0]);

ProductVariant productType = RTK_S20;
// Online devices
online_devices_t online_devices;

// Settings
settings_t settings;
