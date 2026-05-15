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
                               UNICORE_LOG_COMMAND | UNICORE_LOG_DATA | UNICORE_LOG_TASK | UNICORE_LOG_CHILD_CLASS);
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
        if (settings.debugGnssConfig == true) {
            systemPrintln("GNSS not online, skipping update");
        }
        return;
    }
    if (!gnss) {
        if (settings.debugGnssConfig == true) {
            systemPrintln("GNSS instance not available, skipping update");
        }
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
            uint8_t currentModel = gnss->configureRover();
            uint8_t needChange = 0;
            if (currentModel != 0) {
                //  0 - Unknown, 1 - Rover Survey, 2 - Rover UAV, 3 - Rover Auto, 4 - Base Survey-in, 5 - Base fixed
                // if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_SURVEY && currentModel == 1) {
                //     needChange = 1;
                // }
                // if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_UAV && currentModel == 2) {
                //     needChange = 1;
                // }
                // if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_AUTOMOTIVE && currentModel == 3) {
                //     needChange = 1;
                // }
                if (currentModel == 4 || currentModel == 5) {
                    // We are in a Base mode, need to change to Rover
                    needChange = 1;
                    settings.dynamicModel = UM980_DYN_MODEL_ROVER_SURVEY;
                }
                if (needChange) {
                    // Assume we are changing from Base to Rover, request any additional config changes
                    // Sets the dynamic model (Survey/UAV/Automotive) and puts the device into Rover mode
                    systemPrintf("GNSS model need change. Current: %d, Desired: %d\r\n", currentModel,
                                 settings.dynamicModel);
                    gnssConfigure(GNSS_CONFIG_MODEL);

                    // Request a change to Rover RTCM
                    gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER);
                }
                gnssConfigureClear(GNSS_CONFIG_ROVER);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA); // Request update to NMEA
                gnssConfigure(GNSS_CONFIG_SAVE);              // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE)) {
            if (gnss->configureBase() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BASE);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE_SURVEY)) {
            if (gnss->configureBase() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BASE_SURVEY);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BASE_FIXED)) {
            if (gnss->configureBase() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BASE_FIXED);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BAUD_RATE_DATA)) {
            if (gnss->setPortBaudrate(UnicorePort::Current, settings.dataPortBaud)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BAUD_RATE_DATA);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_BAUD_RATE_RADIO)) {
            if (gnss->setPortBaudrate(UnicorePort::Com2, settings.radioPortBaud)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_BAUD_RATE_RADIO);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_FIX_RATE)) {
            const double rateSeconds = static_cast<double>(settings.measurementRateMs) / MILLISECONDS_IN_A_SECOND;
            if (gnss->setRate(rateSeconds) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_FIX_RATE);
                gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_NMEA);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_CONSTELLATION)) {
            if (gnss->setConstellations() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_CONSTELLATION);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_ELEVATION)) {
            if (gnss->setElevation(settings.minElev) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_ELEVATION);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_CN0)) {
            if (gnss->setMinCno(settings.minCN0) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_CN0);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MULTIPATH)) {
            if (gnss->setMultipathMitigation(settings.enableMultipathMitigation)
                == Unicore_RESULT_RESPONSE_COMMAND_OK) {
                gnssConfigureClear(GNSS_CONFIG_MULTIPATH);
                gnssConfigure(GNSS_CONFIG_SAVE);
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_NMEA)) {
            if (gnss->setMessagesNMEA() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_NMEA);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER)) {
            if (settings.debugGnssConfig == true && gnss->gnssInRoverMode() == false) {
                systemPrintln("Warning: Change to RTCM Rover rates requested but not in Rover mode.");
            }

            if (gnss->setMessagesRTCMRover() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE)) {
            if (settings.debugGnssConfig == true) {
                if (gnss->gnssInBaseFixedMode() == false && gnss->gnssInBaseSurveyInMode() == false) {
                    systemPrintln("Warning: Change to RTCM Base rates requested but not in Base mode.");
                }
            }

            if (gnss->setMessagesRTCMBase() == true) {
                gnssConfigureClear(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE);
                gnssConfigure(GNSS_CONFIG_SAVE); // Request receiver commit this change to NVM
                // setLoggingType();                // Update Standard, PPP, or custom for icon selection
            }
        }

        if (gnssConfigureRequested(GNSS_CONFIG_MESSAGE_RATE_OTHER)) {
            gnssConfigureUnsupported(GNSS_CONFIG_MESSAGE_RATE_OTHER);
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
                systemPrint("Remaining gnssConfigureRequest: ");

                for (int x = 0; x < GNSS_CONFIG_MAX; x++) {
                    if (settings.gnssConfigureRequest & (1UL << x)) {
                        systemPrintf("%s ", gnssConfigName(x));
                    }
                }
                systemPrintln();
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
        systemPrintf("GNSS configuration in progress, skipping update\r\n");
    }
}

// Given a bit to configure, set that bit in the overall bitfield
void
gnssConfigure(uint32_t configureBit) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            systemPrintf("GNSS Config Set rejected: invalid bit %lu\r\n", static_cast<unsigned long>(configureBit));
        }
        return;
    }

    uint32_t mask = (1UL << configureBit);
    settings.gnssConfigureRequest |= mask; // Set the bit
}

// Given a bit to configure, clear that bit from the overall bitfield
void
gnssConfigureClear(uint32_t configureBit) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            systemPrintf("GNSS Config Clear rejected: invalid bit %lu\r\n", static_cast<unsigned long>(configureBit));
        }
        return;
    }

    uint32_t mask = (1UL << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        systemPrintf("GNSS Config Clear: %s\r\n", gnssConfigName(configureBit));
    }

    settings.gnssConfigureRequest &= ~mask; // Clear the bit
}

// Return true if a given bit is set
bool
gnssConfigureRequested(uint32_t configureBit) {
    if (!gnssConfigBitValid(configureBit)) {
        if (settings.debugGnssConfig) {
            systemPrintf("GNSS Config Request rejected: invalid bit %lu\r\n", static_cast<unsigned long>(configureBit));
        }
        return false;
    }

    uint32_t mask = (1UL << configureBit);

    if (settings.debugGnssConfig && (settings.gnssConfigureRequest & mask)) {
        systemPrintf("GNSS Config Request: %s\r\n", gnssConfigName(configureBit));
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
