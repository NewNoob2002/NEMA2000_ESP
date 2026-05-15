#include "Unicore_UM980.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include "Unicore_Struct.h"

namespace {
constexpr uint8_t kPosTypeSingle = 16;
constexpr uint8_t kPosTypePsrDiff = 17;
constexpr uint8_t kPosTypeNarrowFloat = 34;
constexpr uint8_t kPosTypeWideInt = 49;
constexpr uint8_t kPosTypeNarrowInt = 50;

bool
isPositivePeriod(const float periodSeconds) {
    return std::isfinite(periodSeconds) && (periodSeconds > 0.0f);
}

const char*
dynamicModelName(const Um980DynamicModel model) {
    switch (model) {
        case Um980DynamicModel::UM980_DYN_MODEL_SURVEY: return "SURVEY";
        case Um980DynamicModel::UM980_DYN_MODEL_UAV: return "UAV";
        case Um980DynamicModel::UM980_DYN_MODEL_AUTOMOTIVE: return "AUTOMOTIVE";
        default: return "SURVEY";
    }
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
        log(UnicoreLogLevel::Warn, UNICORE_LOG_DEBUG, "Configuration attempt %d failed, retrying...", x + 1);

        //To Do: reset module
    }
    log(UnicoreLogLevel::Error, UNICORE_LOG_DEBUG, "Configuration failed after 3 attempts");
    return false;
}

