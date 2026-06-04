/*
 * NMEA0183 GNSS to NMEA2000 gateway for ESP32.
 */
#include "HardwareSerial.h"
#include "nmea0183_to_n2k.h"

#include "App/App.h"
#include "HAL/HAL.h"
#include "NMEA2000_esp32.h"
#include "esp_log.h"

constexpr const char* kTag = "[gateway_main]";

bool
SendGatewayMessage(tNMEA2000_esp32& nmea2000, const tN2kMsg& message, const char* name, uint32_t& sent,
                   uint32_t& failed) {
    (void)name;
    if (nmea2000.SendMsg(message)) {
        sent++;
        ESP_LOGI(kTag, "queued %s PGN=%lu", name, message.PGN);
        return true;
    }

    failed++;
    //ESP_LOGW(kTag, "queue failed for %s PGN=%lu", name, message.PGN);
    return false;
}

extern "C" void
app_main(void) {
    HAL::HAL_Init();
    App_Init();

    static tNMEA2000_esp32 nmea2000;
    ConfigureGatewayNmea2000(nmea2000);
    nmea2000.ClearCANStatus();
    nmea2000.Open();

    while (true) {
        HAL::HAL_Update(nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
