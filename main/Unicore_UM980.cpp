#include "Unicore_UM980.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "GNSS.h"
#include "States.h"
#include "Unicore_Struct.h"
#include "mcu_settings.h"

namespace {
constexpr uint8_t kPosTypeSingle = 16;
constexpr uint8_t kPosTypePsrDiff = 17;
constexpr uint8_t kPosTypeNarrowFloat = 34;
constexpr uint8_t kPosTypeWideInt = 49;
constexpr uint8_t kPosTypeNarrowInt = 50;

bool
copyModeToken(char* destination, const size_t destinationSize, const char*& cursor) {
    if (!destination || (destinationSize == 0) || !cursor) {
        return false;
    }

    while (*cursor == ' ') {
        cursor++;
    }

    size_t index = 0;
    while (*cursor && (*cursor != ' ') && (*cursor != ',') && (*cursor != '*') && (*cursor != '\r')
           && (*cursor != '\n')) {
        if (index < (destinationSize - 1)) {
            destination[index++] = *cursor;
        }
        cursor++;
    }
    destination[index] = 0;
    return index > 0;
}
} // namespace

UnicoreUM980::UnicoreUM980(gpio_num_t PowerPin) : _powerPin(PowerPin) { resetDefaults(); }

void
UnicoreUM980::init() {
    gpio_config_t ioConfig = {};
    ioConfig.intr_type = GPIO_INTR_DISABLE;
    ioConfig.mode = GPIO_MODE_OUTPUT;
    ioConfig.pin_bit_mask = (1ULL << _powerPin);
    ioConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ioConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&ioConfig);
    gpio_set_level(_powerPin, 0);

    setRtcmCallback(RtcmCallback, this);
    setNmeaCallback(NmeaCallback, this);
    setBinaryCallback(BinaryCallback, this);
    setHashCallback(HashCallback, this);
}

void
UnicoreUM980::powerOn() {
    gpio_set_level(_powerPin, 1);
}

void
UnicoreUM980::powerOff() {
    gpio_set_level(_powerPin, 0);
}

void
UnicoreUM980::isOnline(bool online) {
    _online = online;
}

void
UnicoreUM980::setConnectCom(const char* com) {
    snprintf(_connectCom, sizeof(_connectCom), "%s", com);
}

void
UnicoreUM980::resetDefaults() {
    for (size_t index = 0; index < MAX_UM980_NMEA_MSG; index++) {
        _nmeaPeriods[index] = kUm980NmeaMessages[index].defaultPeriodSeconds;
    }

    for (size_t index = 0; index < MAX_UM980_RTCM_MSG; index++) {
        _rtcmRoverPeriods[index] = kUm980RtcmMessages[index].defaultPeriodSeconds;
        _rtcmBasePeriods[index] = kUm980RtcmMessages[index].defaultPeriodSeconds;
    }

    for (size_t index = 0; index < MAX_UM980_CONSTELLATIONS; index++) {
        _constellationEnabled[index] = true;
    }

    _rateSeconds = 1.0;
}

void
UnicoreUM980::baseRtcmDefault() {
    for (size_t index = 0; index < MAX_UM980_RTCM_MSG; index++) {
        _rtcmBasePeriods[index] = 0.0f;
    }

    setRtcmBaseMessagePeriod("RTCM1005", 1.0f);
    setRtcmBaseMessagePeriod("RTCM1033", 10.0f);
    setRtcmBaseMessagePeriod("RTCM1074", 1.0f);
    setRtcmBaseMessagePeriod("RTCM1084", 1.0f);
    setRtcmBaseMessagePeriod("RTCM1094", 1.0f);
    setRtcmBaseMessagePeriod("RTCM1124", 1.0f);
}

void
UnicoreUM980::baseRtcmLowDataRate() {
    for (size_t index = 0; index < MAX_UM980_RTCM_MSG; index++) {
        _rtcmBasePeriods[index] = 0.0f;
    }

    setRtcmBaseMessagePeriod("RTCM1005", 10.0f);
    setRtcmBaseMessagePeriod("RTCM1033", 10.0f);
    setRtcmBaseMessagePeriod("RTCM1074", 2.0f);
    setRtcmBaseMessagePeriod("RTCM1084", 2.0f);
    setRtcmBaseMessagePeriod("RTCM1094", 2.0f);
    setRtcmBaseMessagePeriod("RTCM1124", 2.0f);
}

bool
UnicoreUM980::configure() {
    for (int x = 0; x < 3; x++) {
        if (configureOnceTime() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            return true;
        }
        delay(1000);
        log(UnicoreLogLevel::Warn, UNICORE_LOG_CHILD_CLASS, "Configuration attempt %d failed, retrying...", x + 1);

        //To Do: reset module
    }
    log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS, "Configuration failed after 3 attempts");
    return false;
}

UnicoreResult_t
UnicoreUM980::configureOnceTime() {
    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Configuring UM980 with current settings...");

    UnicoreResult_t result = disableAllOutput();

    result = firstError(result, setElevation(15));
    if (_version.modelType == 18 || _version.modelType == 26) {
        if (queryConfigContains("CONFIG SIGNALGROUP 2") != Unicore_RESULT_CONFIG_PRESENT) {
            result = firstError(result, sendCommandAndWait("CONFIG SIGNALGROUP 2", 1500));
        }
    } else if (_version.modelType == 17) {
        if (queryConfigContains("CONFIG SIGNALGROUP 4 5") != Unicore_RESULT_CONFIG_PRESENT) {
            result = firstError(result, sendCommandAndWait("CONFIG SIGNALGROUP 4 5", 1500));
        }
    }

    // first disable all output to ensure a known state, then re-enable the desired messages with their current periods
    return result;
}

