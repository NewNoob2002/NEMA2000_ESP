#include "States.h"
#include "Bluetooth.h"
#include "GNSS.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "esp32-hal.h"
#include "mcu_settings.h"
#include "myWebServer.h"

#define RTK_MODE(mode)     RTK_MODE = mode;

#define EQ_RTK_MODE(mode)  (RTK_MODE && (RTK_MODE == (mode & RTK_MODE)))
#define NEQ_RTK_MODE(mode) ((RTK_MODE == 0) || ((mode & RTK_MODE) == 0))
//----------------------------------------
// Constants
//----------------------------------------
const RTK_MODE_ENTRY_T stateModeTable[] = {
    {"Rover", STATE_ROVER_NOT_STARTED, STATE_ROVER_RTK_FIX},
    {"Base Caster", STATE_BASE_CASTER_NOT_STARTED, STATE_BASE_CASTER_NOT_STARTED},
    {"Base Assist", STATE_BASE_ASSIST_NOT_STARTED, STATE_BASE_ASSIST_NOT_STARTED},
    {"Base", STATE_BASE_NOT_STARTED, STATE_BASE_FIXED_TRANSMITTING},
    {"WebConfig", STATE_WEB_CONFIG_NOT_STARTED, STATE_PROFILE}, // Covers SETUP, WEB_CONFIG, TEST
#ifdef COMPILE_NTP
    {"NTP", STATE_NTPSERVER_NOT_STARTED, STATE_NTPSERVER_SYNC},
#endif
    {"Shutdown", STATE_SHUTDOWN, STATE_SHUTDOWN}};

const int stateModeTableEntries = sizeof(stateModeTable) / sizeof(stateModeTable[0]);
//----------------------------------------
// Locals
//----------------------------------------
volatile SystemState_t systemState = STATE_NOT_SET;
volatile RTK_MODE_t RTK_MODE = RTK_MODE_ROVER;
SystemState_t lastSystemState = STATE_NOT_SET;
SystemState_t requestedSystemState = STATE_NOT_SET;
bool newSystemStateRequested = false;

static uint32_t lastStateTime = 0;
uint32_t lastSystemStateUpdate = 0;
bool forceSystemStateUpdate = false; // Set true to avoid update wait

void
stateInit() {
    systemState = STATE_NOT_SET;
}

