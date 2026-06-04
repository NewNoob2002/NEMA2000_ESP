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
    // Serial.begin(115200);
    static tGatewayNmea0183Parser nmeaParser;
    if (!nmeaParser.Begin()) {
        ESP_LOGE(kTag, "NMEA0183_PARSER_START_FAIL");
    }

    static tNMEA2000_esp32 nmea2000;
    ConfigureGatewayNmea2000(nmea2000);
    nmea2000.ClearCANStatus();
    nmea2000.Open();

    ESP_LOGI(kTag, "NMEA2000_CAN_OPEN_PASS source=%u", nmea2000.GetN2kSource());
    ESP_LOGI(kTag, "NMEA0183_RUNTIME_READY feed UART bytes into tGatewayNmea0183Parser::FeedByte()");

    uint32_t sent = 0;
    uint32_t failed = 0;

    while (true) {
        nmea2000.ParseMessages();

        tGatewayN2kMessages messages;
        if (nmeaParser.TakeMessages(messages)) {
            SendGatewayMessage(nmea2000, messages.LatLonRapid, "GNSS rapid position", sent, failed);
            SendGatewayMessage(nmea2000, messages.CogSogRapid, "COG/SOG rapid", sent, failed);
            SendGatewayMessage(nmea2000, messages.Gnss, "GNSS position data", sent, failed);
        }
        HAL::HAL_Update(nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