UnicoreResult_t
UnicoreUM980::configureGNSS(const UnicorePort port) {
    UnicoreResult_t result = requestVersion();
    result = firstError(result, setConstellations());
    result = firstError(result, enableBinaryNavigation(port, static_cast<float>(_rateSeconds)));
    return result;
}

UnicoreResult_t
UnicoreUM980::setModeBaseAverage(uint16_t averageSeconds) {
    char command[50];
    snprintf(command, sizeof(command), "TIME %d", averageSeconds);

    return (setBaseMode(command));
}

bool
UnicoreUM980::setBaseModeECEF(double coordinateX, double coordinateY, double coordinateZ) {
    char command[50];
    snprintf(command, sizeof(command), "%0.4f %0.4f %0.4f", coordinateX, coordinateY, coordinateZ);

    return (setBaseMode(command) == Unicore_RESULT_RESPONSE_COMMAND_OK);
}

bool
UnicoreUM980::setBaseModeGeodetic(double latitude, double longitude, double altitude) {
    char command[50];
    snprintf(command, sizeof(command), "%0.11f %0.11f %0.6f", latitude, longitude, altitude);

    return (setBaseMode(command) == Unicore_RESULT_RESPONSE_COMMAND_OK);
}

bool
UnicoreUM980::configureRover() {
    if (!_online) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_CHILD_CLASS, "Cannot configure Rover mode while GNSS is offline");
        return false;
    }
    uint8_t currentModel = requestModel();
    uint8_t needChange = 0;
    if (currentModel != 0) {
        //  0 - Unknown, 1 - Rover Survey, 2 - Rover UAV, 3 - Rover Auto, 4 - Base Survey-in, 5 - Base fixed
        if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_SURVEY && currentModel == 1) {
            needChange = 0;
        }
        if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_UAV && currentModel == 2) {
            needChange = 0;
        }
        if (settings.dynamicModel == UM980_DYN_MODEL_ROVER_AUTOMOTIVE && currentModel == 3) {
            needChange = 0;
        }
        if (currentModel == 4 || currentModel == 5) {
            // We are in a Base mode, need to change to Rover
            needChange = 1;
            settings.dynamicModel = UM980_DYN_MODEL_ROVER_SURVEY;
        }
        if (needChange) {
            // Assume we are changing from Base to Rover, request any additional config changes
            // Sets the dynamic model (Survey/UAV/Automotive) and puts the device into Rover mode
            log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Changing GNSS model from Base to Rover...");
            gnssConfigure(GNSS_CONFIG_MODEL, __FILE__, __LINE__);

            // Request a change to Rover RTCM
            gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER, __FILE__, __LINE__);
        } else {
            // No change needed, but we may want to update the message rates just in case
            log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "GNSS model no change needed. Current: %d   ",
                currentModel);
        }
    }
    return true;
}

bool
UnicoreUM980::configureBase() {
    static bool firstTime = true;
    requestModel();
    if (firstTime) {
        log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS,
            "First time Base configuration. Current GNSS model: %d. Skipping mode checks to allow initial "
            "configuration to complete.",
            _model);
        firstTime = false;
    } else {
        // Skip these checks first time around. We need the setModel
        // If we are already in the appropriate base mode, no changes needed
        if (settings.fixedBase == false && gnssInBaseSurveyInMode()) {
            return (true);
        }
        if (settings.fixedBase == true && gnssInBaseFixedMode()) {
            return (true);
        }
    }
    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Changing GNSS model to Base...");
    // Assume we are changing from Rover to Base, request any additional config
    // changes
    // settings.dynamicModel = settings.fixedBase ? UM980_DYN_MODEL_BASE_FIXED : UM980_DYN_MODEL_BASE_SURVEY;
    // Set the dynamic mode. This will cancel any base averaging mode and is
    // needed to allow a freshly started device to settle in regular GNSS
    // reception mode before issuing a surveyInStart().
    // gnss->setModel(settings.dynamicModel) sets the model
    // setModel(settings.dynamicModel);
    gnssConfigure(GNSS_CONFIG_MODEL, __FILE__, __LINE__);

    // Request a change to Base RTCM. gnss->setMessagesRTCMBase() sets the
    // messages
    gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE, __FILE__, __LINE__);

    return (true);
}

bool
UnicoreUM980::surveyInStart() {
    if (_online) {
        if (gnssInBaseSurveyInMode()) {
            return (true);
        }
        // Set base averaging to the specified seconds to start the survey-in process. The receiver will automatically determine when it has enough data for a position solution and switch from "Survey" to "Fixed" mode at that time.
        UnicoreResult_t result = setModeBaseAverage(settings.observationSeconds);

        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS,
                "Failed to start survey-in with average time of %d seconds. Result: %d", settings.observationSeconds,
                result);
            return false;
        }

        log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS,
            "Survey-in started with average time of %d seconds. Waiting for receiver to determine when it has enough "
            "data to complete the survey-in process...",
            settings.observationSeconds);

        _autoBaseStartTimer = millis();
        return true;
    }
    return false;
}

