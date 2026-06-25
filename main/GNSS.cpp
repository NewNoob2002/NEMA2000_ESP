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
#define TAG "[GNSS] "

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
static uint32_t lastGnssConfigReportTime = 0;

static const char*
gnssConfigName(const uint32_t configureBit) {
    if (configureBit < gnssConfigStateEntries) {
        return gnssConfigDisplayNames[configureBit];
    }
    return "UNKNOWN";
}

static bool
gnssConfigBitValid(const uint32_t configureBit) {
    return configureBit < GNSS_CONFIG_MAX;
}

static void
gnssConfigureUnsupported(const uint32_t configureBit) {
    if (settings.debugGnssConfig) {
        systemPrintf("GNSS Config Unsupported: %s\r\n", gnssConfigName(configureBit));
    }
    gnssConfigureClear(configureBit);
}

void
gnssBegin(HardwareSerial*& pGnssSerial, UnicoreUM980*& pUm980) {
    if (settings.printTaskStartStop) {
        systemPrintln("Task pinGnssUartTask started");
    }

    if (pGnssSerial == nullptr) {
        ESP_LOGI(TAG, "GNSS UART initialized on RX: %d, TX: %d at %d baud", GNSS_RX_PIN, GNSS_TX_PIN,
                 settings.dataPortBaud);
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
    pUm980->enableDebugLogging(UnicoreLogLevel::Debug,
                               UNICORE_LOG_COMMAND | UNICORE_LOG_TX | UNICORE_LOG_DATA | UNICORE_LOG_CHILD_CLASS);
    pUm980->init();
    pUm980->powerOn();
    delay(2000); // Wait for the GNSS to power up
    if (pUm980->begin(*pGnssSerial)) {
        if (pUm980->requestVersion(2000) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            online_devices.gnss = true;
            pUm980->setOnline(true);
        } else {
            ESP_LOGE(TAG, "Failed to get GNSS version");
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize GNSS, deleting instance");
        delete pUm980;
        pUm980 = nullptr;
    }
    if (settings.printTaskStartStop) {
        systemPrintln("Unicore GNSS library initialization stop");
    }
    // Nothing to do here since the GNSS library is initialized lazily
}

void
gnssUpdate(UnicoreUM980* gnss) {
    if (!online_devices.gnss || !gnss) {
        return;
    }

    if (gnssConfigureComplete() == true) {
        // We need to establish the logging type:
        //  After a device has completed boot up (the GNSS may or may not have been reconfigured)
        //  After a user changes the message configurations (NMEA, RTCM, or OTHER).
        // if (loggingType == LOGGING_UNKNOWN) {
        //     setLoggingType(); // Update Standard, PPP, or custom for icon selection
        // }

        gnss->ensureBinaryNavigationMessages();
        return; // No configuration requests
    }

    if (inWebConfigMode() == false) {
        gnssConfigureInProgress = true; // Set the 'semaphore'
        // Service requests
        // Clear the requests as they are completed successfully
        // If a platform requires a device reset to complete the config (ie, LG290P changing constellations) then
        // the platform specific function should call gnssConfigure(GNSS_CONFIG_RESET)

        if (gnssConfigureRequested(GNSS_CONFIG_ONCE)) {
            if (gnss->configureReceiver() == true) {
                gnssConfigureClear(GNSS_CONFIG_ONCE);
                gnssConfigure(GNSS_CONFIG_ELEVATION, __FILE__, __LINE__);
            }
        }

        // For some receivers (ie, UM980) changing the model changes to Rover/Base.
        // Configure model before setting the mode and message rates
        if (gnssConfigureRequested(GNSS_CONFIG_MODEL)) {
            if (gnss->applyDynamicModel(settings.dynamicModel) == true) {
                gnssConfigureClear(GNSS_CONFIG_MODEL);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_ROVER)) {
            if (gnss->prepareRoverMode()) {
                gnssConfigureClear(GNSS_CONFIG_ROVER);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA, __FILE__, __LINE__);            // Request update to NMEA
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_BASEINFOA_ROVER, __FILE__, __LINE__); // Request update to NMEA
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE)) {
            if (gnss->prepareBaseMode()) {
                gnssConfigureClear(GNSS_CONFIG_BASE);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA, __FILE__, __LINE__);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE_SURVEY)) {
            if (gnss->startSurveyIn()) {
                gnssConfigureClear(GNSS_CONFIG_BASE_SURVEY);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE_FIXED)) {
            if (gnss->startFixedBase()) {
                gnssConfigureClear(GNSS_CONFIG_BASE_FIXED);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BAUD_RATE_DATA)) {
            if (gnss->setPortBaudrate(UnicorePort::Current, settings.dataPortBaud)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BAUD_RATE_DATA);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BAUD_RATE_RADIO)) {
            if (gnss->setPortBaudrate(UnicorePort::Com2, settings.radioPortBaud)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BAUD_RATE_RADIO);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_FIX_RATE)) {
            const double rateSeconds = static_cast<double>(settings.measurementRateMs) / MILLISECONDS_IN_A_SECOND;
            if (gnss->setNavigationRate(rateSeconds) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_FIX_RATE);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA, __FILE__, __LINE__);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_CONSTELLATION)) {
            if (gnss->applyConstellationConfig() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_CONSTELLATION);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_ELEVATION)) {
            if (gnss->applyElevationMask(settings.minElev) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_ELEVATION);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_CN0)) {
            if (gnss->applyMinCno(settings.minCN0) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_CN0);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MULTIPATH)) {
            if (gnss->applyMultipathMitigation(settings.enableMultipathMitigation)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_MULTIPATH);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_NMEA)) {
            if (gnss->applyNmeaMessageConfig() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_NMEA);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER)) {
            if (settings.debugGnssConfig == true && gnss->gnssInRoverMode() == false) {
                ESP_LOGW(TAG, "Warning: Change to RTCM Rover rates requested but not in Rover mode, current mode :%d.",
                         gnss->getDynamicModel());
            }

            if (gnss->applyRoverRtcmMessageConfig() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE)) {
            if (settings.debugGnssConfig == true) {
                if (gnss->gnssInBaseFixedMode() == false && gnss->gnssInBaseSurveyInMode() == false) {
                    ESP_LOGW(TAG,
                             "Warning: Change to RTCM Base rates requested but not in Base mode, current mode :%d.",
                             gnss->getDynamicModel());
                }
            }

            if (gnss->applyBaseRtcmMessageConfig() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_BASEINFOA_ROVER)) {
            if (gnss->applyBaseInfoMessageConfig()) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_BASEINFOA_ROVER);
                gnssConfigure(GNSS_CONFIG_SAVE, __FILE__, __LINE__); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_PPS)) {
            gnssConfigureUnsupported(GNSS_CONFIG_PPS);
        }

        if (gnssConfigureRequested(GNSS_CONFIG_PPP)) {
            gnssConfigureUnsupported(GNSS_CONFIG_PPP);
        }

        if (gnssConfigureRequested(GNSS_CONFIG_TILT)) {
            gnssConfigureUnsupported(GNSS_CONFIG_TILT);
        }

        if (gnssConfigureRequested(GNSS_CONFIG_EXT_CORRECTIONS)) {
            gnssConfigureUnsupported(GNSS_CONFIG_EXT_CORRECTIONS);
        }

        if (gnssConfigureRequested(GNSS_CONFIG_LOGGING)) {
            gnssConfigureUnsupported(GNSS_CONFIG_LOGGING);
        }

        // Save changes to NVM
        if (gnssConfigureRequested(GNSS_CONFIG_SAVE)) {
            if (gnss->saveConfiguration() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_RESET)) {
            gnssConfigureUnsupported(GNSS_CONFIG_RESET);
        }

        // If gnssConfigureRequest bits are still set, the next update will attempt to service them.

        if (settings.gnssConfigureRequest != 0) {
            if (settings.debugGnssConfig && (millis() - lastGnssConfigReportTime > 2000)) {
                lastGnssConfigReportTime = millis();
                ESP_LOGI(TAG, "Remaining gnssConfigureRequest: ");

                for (int x = 0; x < GNSS_CONFIG_MAX; x++) {
                    if (settings.gnssConfigureRequest & (1UL << x)) {
                        ESP_LOGI(TAG, "%s ", gnssConfigName(x));
                    }
                }
                ESP_LOGI(TAG, "");
            }

            // On Facet FP mosaic-X5:
            //   If NTRIP has been connected and corrections have been pushed to the GNSS over COM1
            //   Then the corrections are stopped (e.g. NTRIP is disabled)
            //   COM1 can ignore incoming commands and the above GNSS configuration fails with a timeout
            //   The solution is to send the escape sequence
            // gnss->comPortRefresh();
        }

        // settings.gnssConfigureRequest was likely changed. Record the current config state to ESP32 NVM
        // recordSystemSettings();

        gnssConfigureInProgress = false; // Clear the 'semaphore'
    } else {
        ESP_LOGW(TAG, "GNSS configuration in progress, skipping update");
    }
}