// Given the current state, see if conditions have moved us to a new state
// A user pressing the mode button (change between rover/base) is handled by buttonCheckTask()
void
stateUpdate(UnicoreUM980* gnss) {
    if (((millis() - lastSystemStateUpdate) > 500) || (forceSystemStateUpdate == true)) {
        lastSystemStateUpdate = millis();
        forceSystemStateUpdate = false;
        const bool gnssReady = (gnss != nullptr) && online_devices.gnss;

        // Check to see if any external sources need to change state
        if (newSystemStateRequested == true) {
            newSystemStateRequested = false;
            if (systemState != requestedSystemState) {
                changeState(requestedSystemState);
                lastStateTime = millis();
            }
        }

        if (settings.enablePrintStates && ((millis() - lastStateTime) > 15000)) {
            changeState(systemState);
            lastStateTime = millis();
        }

        switch (systemState) {
            default: {
                systemPrintf("Unknown state: %d\n", systemState);
            } break;
            /* ROVER STATES */
            case (STATE_ROVER_NOT_STARTED): {
                RTK_MODE(RTK_MODE_ROVER);

                gnssConfigure(GNSS_CONFIG_ROVER, __FILE__, __LINE__);

                if (gnssReady == false) {
                    changeState(STATE_ROVER_NO_FIX);
                } else {
                    settings.lastState = STATE_ROVER_NOT_STARTED;
                    // recordSystemSettings(); // Record this state for next POR
                    changeState(STATE_ROVER_CONFIG_WAIT);
                }
            } break;
            case (STATE_ROVER_CONFIG_WAIT): {
                if (gnssConfigureComplete()) {
                    systemPrintln("Rover configured");
                    changeState(STATE_ROVER_NO_FIX);
                }
            } break;
            case (STATE_ROVER_NO_FIX): {
                if (gnssReady && gnss->isFixed()) {
                    changeState(STATE_ROVER_FIX);
                }
            } break;
            case (STATE_ROVER_FIX): {
                if (!gnssReady || !gnss->isFixed()) {
                    changeState(STATE_ROVER_NO_FIX);
                } else if (gnss->isRTKFloat()) {
                    changeState(STATE_ROVER_RTK_FLOAT);
                } else if (gnss->isRTKFix()) {
                    changeState(STATE_ROVER_RTK_FIX);
                }
            } break;
            case (STATE_ROVER_RTK_FLOAT): {
                if (!gnssReady || !gnss->isFixed()) {
                    changeState(STATE_ROVER_NO_FIX);
                } else if (gnss->isRTKFix() == false && gnss->isRTKFloat() == false) { // No RTK
                    changeState(STATE_ROVER_FIX);
                } else if (gnss->isRTKFix() == true) {
                    changeState(STATE_ROVER_RTK_FIX);
                }
            } break;
            case (STATE_ROVER_RTK_FIX): {
                if (!gnssReady || !gnss->isFixed()) {
                    changeState(STATE_ROVER_NO_FIX);
                } else if (gnss->isRTKFix() == false && gnss->isRTKFloat() == false) { // No RTK
                    changeState(STATE_ROVER_FIX);
                } else if (gnss->isRTKFloat()) {
                    changeState(STATE_ROVER_RTK_FLOAT);
                }
            } break;
            /* BASE STATES */
            case (STATE_BASE_CASTER_NOT_STARTED): {
                settings.baseCasterOverride = true;
                changeState(STATE_BASE_NOT_STARTED);
            } break;
                // User wants to switch to fixed base, using the current position as
                // the fixed base position.
                // Note: this works when switching from Rover (e.g. with RTK Fix)
                //       or when switching from Temporary Base (after Survey-In)
            case (STATE_BASE_ASSIST_NOT_STARTED): {
                RTK_MODE(RTK_MODE_BASE_UNDECIDED);
                if (!online_devices.gnss) {
                    return;
                }
                // Copy current position into fixed base position
                settings.fixedBase = true;
                if (settings.fixedBaseCoordinateType == COORD_TYPE_GEODETIC) {
                    settings.fixedLat = gnss->getLatitude();
                    settings.fixedLong = gnss->getLongitude();

                    // See issue #809
                    // gnss->getAltitude() will always return Height above ellipsoid
                    // even if the underlying library getAltitude does not
                    settings.fixedAltitude = gnss->getAltitude();

                    // Subtract the antennaHeight and antennaPhaseCenter
                    // settings.fixedAltitude is the pole tip altitude, not the GNSS antenna altitude
                    settings.fixedAltitude -= ((settings.antennaHeight_mm + settings.antennaPhaseCenter_mm) / 1000.0);

                    systemPrint("Switching to Fixed Base mode using:");
                    systemPrint(" Lat: ");
                    systemPrint(settings.fixedLat, 8);
                    systemPrint(", Lon: ");
                    systemPrint(settings.fixedLong, 8);
                    systemPrint(", Alt: ");
                    systemPrintln(settings.fixedAltitude, 4);
                } else {
                    double ecefX = 0;
                    double ecefY = 0;
                    double ecefZ = 0;
                    // Don't subtract antennaHeight_mm + antennaPhaseCenter_mm
                    geodeticToEcef(gnss->getLatitude(), gnss->getLongitude(), gnss->getAltitude(), &ecefX, &ecefY,
                                   &ecefZ);
                    settings.fixedEcefX = ecefX;
                    settings.fixedEcefY = ecefY;
                    settings.fixedEcefZ = ecefZ;

                    systemPrint("Switching to Fixed Base mode using ECEF: ");
                    systemPrint(settings.fixedEcefX, 4);
                    systemPrint(",");
                    systemPrint(settings.fixedEcefY, 4);
                    systemPrint(",");
                    systemPrintln(settings.fixedEcefZ, 4);
                }
                // STATE_BASE_NOT_STARTED will record settings for next POR
                changeState(STATE_BASE_NOT_STARTED);
            } break;
            case (STATE_BASE_NOT_STARTED): {
                // Mark RTK_MODE as BASE_UNDECIDED to avoid starting NTRIP Client when we may not need it
                RTK_MODE(RTK_MODE_BASE_UNDECIDED);
                if (!online_devices.gnss) {
                    return;
                }
                gnssConfigure(GNSS_CONFIG_BASE, __FILE__, __LINE__);
                changeState(STATE_BASE_CONFIG_WAIT);
            } break;
            case (STATE_BASE_CONFIG_WAIT): {
                if (gnssConfigureComplete()) {
                    systemPrintln("Base configured");

                    if (settings.fixedBase == false) {
                        changeState(STATE_BASE_TEMP_SETTLE);
                        RTK_MODE(RTK_MODE_BASE_SURVEY_IN); // Now allow NTRIP Client to start
                    } else {
                        gnssConfigure(GNSS_CONFIG_BASE_FIXED, __FILE__, __LINE__); // Request start of fixed base
                        changeState(STATE_BASE_FIXED_NOT_STARTED);
                        RTK_MODE(RTK_MODE_BASE_FIXED); // Now allow NTRIP Server to start
                    }
                }
            } break;
            case (STATE_BASE_TEMP_SETTLE): {
                int siv = gnss->getSatellitesInView();
                float hpa = gnss->getHorizontalAccuracy();

                // Check for horizontal accuracy threshold before starting survey in
                char accuracy[20];
                char temp[20];
                const char* units = getHpaUnits(hpa, temp, sizeof(temp), 2, true);

                // surveyInStartingAccuracy is 10m max
                const char* accUnits =
                    getHpaUnits(settings.surveyInStartingAccuracy, accuracy, sizeof(accuracy), 2, false);

                systemPrintf("Waiting for Horz Accuracy < %s (%s): %s%s%s%s, SIV: %d\n", accuracy, accUnits, temp,
                             (accUnits != units) ? " (" : "", (accUnits != units) ? units : "",
                             (accUnits != units) ? ")" : "", siv);
                // On the mosaic-X5, the HPA is undefined while the GNSS is determining its fixed position
                // We need to skip the HPA check...
                if (hpa > 0.0 && hpa < settings.surveyInStartingAccuracy) {
                    gnssConfigure(GNSS_CONFIG_BASE_SURVEY, __FILE__,
                                  __LINE__); // Request reconfigure to base survey in mode

                    changeState(STATE_BASE_TEMP_SURVEY_STARTED);
                }
            } break;
            case (STATE_BASE_TEMP_SURVEY_STARTED): {
                // Get the data once to avoid duplicate slow responses
                int observationTime = gnss->getSurveyInObservationTimeSeconds();
                float meanAccuracy = gnss->getSurveyInMeanAccuracy();
                int siv = gnss->getSatellitesInView();

                if (gnss->isSurveyInComplete() == true) // Survey in complete
                {
                    systemPrintf("Observation Time: %d\n", observationTime);
                    systemPrintln("Base survey complete! RTCM now broadcasting.");

                    // baseStatusLedOn(); // Indicate survey complete

                    // Start the NTRIP server if requested
                    RTK_MODE(RTK_MODE_BASE_FIXED);

                    // rtcmPacketsSent = 0; // Reset any previous number
                    changeState(STATE_BASE_TEMP_TRANSMITTING);
                } else {
                    char temp[20];
                    const char* units = getHpaUnits(meanAccuracy, temp, sizeof(temp), 3, true);
                    systemPrintf("Time elapsed: %d Accuracy (%s): %s SIV: %d\n", observationTime, units, temp, siv);

                    if (observationTime > 60UL * 15UL) {
                        systemPrintf("Survey-In took more than %d minutes. Returning to rover mode.\n",
                                     60UL * 15UL / 60UL);

                        if (gnss->surveyInReset() == false) {
                            systemPrintln("Survey reset failed - attempt 1/3");
                            if (gnss->surveyInReset() == false) {
                                systemPrintln("Survey reset failed - attempt 2/3");
                                if (gnss->surveyInReset() == false) {
                                    systemPrintln("Survey reset failed - attempt 3/3");
                                }
                            }
                        }

                        changeState(STATE_ROVER_NOT_STARTED);
                    }
                }
            } break;

            // Leave base temp transmitting over external radio, or WiFi/NTRIP, or ESP NOW
            case (STATE_BASE_TEMP_TRANSMITTING): {
            } break;
            case (STATE_BASE_FIXED_NOT_STARTED): {
                if (gnssConfigureComplete()) {
                    // baseStatusLedOn(); // Turn on the base/status LED
                    changeState(STATE_BASE_FIXED_TRANSMITTING);
                }
            } break;
            // Leave base fixed transmitting if user has enabled WiFi/NTRIP
            case (STATE_BASE_FIXED_TRANSMITTING): {
            } break;

            /* WEB CONFIG STATES */
            case (STATE_WEB_CONFIG_NOT_STARTED): {
                if (lastSystemState != STATE_WEB_CONFIG_NOT_STARTED) {
                    delay(500);
                }
                bluetoothEnd();   // Release Bluetooth memory before starting WiFi/WebServer resources.
                webServerStart(); // Start the webserver state machine for web config
                RTK_MODE(RTK_MODE_WEB_CONFIG);
                changeState(STATE_WEB_CONFIG_WAIT_FOR_NETWORK);
            } break;
            case (STATE_WEB_CONFIG_WAIT_FOR_NETWORK): {
                changeState(STATE_WEB_CONFIG);
            } break;
            case (STATE_WEB_CONFIG): break;
            case (STATE_PROFILE): {
                // Do nothing - display only
            } break;
#ifdef COMPILE_NTP
            case (STATE_NTPSERVER_NOT_STARTED): {
                RTK_MODE(RTK_MODE_NTP);
                changeState(STATE_NTPSERVER_NO_SYNC);
            } break;
            case (STATE_NTPSERVER_NO_SYNC): {
                if (gnssReady && gnss->isValidTime()) {
                    changeState(STATE_NTPSERVER_SYNC);
                }
            } break;
            case (STATE_NTPSERVER_SYNC): {
                if (!gnssReady || !gnss->isValidTime()) {
                    changeState(STATE_NTPSERVER_NO_SYNC);
                }
            } break;
#endif
            case (STATE_SHUTDOWN): break;
            case (STATE_NOT_SET): {
                gnssConfigure(GNSS_CONFIG_ONCE, __FILE__, __LINE__);
                changeState(STATE_ROVER_NOT_STARTED);
            } break;
        }
    }
}