bool
UnicoreUM980::surveyInReset() {
    bool result = false;
    if (_online) {
        result = setModeRoverSurvey();
    }
    return (result);
}

bool
UnicoreUM980::fixedBaseStart() {
    if (_online == false) {
        return (false);
    }

    // If we are already in the appropriate base mode, no changes needed
    if (gnssInBaseFixedMode()) {
        return (true);
    }

    bool result = true;

    if (settings.fixedBaseCoordinateType == COORD_TYPE_ECEF) {
        result &= setBaseModeECEF(settings.fixedEcefX, settings.fixedEcefY, settings.fixedEcefZ);
    } else if (settings.fixedBaseCoordinateType == COORD_TYPE_GEODETIC) {
        float totalFixedAltitude =
            settings.fixedAltitude + ((settings.antennaHeight_mm + settings.antennaPhaseCenter_mm) / 1000.0);
        result &= setBaseModeGeodetic(settings.fixedLat, settings.fixedLong, totalFixedAltitude);
    }

    return result;
}

UnicoreResult_t
UnicoreUM980::requestVersion(const uint32_t timeoutMs) {
    return requestMessage(MSG_VERSION, timeoutMs);
}

UnicoreResult_t
UnicoreUM980::enableBinaryNavigation(const UnicorePort port, const float periodSeconds) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;

    result = firstError(result, logMessage(MSG_RECTIMEB, port, UnicoreLogTrigger::OnTime, periodSeconds));
    if (startBinaryBeforeFix || (nmeaPositionStatus > 0)) {
        result = firstError(result, logMessage(MSG_BESTNAVB, port, UnicoreLogTrigger::OnTime, periodSeconds));
        result = firstError(result, logMessage(MSG_BESTNAVXYZB, port, UnicoreLogTrigger::OnTime, periodSeconds));
    }

    return result;
}

UnicoreResult_t
UnicoreUM980::disableBinaryNavigation(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;

    result = firstError(result, unlogMessage(MSG_RECTIMEB, port));
    result = firstError(result, unlogMessage(MSG_BESTNAVB, port));
    result = firstError(result, unlogMessage(MSG_BESTNAVXYZB, port));

    return result;
}

UnicoreResult_t
UnicoreUM980::disableAllOutput() {
    UnicoreResult_t firstFailure = Unicore_RESULT_RESPONSE_COMMAND_OK;
    bool com1Done = false;
    bool com2Done = false;
    bool com3Done = false;

    for (int attempt = 0; attempt < 3 && (!com1Done || !com2Done || !com3Done); attempt++) {
        if (!com1Done) {
            const UnicoreResult_t result = unlogPort(UnicorePort::Com1);
            com1Done = (result == Unicore_RESULT_RESPONSE_COMMAND_OK);
            if (!com1Done && (firstFailure == Unicore_RESULT_RESPONSE_COMMAND_OK)) {
                firstFailure = result;
            }
        }
        if (!com2Done) {
            const UnicoreResult_t result = unlogPort(UnicorePort::Com2);
            com2Done = (result == Unicore_RESULT_RESPONSE_COMMAND_OK);
            if (!com2Done && (firstFailure == Unicore_RESULT_RESPONSE_COMMAND_OK)) {
                firstFailure = result;
            }
        }
        if (!com3Done) {
            const UnicoreResult_t result = unlogPort(UnicorePort::Com3);
            com3Done = (result == Unicore_RESULT_RESPONSE_COMMAND_OK);
            if (!com3Done && (firstFailure == Unicore_RESULT_RESPONSE_COMMAND_OK)) {
                firstFailure = result;
            }
        }
    }

    return (com1Done && com2Done && com3Done) ? Unicore_RESULT_RESPONSE_COMMAND_OK : firstFailure;
}

bool
UnicoreUM980::setMessagesNMEA() {
    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Setting NMEA messages on COM ports...");

    UnicoreResult_t result = disableAllOutput(); // Disable all NMEA and RTCM output on all ports...

    um980MessagesEnabled_NMEA.enabled = false;

    if (um980MessagesEnabled_RTCM_Rover || um980MessagesEnabled_RTCM_Base) {
        um980MessagesEnabled_RTCM_Rover = false;
        um980MessagesEnabled_RTCM_Base = false;
        // Request reconfigure of RTCM
        if (inBaseMode()) { // If the current system state is Base
            gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE, __FILE__, __LINE__);
        } else {
            gnssConfigure(GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER, __FILE__, __LINE__);
        }
    }

    result = enableNmeaMessages(); // Enable all NMEA messages on the current port

    if (result == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        um980MessagesEnabled_NMEA.enabled = true;
        um980MessagesEnabled_NMEA.millis = millis();
        return true;
    }
    return false;
}

UnicoreResult_t
UnicoreUM980::enableNmeaMessages(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (int messageNumber = 0; messageNumber < MAX_UM980_NMEA_MSG; messageNumber++) {
        if (_nmeaPeriods[messageNumber] > 0.0f) {
            // Enable the message
            result = firstError(result,
                                setPortMessage(&kUm980NmeaMessages[messageNumber], _nmeaPeriods[messageNumber], port));
        }
        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            if (settings.debugGnssConfig) {
                log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS,
                    "setMessagesNMEA failed to set %0.2f for message %s [%d] on port %s.\r\n",
                    _nmeaPeriods[messageNumber], kUm980NmeaMessages[messageNumber].name, messageNumber, portName(port));
            }
            return Unicore_RESULT_RESPONSE_COMMAND_ERROR; // Don't attempt other messages, assume communication is down
        }
    }
    return result;
}