// Given a bit to configure, set that bit in the overall bitfield
void
gnssConfigure(uint32_t configureBit, const char* fileName, uint32_t lineNumber) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            ESP_LOGW(TAG, "GNSS Config Set rejected: invalid bit %lu", static_cast<unsigned long>(configureBit));
        }
        return;
    }
    ESP_LOGI(TAG, "GNSS Config Set: %s in %s:%d", gnssConfigName(configureBit), fileName, lineNumber);
    uint32_t mask = (1UL << configureBit);
    settings.gnssConfigureRequest |= mask; // Set the bit
}

// Given a bit to configure, clear that bit from the overall bitfield
void
gnssConfigureClear(uint32_t configureBit) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            ESP_LOGW(TAG, "GNSS Config Clear rejected: invalid bit %lu", static_cast<unsigned long>(configureBit));
        }
        return;
    }

    uint32_t mask = (1UL << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        ESP_LOGI(TAG, "GNSS Config Clear: %s", gnssConfigName(configureBit));
    }

    settings.gnssConfigureRequest &= ~mask; // Clear the bit
}

// Return true if a given bit is set
bool
gnssConfigureRequested(uint32_t configureBit) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            ESP_LOGW(TAG, "GNSS Config Request rejected: invalid bit %lu", static_cast<unsigned long>(configureBit));
        }
        return false;
    }

    uint32_t mask = (1UL << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        ESP_LOGI(TAG, "GNSS Config Request: %s", gnssConfigName(configureBit));
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