// System state changes may only occur within main state machine
// To allow state changes from external sources (ie, Button Tasks) requests can be made
// Requests are handled at the start of stateUpdate()
void
requestChangeState(SystemState_t requestedState) {
    requestedSystemState = requestedState;
    newSystemStateRequested = true;
    systemPrintf("Requested System State: %d\n", requestedSystemState);
}

SystemState_t
getSystemStateForReporting() {
    return newSystemStateRequested ? requestedSystemState : systemState;
}

// Print the current state
const char*
getState(SystemState_t state) {
    switch (state) {
        default: return "UNKNOWN";

        case (STATE_ROVER_NOT_STARTED): return "STATE_ROVER_NOT_STARTED";
        case (STATE_ROVER_CONFIG_WAIT): return "STATE_ROVER_CONFIG_WAIT";
        case (STATE_ROVER_NO_FIX): return "STATE_ROVER_NO_FIX";
        case (STATE_ROVER_FIX): return "STATE_ROVER_FIX";
        case (STATE_ROVER_RTK_FLOAT): return "STATE_ROVER_RTK_FLOAT";
        case (STATE_ROVER_RTK_FIX): return "STATE_ROVER_RTK_FIX";
        case (STATE_BASE_CASTER_NOT_STARTED): return "STATE_BASE_CASTER_NOT_STARTED";
        case (STATE_BASE_ASSIST_NOT_STARTED): return "STATE_BASE_ASSIST_NOT_STARTED";
        case (STATE_BASE_NOT_STARTED): return "STATE_BASE_NOT_STARTED";
        case (STATE_BASE_CONFIG_WAIT): return "STATE_BASE_CONFIG_WAIT";
        case (STATE_BASE_TEMP_SETTLE): return "STATE_BASE_TEMP_SETTLE";
        case (STATE_BASE_TEMP_SURVEY_STARTED): return "STATE_BASE_TEMP_SURVEY_STARTED";
        case (STATE_BASE_TEMP_TRANSMITTING): return "STATE_BASE_TEMP_TRANSMITTING";
        case (STATE_BASE_FIXED_NOT_STARTED): return "STATE_BASE_FIXED_NOT_STARTED";
        case (STATE_BASE_FIXED_TRANSMITTING): return "STATE_BASE_FIXED_TRANSMITTING";

        case (STATE_WEB_CONFIG_NOT_STARTED): return "STATE_WEB_CONFIG_NOT_STARTED";
        case (STATE_WEB_CONFIG_WAIT_FOR_NETWORK):
        case (STATE_WEB_CONFIG): return "STATE_WEB_CONFIG";
        case (STATE_PROFILE): return "STATE_PROFILE";
#ifdef COMPILE_NTP
        case (STATE_NTPSERVER_NOT_STARTED): return "STATE_NTPSERVER_NOT_STARTED";
        case (STATE_NTPSERVER_NO_SYNC): return "STATE_NTPSERVER_NO_SYNC";
        case (STATE_NTPSERVER_SYNC): return "STATE_NTPSERVER_SYNC";
#endif
        case (STATE_SHUTDOWN): return "STATE_SHUTDOWN";
        case (STATE_NOT_SET): return "STATE_NOT_SET";
    }
}

