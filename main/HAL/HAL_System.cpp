#include "HAL.h"
#include "esp32-hal.h"
#include "esp_heap_caps.h"
#include "mcu_settings.h"
#include "myWebServer.h"

static uint32_t lastHeapReport = 0;

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
        reportHeapNow(false);
    }
}

namespace HAL {
void
System_Update() {
    reportHeap();
}
} // namespace HAL