UnicoreResult_t
UnicoreUM980::disableNmeaMessages(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (int messageNumber = 0; messageNumber < MAX_UM980_NMEA_MSG; messageNumber++) {
        if (_nmeaPeriods[messageNumber] == 0.0f) {
            // Enable the message
            result = firstError(result,
                                setPortMessage(&kUm980NmeaMessages[messageNumber], _nmeaPeriods[messageNumber], port));
        }
        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            if (settings.debugGnssConfig) {
                log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS,
                    "setMessagesNMEA failed to set %0.2f for message %s [%d] on port %s.\r\n",
                    _nmeaPeriods[messageNumber], kUm980NmeaMessages[messageNumber].name, messageNumber, portName(port));
            }
            return Unicore_RESULT_RESPONSE_COMMAND_ERROR; // Don't attempt other messages, assume communication is down
        }
    }
    return result;
}

bool
UnicoreUM980::setMessagesRTCMRover() {
    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Setting RTCM rover messages on COM ports...");
    UnicoreResult_t result = enableRtcmRoverMessages();

    if (result == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        um980MessagesEnabled_RTCM_Rover = true;
        um980MessagesEnabled_RTCM_Base = false;
        log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "RTCM rover messages enabled successfully.");
        return true;
    }
    log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS, "Failed to enable RTCM rover messages. Result: %d", result);
    return false;
}

bool
UnicoreUM980::setMessagesRTCMBase() {
    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "Setting RTCM base messages on COM ports...");

    UnicoreResult_t result = enableRtcmBaseMessages();

    if (result == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        um980MessagesEnabled_RTCM_Base = true;
        um980MessagesEnabled_RTCM_Rover = false;
        log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "RTCM base messages enabled successfully.");
        return true;
    }
    log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS, "Failed to enable RTCM base messages. Result: %d", result);
    return false;
}

UnicoreResult_t
UnicoreUM980::enableRtcmRoverMessages(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (int messageNumber = 0; messageNumber < MAX_UM980_RTCM_MSG; messageNumber++) {
        if (_rtcmRoverPeriods[messageNumber] > 0.0f) {
            result = firstError(
                result, setPortMessage(&kUm980RtcmMessages[messageNumber], _rtcmRoverPeriods[messageNumber], port));
        }
        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            return result;
        }
    }
    return result;
}

UnicoreResult_t
UnicoreUM980::enableRtcmBaseMessages(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (int messageNumber = 0; messageNumber < MAX_UM980_RTCM_MSG; messageNumber++) {
        if (_rtcmBasePeriods[messageNumber] > 0.0f) {
            result = firstError(
                result, setPortMessage(&kUm980RtcmMessages[messageNumber], _rtcmBasePeriods[messageNumber], port));
        }
        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            return result;
        }
    }
    return result;
}

UnicoreResult_t
UnicoreUM980::disableRtcmMessages(const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (int messageNumber = 0; messageNumber < MAX_UM980_RTCM_MSG; messageNumber++) {
        result = firstError(result, unlogMessage(kUm980RtcmMessages[messageNumber].name, port));
        if (result != Unicore_RESULT_RESPONSE_COMMAND_OK) {
            return result;
        }
    }
    return result;
}

UnicoreResult_t
UnicoreUM980::setMode(const char* modeCommand) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!modeCommand) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif // UNICORE_NULLPTR_CHECK
    char command[64] = {};
    snprintf(command, sizeof(command), "MODE %s", modeCommand);

    return sendCommandAndWait(command, 1500);
}

UnicoreResult_t
UnicoreUM980::setRoverMode(const char* roverType) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!roverType) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif // UNICORE_NULLPTR_CHECK
    char command[50];
    snprintf(command, sizeof(command), "ROVER %s", roverType);
    return setMode(command);
}

UnicoreResult_t
UnicoreUM980::setBaseMode(const char* baseType) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!baseType) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif // UNICORE_NULLPTR_CHECK
    char command[50];
    snprintf(command, sizeof(command), "BASE %s", baseType);
    return setMode(command);
}