// Change states and print the new state
void
changeState(SystemState_t newState) {
    const char* arrow = "";
    const char* asterisk = "";
    const char* initialState = "";
    const char* endingState = "";

    // Log the heap size at the state change
    reportHeapNow(false);

    // Debug print of new state, add leading asterisk for repeated states
    if ((!settings.enablePrintDuplicateStates) && (newState == systemState)) {
        return;
    }

    if (settings.enablePrintStates) {
        arrow = "";
        asterisk = "";
        initialState = "";
        if (newState == systemState) {
            asterisk = "*";
        } else {
            initialState = getState(systemState);
            arrow = " --> ";
        }
    }
    lastSystemState = systemState; // Save the previous state for reference
    systemState = newState;        // Set the new state
    if (settings.enablePrintStates) {
        endingState = getState(newState);

        if (!online_devices.rtc) {
            systemPrintf("[State] %s%s%s%s\n", asterisk, initialState, arrow, endingState);
        } else {
            // Timestamp the state change
            systemPrintf("[State] %s%s%s%s, %s\n", asterisk, initialState, arrow, endingState, getTimeStamp());
        }
    }
}

const char*
stateToRtkMode(SystemState_t state) {
    const RTK_MODE_ENTRY_T* mode;

    static char modeName[20];

    strncpy(modeName, "Unknown Mode", sizeof(modeName) - 1); // Reset to unknown at each function call

    // Walk the RTK mode table
    for (int index = 0; index < stateModeTableEntries; index++) {
        mode = &stateModeTable[index];
        if ((state >= mode->first) && (state <= mode->last)) {
            snprintf(modeName, sizeof(modeName), "%s", mode->modeName);
        }
    }

    // Base Cast mode looks like Base mode to the table lookup, but we want to report it differently
    if (state >= STATE_BASE_NOT_STARTED && state <= STATE_BASE_FIXED_TRANSMITTING) {
        if (settings.baseCasterOverride == true) {
            return "Base Caster";
        }
    }

    return (const char*)modeName;
}

bool
inRoverMode() {
    if (systemState >= STATE_ROVER_NOT_STARTED && systemState <= STATE_ROVER_RTK_FIX) {
        return (true);
    }
    return (false);
}

bool
inBaseMode() {
    if (systemState >= STATE_BASE_CASTER_NOT_STARTED && systemState <= STATE_BASE_FIXED_TRANSMITTING) {
        return (true);
    }
    return (false);
}

bool
inWebConfigMode() {
    if (systemState >= STATE_WEB_CONFIG_NOT_STARTED && systemState <= STATE_WEB_CONFIG) {
        return (true);
    }
    return (false);
}

#ifdef COMPILE_NTP
bool
inNtpMode() {
    if (systemState >= STATE_NTPSERVER_NOT_STARTED && systemState <= STATE_NTPSERVER_SYNC) {
        return (true);
    }
    return (false);
}
#endif
