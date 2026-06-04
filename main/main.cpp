/*
 * NMEA0183 GNSS to NMEA2000 gateway for ESP32.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "App/App.h"
#include "HAL/HAL.h"

extern "C" void
app_main(void) {
    HAL::HAL_Init();
    App_Init();

    while (true) {
        HAL::HAL_Update(nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