UnicoreResult_t
UnicoreUM980::configureOnceTime() {
    log(UnicoreLogLevel::Info, UNICORE_LOG_DEBUG, "Configuring UM980 with current settings...");

    UnicoreResult_t result = disableAllOutput();

    result = firstError(result, setElevation(15));
    if (queryConfigContains("CONFIG SIGNALGROUP 2") != Unicore_RESULT_CONFIG_PRESENT) {
        result = firstError(result, sendCommandAndWait("CONFIG SIGNALGROUP 2", 1500));
    }

    if (result == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        result = firstError(result, saveConfiguration());
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

bool
UnicoreUM980::configureRover() {
    int currentMode = getMode();
    // result = firstError(result, configureRoverOutput(port));
    return result;
}

UnicoreResult_t
UnicoreUM980::configureBase(const UnicorePort port) {
    UnicoreResult_t result = setBaseMode();
    result = firstError(result, configureBaseOutput(port));
    _mode = (result == Unicore_RESULT_RESPONSE_COMMAND_OK) ? Um980Mode::Base : _mode;
    return result;
}

UnicoreResult_t
UnicoreUM980::configureRoverOutput(const UnicorePort port) {
    UnicoreResult_t result = disableAllOutput();
    result = firstError(result, enableNmeaMessages(port));
    result = firstError(result, enableBinaryNavigation(port, static_cast<float>(_rateSeconds)));
    return result;
}

UnicoreResult_t
UnicoreUM980::configureBaseOutput(const UnicorePort port) {
    UnicoreResult_t result = disableAllOutput();
    result = firstError(result, enableRtcmBaseMessages(port));
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

UnicoreResult_t
UnicoreUM980::enableNmeaMessages(const UnicorePort port) {
    return applyMessagePeriods(kUm980NmeaMessages, _nmeaPeriods, MAX_UM980_NMEA_MSG, port);
}

UnicoreResult_t
UnicoreUM980::disableNmeaMessages(const UnicorePort port) {
    return unlogMessages(kUm980NmeaMessages, MAX_UM980_NMEA_MSG, port);
}

UnicoreResult_t
UnicoreUM980::enableRtcmRoverMessages(const UnicorePort port) {
    return applyMessagePeriods(kUm980RtcmMessages, _rtcmRoverPeriods, MAX_UM980_RTCM_MSG, port);
}

UnicoreResult_t
UnicoreUM980::enableRtcmBaseMessages(const UnicorePort port) {
    return applyMessagePeriods(kUm980RtcmMessages, _rtcmBasePeriods, MAX_UM980_RTCM_MSG, port);
}

UnicoreResult_t
UnicoreUM980::disableRtcmMessages(const UnicorePort port) {
    return unlogMessages(kUm980RtcmMessages, MAX_UM980_RTCM_MSG, port);
}

UnicoreResult_t
UnicoreUM980::setMode(const char* modeCommand) {
    char command[64] = {};
    snprintf(command, sizeof(command), "MODE %s", modeCommand);

    return sendCommandAndWait(command, 1500);
}

UnicoreResult_t
UnicoreUM980::setRoverMode(const char* roverType) {
    char command[50];
    snprintf(command, sizeof(command), "ROVER %s", roverType);
    return setMode(command);
}

UnicoreResult_t
UnicoreUM980::setBaseMode() {
    const UnicoreResult_t result = sendCommandAndWait("MODE BASE", 1500);
    if (result == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        _mode = Um980Mode::Base;
    }
    return result;
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
        if (modelNumber == UM980_DYN_MODEL_SURVEY) {
            return setRoverMode("SURVEY") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else if (modelNumber == UM980_DYN_MODEL_UAV) {
            return setRoverMode("UAV") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else if (modelNumber == UM980_DYN_MODEL_AUTOMOTIVE) {
            return setRoverMode("AUTOMOTIVE") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        } else {
            log(UnicoreLogLevel::Error, UNICORE_LOG_DEBUG, "Unsupported UM980 model number: %u, Use SURVEY default",
                modelNumber);
            return setRoverMode("SURVEY") == Unicore_RESULT_RESPONSE_COMMAND_OK;
        }
    }
    return false;
}

uint8_t
UnicoreUM980::getModel() {
    if (sendCommandAndWait("MODE", 2000, "#MODE") == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        return strtoul(_lastCommandResponse, nullptr, 10);
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
        if (isPositivePeriod(period)) {
            count++;
        }
    }
    return count;
}

uint8_t
UnicoreUM980::getActiveRtcmRoverMessageCount() const {
    uint8_t count = 0;
    for (const float period : _rtcmRoverPeriods) {
        if (isPositivePeriod(period)) {
            count++;
        }
    }
    return count;
}

uint8_t
UnicoreUM980::getActiveRtcmBaseMessageCount() const {
    uint8_t count = 0;
    for (const float period : _rtcmBasePeriods) {
        if (isPositivePeriod(period)) {
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
    return _bestNav.latitude;
}

double
UnicoreUM980::getLongitude() const {
    return _bestNav.longitude;
}

double
UnicoreUM980::getAltitude() const {
    return _bestNav.altitude;
}

float
UnicoreUM980::getHorizontalAccuracy() const {
    return _horizontalAccuracy;
}

uint8_t
UnicoreUM980::getFixType() const {
    return _bestNav.positionType;
}

uint8_t
UnicoreUM980::getCarrierSolution() const {
    return _bestNav.rtkSolution;
}

uint8_t
UnicoreUM980::getSatellitesInView() const {
    return _bestNav.satellitesTracked;
}

uint8_t
UnicoreUM980::getDay() const {
    return _recTime.day;
}

uint8_t
UnicoreUM980::getMonth() const {
    return _recTime.month;
}

uint16_t
UnicoreUM980::getYear() const {
    return _recTime.year;
}

uint8_t
UnicoreUM980::getHour() const {
    return _recTime.hour;
}

uint8_t
UnicoreUM980::getMinute() const {
    return _recTime.minute;
}

uint8_t
UnicoreUM980::getSecond() const {
    return _recTime.second;
}

uint16_t
UnicoreUM980::getMillisecond() const {
    return _recTime.millisecond;
}

uint8_t
UnicoreUM980::getLeapSeconds() const {
    return getLastBinaryHeader().leapSeconds;
}

double
UnicoreUM980::getEcefX() const {
    return _bestNavXyz.ecefX;
}

double
UnicoreUM980::getEcefY() const {
    return _bestNavXyz.ecefY;
}

double
UnicoreUM980::getEcefZ() const {
    return _bestNavXyz.ecefZ;
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

void
UnicoreUM980::processNmeaSentence(const char* sentence, uint16_t length) {
    if (getLastCommandResult() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
        // capture response to command for potential use in getters like getModel()
        if (strncmp(sentence, "#MODE,", 6) == 0) {
            handleModeSentence(sentence, length);
        }
    }
}

void
UnicoreUM980::handleModeSentence(const char* sentence, uint16_t length) {}

void
UnicoreUM980::NmeaCallback(const char* sentence, uint16_t length, void* userdata) {
    UnicoreUM980* instance = static_cast<UnicoreUM980*>(userdata);
    if (instance) {
        instance->processNmeaSentence(sentence, length);
    }
}

UnicoreResult_t
UnicoreUM980::applyMessagePeriods(const Um980MessageConfig* messages, const float* periods, const size_t count,
                                  const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (size_t index = 0; index < count; index++) {
        if (isPositivePeriod(periods[index])) {
            result =
                firstError(result, logMessage(messages[index].name, port, UnicoreLogTrigger::OnTime, periods[index]),
                           Unicore_RESULT_RESPONSE_COMMAND_OK);
        } else {
            result = firstError(result, unlogMessage(messages[index].name, port), Unicore_RESULT_RESPONSE_COMMAND_OK);
        }
    }
    return result;
}

UnicoreResult_t
UnicoreUM980::unlogMessages(const Um980MessageConfig* messages, const size_t count, const UnicorePort port) {
    UnicoreResult_t result = Unicore_RESULT_RESPONSE_COMMAND_OK;
    for (size_t index = 0; index < count; index++) {
        result = firstError(result, unlogMessage(messages[index].name, port), Unicore_RESULT_RESPONSE_COMMAND_OK);
    }
    return result;
}

bool
UnicoreUM980::setMessagePeriod(const Um980MessageConfig* messages, float* periods, const size_t count,
                               const char* msgName, const float periodSeconds) {
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
    const int16_t index = findMessageIndex(messages, count, msgName);
    if (index == UM980_MESSAGE_NOT_FOUND) {
        return 0.0f;
    }
    return periods[index];
}

int16_t
UnicoreUM980::findMessageIndex(const Um980MessageConfig* messages, const size_t count, const char* msgName) const {
    if (!msgName) {
        return UM980_MESSAGE_NOT_FOUND;
    }

    for (size_t index = 0; index < count; index++) {
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
