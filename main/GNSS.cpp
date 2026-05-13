#include "GNSS.h"
#include <Arduino.h>
#include "HAL_Config.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "mcu_settings.h"

//----------------------------------------
// Constants
//----------------------------------------

static const char* gnssConfigDisplayNames[] = {
    "ONCE",
    "ROVER",
    "BASE",
    "BASE SURVEY",
    "BASE FIXED",
    "BAUD_RATE_RADIO",
    "BAUD_RATE_DATA",
    "RATE",
    "CONSTELLATION",
    "ELEVATION",
    "CN0",
    "PPS",
    "MODEL",
    "MESSAGE_RATE_NMEA",
    "MESSAGE_RATE_RTCM_ROVER",
    "MESSAGE_RATE_RTCM_BASE",
    "MESSAGE_RATE_RTCM_OTHER",
    "PPP_HAS_B2B",
    "MULTIPATH",
    "TILT",
    "EXT_CORRECTIONS",
    "LOGGING",
    "SAVE",
    "RESET",
};

static const int gnssConfigStateEntries = sizeof(gnssConfigDisplayNames) / sizeof(gnssConfigDisplayNames[0]);

//----------------------------------------
// Locals
//----------------------------------------

void
gnssBegin(HardwareSerial* pGnssSerial, UnicoreUM980* pUm980) {
    if (settings.printTaskStartStop) {
        systemPrintln("Task pinGnssUartTask started");
    }

    if (pGnssSerial == nullptr) {
        pGnssSerial = new HardwareSerial(1);
        pGnssSerial->setRxBufferSize(settings.uartReceiveBufferSize);
        pGnssSerial->setTimeout(settings.serialTimeoutGNSS);
        pGnssSerial->setRxFIFOFull(settings.serialGNSSRxFullThreshold);
        pGnssSerial->begin(settings.dataPortBaud, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
    }
    if (settings.printTaskStartStop) {
        systemPrintln("Task pinGnssUartTask stopped");
    }

    if (settings.printTaskStartStop) {
        systemPrintln("Unicore GNSS library initialization started");
    }

    if (pUm980 == nullptr) {
        pUm980 = new UnicoreUM980(GNSS_POWER_PIN);
    }

    pUm980->enableDebugLogging(Serial, UnicoreLogLevel::Debug,
                               UNICORE_LOG_COMMAND | UNICORE_LOG_DATA | UNICORE_LOG_TASK);
    // pUm980->setNmeaCallback(OnNmea);
    // pUm980->setRtcmCallback(OnRtcm);
    // pUm980->setBinaryCallback(OnBinary);
    pUm980->init();
    pUm980->powerOn();
    if (pUm980->begin(*pGnssSerial, nullptr, &Serial)) {
        if (pUm980->requestVersion(2000) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            online_devices.gnss = true;
        } else {
            systemPrintln("Failed to get GNSS version");
        }
    }

    if (settings.printTaskStartStop) {
        systemPrintln("Unicore GNSS library initialization stopped");
    }
    // Nothing to do here since the GNSS library is initialized lazily
}

void
gnssUpdate(UnicoreUM980* pUm980) {
    if (!online_devices.gnss) {
        return;
    }
    if (!pUm980) {
        return;
    }
}

// Given a bit to configure, set that bit in the overall bitfield
void
gnssConfigure(uint32_t configureBit) {
    uint32_t mask = (1 << configureBit);
    settings.gnssConfigureRequest |= mask; // Set the bit
}