UnicoreResult_t
UnicoreUM980::setRate(const double secondsBetweenSolutions) {
    if (!std::isfinite(secondsBetweenSolutions) || (secondsBetweenSolutions <= 0.0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    _rateSeconds = secondsBetweenSolutions;
    return Unicore_RESULT_RESPONSE_COMMAND_OK;
}

bool
UnicoreUM980::setModel(const uint8_t modelNumber) {
    if (_online) {
        if (modelNumber == UM980_DYN_MODEL_ROVER_SURVEY) {
            return setRoverMode("SURVEY") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else if (modelNumber == UM980_DYN_MODEL_ROVER_UAV) {
            return setRoverMode("UAV") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else if (modelNumber == UM980_DYN_MODEL_ROVER_AUTOMOTIVE) {
            return setRoverMode("AUTOMOTIVE") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else {
            log(UnicoreLogLevel::Error, UNICORE_LOG_CHILD_CLASS,
                "Unsupported UM980 model number: %u, Use SURVEY default", modelNumber);
            return setRoverMode("SURVEY") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        }
    }
    return false;
}

bool
UnicoreUM980::setModeRoverSurvey() {
    return setRoverMode("SURVEY") == Unicore_RESULT_RESPONSE_COMMAND_OK;
}

bool
UnicoreUM980::setModeRoverUAV() {
    return setRoverMode("UAV") == Unicore_RESULT_RESPONSE_COMMAND_OK;
}

bool
UnicoreUM980::setModeRoverAutomotive() {
    return setRoverMode("AUTOMOTIVE") == Unicore_RESULT_RESPONSE_COMMAND_OK;
}

uint8_t
UnicoreUM980::requestModel() {
    if (sendCommandAndWait("MODE", 2000, "#MODE") == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        return _model;
    }
    return 0;
}

UnicoreResult_t
UnicoreUM980::setElevation(const uint8_t elevationDegrees) {
    char command[32];
    snprintf(command, sizeof(command), "%d", elevationDegrees);
    return disableSystem(command);
}

UnicoreResult_t
UnicoreUM980::setMinCno(const uint8_t cnoValue) {
    char command[32] = {};
    snprintf(command, sizeof(command), "CONFIG MINCNO %u", cnoValue);
    return sendCommandAndWait(command, 1000);
}

UnicoreResult_t
UnicoreUM980::setMultipathMitigation(const bool enable) {
    char command[48] = {};
    snprintf(command, sizeof(command), "CONFIG MULTIPATHMITIGATION %s", enable ? "ENABLE" : "DISABLE");
    return sendCommandAndWait(command, 1000);
}

UnicoreResult_t
UnicoreUM980::setConstellations() {
    char command[96] = {};
    snprintf(command, sizeof(command), "CONFIG SIGNALGROUP");

    bool anyEnabled = false;
    for (size_t index = 0; index < MAX_UM980_CONSTELLATIONS; index++) {
        if (!_constellationEnabled[index]) {
            continue;
        }

        strncat(command, " ", sizeof(command) - strlen(command) - 1);
        strncat(command, kUm980ConstellationCommands[index].commandName, sizeof(command) - strlen(command) - 1);
        anyEnabled = true;
    }

    if (!anyEnabled) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    return sendCommandAndWait(command, 1500);
}

UnicoreResult_t
UnicoreUM980::setConstellationEnabled(const char* commandName, const bool enabled) {
    if (!commandName) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    for (size_t index = 0; index < MAX_UM980_CONSTELLATIONS; index++) {
        if (strcasecmp(kUm980ConstellationCommands[index].commandName, commandName) == 0) {
            _constellationEnabled[index] = enabled;
            return Unicore_RESULT_RESPONSE_COMMAND_OK;
        }
    }

    return Unicore_RESULT_WRONG_COMMAND;
}

bool
UnicoreUM980::setNmeaMessagePeriod(const char* msgName, const float periodSeconds) {
    return setMessagePeriod(kUm980NmeaMessages, _nmeaPeriods, MAX_UM980_NMEA_MSG, msgName, periodSeconds);
}

bool
UnicoreUM980::setRtcmRoverMessagePeriod(const char* msgName, const float periodSeconds) {
    return setMessagePeriod(kUm980RtcmMessages, _rtcmRoverPeriods, MAX_UM980_RTCM_MSG, msgName, periodSeconds);
}

bool
UnicoreUM980::setRtcmBaseMessagePeriod(const char* msgName, const float periodSeconds) {
    return setMessagePeriod(kUm980RtcmMessages, _rtcmBasePeriods, MAX_UM980_RTCM_MSG, msgName, periodSeconds);
}

float
UnicoreUM980::getNmeaMessagePeriod(const char* msgName) const {
    return getMessagePeriod(kUm980NmeaMessages, _nmeaPeriods, MAX_UM980_NMEA_MSG, msgName);
}

float
UnicoreUM980::getRtcmRoverMessagePeriod(const char* msgName) const {
    return getMessagePeriod(kUm980RtcmMessages, _rtcmRoverPeriods, MAX_UM980_RTCM_MSG, msgName);
}

float
UnicoreUM980::getRtcmBaseMessagePeriod(const char* msgName) const {
    return getMessagePeriod(kUm980RtcmMessages, _rtcmBasePeriods, MAX_UM980_RTCM_MSG, msgName);
}

uint8_t
UnicoreUM980::getActiveNmeaMessageCount() const {
    uint8_t count = 0;
    for (const float period : _nmeaPeriods) {
        if (period > 0.0f) {
            count++;
        }
    }
    return count;
}

uint8_t
UnicoreUM980::getActiveRtcmRoverMessageCount() const {
    uint8_t count = 0;
    for (const float period : _rtcmRoverPeriods) {
        if (period > 0.0f) {
            count++;
        }
    }
    return count;
}

uint8_t
UnicoreUM980::getActiveRtcmBaseMessageCount() const {
    uint8_t count = 0;
    for (const float period : _rtcmBasePeriods) {
        if (period > 0.0f) {
            count++;
        }
    }
    return count;
}

int16_t
UnicoreUM980::getNmeaMessageNumberByName(const char* msgName) const {
    return findMessageIndex(kUm980NmeaMessages, MAX_UM980_NMEA_MSG, msgName);
}

int16_t
UnicoreUM980::getRtcmMessageNumberByName(const char* msgName) const {
    return findMessageIndex(kUm980RtcmMessages, MAX_UM980_RTCM_MSG, msgName);
}

bool
UnicoreUM980::isGgaActive() const {
    return getNmeaMessagePeriod("GPGGA") > 0.0f;
}

double
UnicoreUM980::getLatitude() const {
    if (_bestNav) {
        return _bestNav->latitude;
    } else {
        return 0.0f;
    }
}

double
UnicoreUM980::getLongitude() const {
    if (_bestNav) {
        return _bestNav->longitude;
    } else {
        return 0.0f;
    }
}

double
UnicoreUM980::getAltitude() const {
    if (_bestNav) {
        return _bestNav->altitude;
    } else {
        return 0.0f;
    }
}

double
UnicoreUM980::getHorizontalSpeed() const {
    if (_bestNav) {
        return _bestNav->horizontalSpeed;
    } else {
        return 0.0;
    }
}

double
UnicoreUM980::getTrackGround() const {
    if (_bestNav) {
        return _bestNav->trackGround;
    } else {
        return 0.0;
    }
}

float
UnicoreUM980::getLatitudeDeviation() const {
    if (_bestNav) {
        return _bestNav->latitudeDeviation;
    } else {
        return 0.0f;
    }
}

float
UnicoreUM980::getLongitudeDeviation() const {
    if (_bestNav) {
        return _bestNav->longitudeDeviation;
    } else {
        return 0.0f;
    }
}

float
UnicoreUM980::getHorizontalAccuracy() const {
    if (_online) {
        float latitudeDeviation = getLatitudeDeviation();
        float longitudeDeviation = getLongitudeDeviation();

        // The binary message may contain all 0xFFs leading to a very large negative
        // number.
        if (longitudeDeviation < -0.01) {
            longitudeDeviation = 50.0;
        }
        if (latitudeDeviation < -0.01) {
            latitudeDeviation = 50.0;
        }

        // Return the lower of the two Lat/Long deviations
        if (longitudeDeviation < latitudeDeviation) {
            return (longitudeDeviation);
        }
        return (latitudeDeviation);
    }
    return 0;
}

float
UnicoreUM980::getSurveyInMeanAccuracy() const {
    return 0.0f;
}

uint32_t
UnicoreUM980::getSurveyInObservationTimeSeconds() const {
    uint32_t elapsedSeconds = (millis() - _autoBaseStartTimer) / 1000;
    return (elapsedSeconds);
}

bool
UnicoreUM980::isSurveyInComplete() const {
    if (getSurveyInObservationTimeSeconds() > settings.observationSeconds) {
        return (true);
    }
    return (false);
}

uint8_t
UnicoreUM980::getFixType() const {
    if (_bestNav) {
        return _bestNav->positionType;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getCarrierSolution() const {
    if (_bestNav) {
        return _bestNav->rtkSolution;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getSatellitesInView() const {
    if (_bestNav) {
        return _bestNav->satellitesTracked;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getSatellitesUsed() const {
    if (_bestNav) {
        return _bestNav->satellitesUsed;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getDay() const {
    if (_recTime) {
        return _recTime->day;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getMonth() const {
    if (_recTime) {
        return _recTime->month;
    } else {
        return 0;
    }
}

uint16_t
UnicoreUM980::getYear() const {
    if (_recTime) {
        return _recTime->year;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getHour() const {
    if (_recTime) {
        return _recTime->hour;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getMinute() const {
    if (_recTime) {
        return _recTime->minute;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getSecond() const {
    if (_recTime) {
        return _recTime->second;
    } else {
        return 0;
    }
}

uint16_t
UnicoreUM980::getMillisecond() const {
    if (_recTime) {
        return _recTime->millisecond;
    } else {
        return 0;
    }
}

uint8_t
UnicoreUM980::getLeapSeconds() const {
    return getLastBinaryHeader().leapSeconds;
}

double
UnicoreUM980::getEcefX() const {
    if (_bestNavXyz) {
        return _bestNavXyz->ecefX;
    } else {
        return 0;
    }
}

double
UnicoreUM980::getEcefY() const {
    if (_bestNavXyz) {
        return _bestNavXyz->ecefY;
    } else {
        return 0;
    }
}

double
UnicoreUM980::getEcefZ() const {
    if (_bestNavXyz) {
        return _bestNavXyz->ecefZ;
    } else {
        return 0;
    }
}

uint16_t
UnicoreUM980::getFixAgeMilliseconds() const {
    const unsigned long age = millis() - _pvtArrivalMillis;
    return (age > UINT16_MAX) ? UINT16_MAX : static_cast<uint16_t>(age);
}

double
UnicoreUM980::getRateS() const {
    return _rateSeconds;
}

Um980Mode
UnicoreUM980::getMode() const {
    return _mode;
}

uint8_t
UnicoreUM980::getDynamicModel() const {
    return _model;
}

const char*
UnicoreUM980::getFirmwareVersion() const {
    return _version.swVersion;
}

const char*
UnicoreUM980::getSerialNumber() const {
    return _version.serialNumber;
}

uint8_t
UnicoreUM980::getModelType() const {
    return _version.modelType;
}

const char*
UnicoreUM980::getModelName() const {
    return _version.modelName;
}

const char*
UnicoreUM980::getId() const {
    return _version.efuseID;
}

bool
UnicoreUM980::inRoverMode() const {
    return _mode == Um980Mode::Rover;
}

bool
UnicoreUM980::isFixed() const {
    return (getFixType() != 0) && (getLastBestNavMs() != 0);
}

bool
UnicoreUM980::isDgpsFixed() const {
    return (getFixType() == kPosTypePsrDiff) || isRTKFix() || isRTKFloat();
}

bool
UnicoreUM980::isRTKFix() const {
    uint8_t _fixType = getFixType();
    return (_fixType == kPosTypeNarrowInt);
}

bool
UnicoreUM980::isRTKFloat() const {
    uint8_t _fixType = getFixType();
    return (_fixType == kPosTypeWideInt) || (_fixType == kPosTypeNarrowFloat);
}

bool
UnicoreUM980::isValidDate() const {
    return _validDate;
}

bool
UnicoreUM980::isValidTime() const {
    return _validTime;
}

bool
UnicoreUM980::isFullyResolved() const {
    return _fullyResolved;
}

bool
UnicoreUM980::gnssInRoverMode() {
    return _model == UM980_DYN_MODEL_ROVER_SURVEY || _model == UM980_DYN_MODEL_ROVER_UAV
           || _model == UM980_DYN_MODEL_ROVER_AUTOMOTIVE;
}

bool
UnicoreUM980::gnssInBaseSurveyInMode() {
    return _model == UM980_DYN_MODEL_BASE_SURVEY;
}

bool
UnicoreUM980::gnssInBaseFixedMode() {
    return _model == UM980_DYN_MODEL_BASE_FIXED;
}

void
UnicoreUM980::setUserNmeaCallback(UserNmeaCallback callback, void* context) {
    if (callback) {
        _userNmeaCallback = callback;
        _userNmeaContext = context;
    } else {
        _userNmeaCallback = nullptr;
        _userNmeaContext = nullptr;
    }
}

void
UnicoreUM980::setUserRtcmCallback(UserRtcmCallback callback, void* context) {
    if (callback) {
        _userRtcmCallback = callback;
        _userRtcmContext = context;
    } else {
        _userRtcmCallback = nullptr;
        _userRtcmContext = nullptr;
    }
}

void
UnicoreUM980::setUserBinaryCallback(UserBinaryCallback callback, void* context) {
    if (callback) {
        _userBinaryCallback = callback;
        _userBinaryContext = context;
    } else {
        _userBinaryCallback = nullptr;
        _userBinaryContext = nullptr;
    }
}

void
UnicoreUM980::setUserHashCallback(UserHashCallback callback, void* context) {
    if (callback) {
        _userHashCallback = callback;
        _userHashContext = context;
    } else {
        _userHashCallback = nullptr;
        _userHashContext = nullptr;
    }
}

void
UnicoreUM980::handleModeSentence(const char* sentence, uint16_t length) {
    (void)length;

    const char* modePayload = strchr(sentence, ';');
    if (!modePayload) {
        return;
    }
    modePayload++;

    if (strncmp(modePayload, "MODE ", 5) != 0) {
        return;
    }
    modePayload += 5;

    char mode[16] = {};
    char model[16] = {};
    if (!copyModeToken(mode, sizeof(mode), modePayload)) {
        return;
    }
    copyModeToken(model, sizeof(model), modePayload);

    if (strcasecmp(mode, "ROVER") == 0) {
        _mode = Um980Mode::Rover;
        if (strcasecmp(model, "UAV") == 0) {
            _model = UM980_DYN_MODEL_ROVER_UAV;
        } else if (strcasecmp(model, "AUTOMOTIVE") == 0) {
            _model = UM980_DYN_MODEL_ROVER_AUTOMOTIVE;
        } else if (strcasecmp(model, "SURVEY") == 0) {
            _model = UM980_DYN_MODEL_ROVER_SURVEY;
        }
    } else if (strcasecmp(mode, "BASE") == 0) {
        _mode = Um980Mode::Base;
        if (strcasecmp(model, "FIXED") == 0) {
            _model = UM980_DYN_MODEL_BASE_FIXED;
        } else if (strcasecmp(model, "TIME") == 0) {
            _model = UM980_DYN_MODEL_BASE_SURVEY;
        }
    } else {
        _mode = Um980Mode::Unknown;
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "[Mode] sentence received. Mode: %s, Model: %s", mode, model);
}

void
UnicoreUM980::handleDevicenameSentence(const char* sentence, uint16_t length) {
    const char* modePayload = strchr(sentence, ',');
    if (!modePayload) {
        return;
    }
    modePayload++;

    char comName[16] = {};

    copyModeToken(comName, sizeof(comName), modePayload);

    setConnectCom(comName);

    log(UnicoreLogLevel::Info, UNICORE_LOG_CHILD_CLASS, "[devicename] sentence received Current Connect  %s", comName);
}

void
UnicoreUM980::NmeaCallback(const char* sentence, uint16_t length, void* userdata) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!sentence || !userdata) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK
    UnicoreUM980* instance = static_cast<UnicoreUM980*>(userdata);
    if (instance) {
        instance->processNmeaSentence(sentence, length);
    }
}

void
UnicoreUM980::HashCallback(const char* sentence, uint16_t length, void* userdata) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!sentence || !userdata) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK

    UnicoreUM980* instance = static_cast<UnicoreUM980*>(userdata);
    if (instance) {
        instance->processHashSentence(sentence, length);
    }
}

void
UnicoreUM980::RtcmCallback(const uint8_t* message, uint16_t length, uint16_t messageNumber, void* userdata) {
#ifdef UNICORE_NULLPTR_CHECK
    if ((!message && (length > 0)) || !userdata) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK

    UnicoreUM980* instance = static_cast<UnicoreUM980*>(userdata);
    if (instance) {
        instance->processRtcmMessage(message, length, messageNumber);
    }
}

void
UnicoreUM980::BinaryCallback(const UnicoreBinaryHeader& header, const uint8_t* payload, uint16_t length,
                             void* userdata) {
#ifdef UNICORE_NULLPTR_CHECK
    if ((!payload && (length > 0)) || !userdata) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK

    UnicoreUM980* instance = static_cast<UnicoreUM980*>(userdata);
    if (instance) {
        instance->processBinaryMessage(header, payload, length);
    }
}

void
UnicoreUM980::processNmeaSentence(const char* sentence, uint16_t length) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!sentence) {
        return;
    }
#endif

    if (sentence && (strncmp(sentence, "$devicename,", 12) == 0)) {
        handleDevicenameSentence(sentence, length);
    }
    // log(UnicoreLogLevel::Debug, UNICORE_LOG_CHILD_CLASS, "NMEA %s sentence received.", msgName);
    if (_userNmeaCallback) {
        _userNmeaCallback(sentence, length, _userNmeaContext);
    }
}

void
UnicoreUM980::processRtcmMessage(const uint8_t* message, uint16_t length, uint16_t messageNumber) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!message && (length > 0)) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK

    log(UnicoreLogLevel::Debug, UNICORE_LOG_CHILD_CLASS, "RTCM%u message received, length=%u", messageNumber, length);

    if (_userRtcmCallback) {
        _userRtcmCallback(message, length, _userRtcmContext);
    }
}

void
UnicoreUM980::processBinaryMessage(const UnicoreBinaryHeader& header, const uint8_t* payload, uint16_t length) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!payload && (length > 0)) {
        return;
    }
#endif //UNICORE_NULLPTR_CHECK

    log(UnicoreLogLevel::Debug, UNICORE_LOG_CHILD_CLASS, "Binary message %u received, length=%u", header.messageId,
        length);

    if (_userBinaryCallback) {
        _userBinaryCallback(header, payload, length, _userBinaryContext);
    }
}

void
UnicoreUM980::processHashSentence(const char* sentence, uint16_t length) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!sentence) {
        return;
    }
#endif // UNICORE_NULLPTR_CHECK

    if (sentence && (strncmp(sentence, "#MODE,", 6) == 0)) {
        handleModeSentence(sentence, length);
    }

    //log(UnicoreLogLevel::Debug, UNICORE_LOG_CHILD_CLASS, "Hash %*.s sentence received.", length, sentence);
    // external callback for users to receive hash sentences directly as they are received by the module, before any internal processing
    if (_userHashCallback) {
        _userHashCallback(sentence, length, _userHashContext);
    }
}

UnicoreResult_t
UnicoreUM980::setPortMessage(const Um980MessageConfig* messages, const float periods, const UnicorePort port) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!messages || !messages->name || (messages->name[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif //UNICORE_NULLPTR_CHECK

    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    if (periods > 0.0f) {
        result = firstError(result, logMessage(messages->name, port, UnicoreLogTrigger::OnTime, periods));
    } else {
        result = firstError(result, unlogMessage(messages->name, port));
    }
    return result;
}

bool
UnicoreUM980::setMessagePeriod(const Um980MessageConfig* messages, float* periods, const size_t count,
                               const char* msgName, const float periodSeconds) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!messages || !periods) {
        return false;
    }
#endif //UNICORE_NULLPTR_CHECK

    const int16_t index = findMessageIndex(messages, count, msgName);
    if (index == UM980_MESSAGE_NOT_FOUND) {
        return false;
    }

    periods[index] = (std::isfinite(periodSeconds) && (periodSeconds > 0.0f)) ? periodSeconds : 0.0f;
    return true;
}

float
UnicoreUM980::getMessagePeriod(const Um980MessageConfig* messages, const float* periods, const size_t count,
                               const char* msgName) const {
#ifdef UNICORE_NULLPTR_CHECK
    if (!messages || !periods) {
        return 0.0f;
    }
#endif //UNICORE_NULLPTR_CHECK

    const int16_t index = findMessageIndex(messages, count, msgName);
    if (index == UM980_MESSAGE_NOT_FOUND) {
        return 0.0f;
    }
    return periods[index];
}

int16_t
UnicoreUM980::findMessageIndex(const Um980MessageConfig* messages, const size_t count, const char* msgName) const {
#ifdef UNICORE_NULLPTR_CHECK
    if (!messages || !msgName) {
        return UM980_MESSAGE_NOT_FOUND;
    }
#endif //UNICORE_NULLPTR_CHECK

    for (size_t index = 0; index < count; index++) {
        if (!messages[index].name) {
            continue;
        }
        if (strcasecmp(messages[index].name, msgName) == 0) {
            return static_cast<int16_t>(index);
        }
    }
    return UM980_MESSAGE_NOT_FOUND;
}

UnicoreResult_t
UnicoreUM980::firstError(const UnicoreResult_t current, const UnicoreResult_t next,
                         const UnicoreResult_t request) const {
    return (current == request) ? next : current;
}
