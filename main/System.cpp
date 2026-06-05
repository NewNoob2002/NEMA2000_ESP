#include <Arduino.h>
#include "Support.h"
#include "esp_app_desc.h"
#include "mcu_settings.h"

static uint32_t lastHeapReport = 0;

void
querySystemInfo() {
    const esp_app_desc_t* app = esp_app_get_description();
}

// If debug option is on, print available heap
void
reportHeapNow(bool alwaysPrint) {
    if (alwaysPrint || (settings.enableHeapReport == true)) {
        lastHeapReport = millis();

        if (online_devices.psram == true) {
            systemPrintf("[%ld] FreeHeap: %ld / HeapLowestPoint: %ld / LargestBlock: %ld / "
                         "Used PSRAM: %ld\r\n",
                         lastHeapReport, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                         xPortGetMinimumEverFreeHeapSize(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                         heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        } else {
            systemPrintf("[%ld] TotalHeap: %ld, FreeHeap: %ld / HeapLowestPoint: %ld / "
                         "LargestBlock: %ld\r\n",
                         lastHeapReport, heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                         heap_caps_get_free_size(MALLOC_CAP_INTERNAL), xPortGetMinimumEverFreeHeapSize(),
                         heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }
    }
}

void
reportHeap() {
    if (millis() - lastHeapReport >= 5000) {
        reportHeapNow(true);
    }
}

// This allows the measurementScaleTable to be alphabetised if desired
int
measurementScaleToIndex(uint8_t scale) {
    for (int i = 0; i < MEASUREMENT_UNITS_MAX; i++) {
        if (measurementScaleTable[i].measurementUnit == scale) {
            return i;
        }
    }

    return -1; // This should never happen...
}

// Returns string of the HPA units
const char*
getHpaUnits(double hpa, char* buffer, int length, int decimals, bool limit) {
    static const char unknown[] = "Unknown";

    int i = measurementScaleToIndex(settings.measurementScale);
    if (i >= 0) {
        const char* units = measurementScaleTable[i].measurementScale1NameShort;

        hpa *= measurementScaleTable[i].multiplierMetersToScale1; // Scale1: m->m or m->ft

        bool limited = false;
        if (limit && (hpa > measurementScaleTable[i].reportingLimitScale1)) // Limit the reported accuracy (Scale1)
        {
            limited = true;
            hpa = measurementScaleTable[i].reportingLimitScale1;
        }

        if (hpa <= measurementScaleTable[i].changeFromScale1To2At) // Scale2: m->m or ft->in
        {
            hpa *= measurementScaleTable[i].multiplierScale1To2;
            units = measurementScaleTable[i].measurementScale2NameShort;
        }

        snprintf(buffer, length, "%s%.*f", limited ? "> " : "", decimals, hpa);
        return units;
    }

    strncpy(buffer, unknown, length);
    return unknown;
}
