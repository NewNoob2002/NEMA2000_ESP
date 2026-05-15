#include "GNSS.h"
#include <Arduino.h>
#include "HAL_Config.h"
#include "States.h"
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
volatile bool gnssConfigureInProgress = false;

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
            pUm980->isOnline(true);
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
gnssUpdate(UnicoreUM980* gnss) {
    if (!online_devices.gnss) {
        return;
    }
    if (!gnss) {
        return;
    }

    if (gnssConfigureComplete() == true) {
        // We need to establish the logging type:
        //  After a device has completed boot up (the GNSS may or may not have been reconfigured)
        //  After a user changes the message configurations (NMEA, RTCM, or OTHER).
        // if (loggingType == LOGGING_UNKNOWN) {
        //     setLoggingType(); // Update Standard, PPP, or custom for icon selection
        // }

        return; // No configuration requests
    }

    if (inWebConfigMode() == false) {
        gnssConfigureInProgress = true; // Set the 'semaphore'
        bool result = true;

        // Service requests
        // Clear the requests as they are completed successfully
        // If a platform requires a device reset to complete the config (ie, LG290P changing constellations) then
        // the platform specific function should call gnssConfigure(GNSS_CONFIG_RESET)

        if (gnssConfigureRequested(GNSS_CONFIG_ONCE)) {
            if (gnss->configure() == true) {
                gnssConfigureClear(GNSS_CONFIG_ONCE);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }

        // For some receivers (ie, UM980) changing the model changes to Rover/Base.
        // Configure model before setting the mode and message rates
        if (gnssConfigureRequested(GNSS_CONFIG_MODEL)) {
            if (gnss->setModel(settings.dynamicModel) == true) {
                gnssConfigureClear(GNSS_CONFIG_MODEL);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_ROVER)) {
            if (gnss->configureRover() == true) {
                gnssConfigureClear(GNSS_CONFIG_ROVER);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }
    }
}

// Given a bit to configure, set that bit in the overall bitfield
void
gnssConfigure(uint32_t configureBit) {
    uint32_t mask = (1 << configureBit);
    settings.gnssConfigureRequest |= mask; // Set the bit
}

// Given a bit to configure, clear that bit from the overall bitfield
void
gnssConfigureClear(uint32_t configureBit) {
    uint32_t mask = (1 << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        systemPrintf("GNSS Config Clear: %s\r\n", gnssConfigDisplayNames[configureBit]);
    }

    settings.gnssConfigureRequest &= ~mask; // Clear the bit
}

// Return true if a given bit is set
bool
gnssConfigureRequested(uint32_t configureBit) {
    uint32_t mask = (1 << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        systemPrintf("GNSS Config Request: %s\r\n", gnssConfigDisplayNames[configureBit]);
    }

    return (settings.gnssConfigureRequest & mask);
}

// Returns true once all configuration requests are cleared
bool
gnssConfigureComplete() {
    if (settings.gnssConfigureRequest == 0) {
        return (true);
    }
    return (false);
}
