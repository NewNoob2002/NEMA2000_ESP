#include "SettingsFile.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <SimpleIni.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Support.h"
#include "esp_log.h"
#include "mcu_settings.h"

namespace {

static const char* const TAG = "SettingsFile";
static const int kSettingsSchemaVersion = 1;
static const size_t kProfileMaxNameLength = 31;

bool settingsDirty = false;

enum class FieldType : uint8_t {
    Bool,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Float,
    Double,
    String,
};

struct SettingsField {
    const char* section;
    const char* key;
    FieldType type;
    size_t offset;
    size_t size;
    double minValue;
    double maxValue;
};

#define SETTINGS_FIELD(section, key, type, member, minValue, maxValue)                                                 \
    { section, key, type, offsetof(settings_t, member), sizeof(((settings_t*)0)->member), minValue, maxValue }

const SettingsField settingsFields[] = {
    SETTINGS_FIELD("Antenna", "antennaHeight_mm", FieldType::Int16, antennaHeight_mm, -32768.0, 32767.0),
    SETTINGS_FIELD("Antenna", "antennaPhaseCenter_mm", FieldType::Float, antennaPhaseCenter_mm, -10000.0, 10000.0),
    SETTINGS_FIELD("Antenna", "ARPLoggingInterval_s", FieldType::UInt16, ARPLoggingInterval_s, 0.0, 65535.0),
    SETTINGS_FIELD("Antenna", "enableARPLogging", FieldType::Bool, enableARPLogging, 0.0, 1.0),

    SETTINGS_FIELD("Base", "baseId", FieldType::String, baseId, 0.0, 0.0),
    SETTINGS_FIELD("Base", "baseCasterOverride", FieldType::Bool, baseCasterOverride, 0.0, 1.0),
    SETTINGS_FIELD("Base", "fixedAltitude", FieldType::Double, fixedAltitude, -100000.0, 100000.0),
    SETTINGS_FIELD("Base", "fixedBase", FieldType::Bool, fixedBase, 0.0, 1.0),
    SETTINGS_FIELD("Base", "fixedBaseCoordinateType", FieldType::Bool, fixedBaseCoordinateType, 0.0, 1.0),
    SETTINGS_FIELD("Base", "fixedEcefX", FieldType::Double, fixedEcefX, -10000000.0, 10000000.0),
    SETTINGS_FIELD("Base", "fixedEcefY", FieldType::Double, fixedEcefY, -10000000.0, 10000000.0),
    SETTINGS_FIELD("Base", "fixedEcefZ", FieldType::Double, fixedEcefZ, -10000000.0, 10000000.0),
    SETTINGS_FIELD("Base", "fixedLat", FieldType::Double, fixedLat, -90.0, 90.0),
    SETTINGS_FIELD("Base", "fixedLong", FieldType::Double, fixedLong, -180.0, 180.0),
    SETTINGS_FIELD("Base", "observationSeconds", FieldType::Int32, observationSeconds, 1.0, 86400.0),
    SETTINGS_FIELD("Base", "observationPositionAccuracy", FieldType::Float, observationPositionAccuracy, 0.0, 100000.0),
    SETTINGS_FIELD("Base", "surveyInStartingAccuracy", FieldType::Float, surveyInStartingAccuracy, 0.0, 100000.0),

    SETTINGS_FIELD("Battery", "enablePrintBatteryMessages", FieldType::Bool, enablePrintBatteryMessages, 0.0, 1.0),
    SETTINGS_FIELD("Battery", "shutdownNoChargeTimeoutMinutes", FieldType::UInt32, shutdownNoChargeTimeoutMinutes, 0.0,
                   4294967295.0),

    SETTINGS_FIELD("Bluetooth", "bluetoothRadioType", FieldType::Int32, bluetoothRadioType, BLUETOOTH_RADIO_SPP,
                   BLUETOOTH_RADIO_OFF),
    SETTINGS_FIELD("Bluetooth", "sppRxQueueSize", FieldType::UInt16, sppRxQueueSize, 1.0, 65535.0),
    SETTINGS_FIELD("Bluetooth", "sppTxQueueSize", FieldType::UInt16, sppTxQueueSize, 1.0, 65535.0),

    SETTINGS_FIELD("Corrections", "correctionsSourcesLifetime_s", FieldType::Int32, correctionsSourcesLifetime_s, 1.0,
                   86400.0),
    SETTINGS_FIELD("Corrections", "debugCorrections", FieldType::Bool, debugCorrections, 0.0, 1.0),
    SETTINGS_FIELD("Corrections", "enableExtCorrRadio", FieldType::UInt8, enableExtCorrRadio, 0.0, 254.0),

#if defined(COMPILE_ETHERNET)
    SETTINGS_FIELD("Ethernet", "enablePrintEthernetDiag", FieldType::Bool, enablePrintEthernetDiag, 0.0, 1.0),
    SETTINGS_FIELD("Ethernet", "ethernetDHCP", FieldType::Bool, ethernetDHCP, 0.0, 1.0),
#endif

    SETTINGS_FIELD("GNSS", "debugGnss", FieldType::Bool, debugGnss, 0.0, 1.0),
    SETTINGS_FIELD("GNSS", "enablePrintPosition", FieldType::Bool, enablePrintPosition, 0.0, 1.0),
    SETTINGS_FIELD("GNSS", "serialGNSSRxFullThreshold", FieldType::UInt16, serialGNSSRxFullThreshold, 1.0, 128.0),
    SETTINGS_FIELD("GNSS", "uartReceiveBufferSize", FieldType::Int32, uartReceiveBufferSize, 256.0, 65535.0),

#if defined(COMPILE_SD_CARD)
    SETTINGS_FIELD("LogFile", "alignedLogFiles", FieldType::Bool, alignedLogFiles, 0.0, 1.0),
    SETTINGS_FIELD("LogFile", "enableLogging", FieldType::Bool, enableLogging, 0.0, 1.0),
    SETTINGS_FIELD("LogFile", "enablePrintLogFileMessages", FieldType::Bool, enablePrintLogFileMessages, 0.0, 1.0),
    SETTINGS_FIELD("LogFile", "enablePrintLogFileStatus", FieldType::Bool, enablePrintLogFileStatus, 0.0, 1.0),
    SETTINGS_FIELD("LogFile", "maxLogLength_minutes", FieldType::Int32, maxLogLength_minutes, 0.0, 1000000.0),
    SETTINGS_FIELD("LogFile", "maxLogTime_minutes", FieldType::Int32, maxLogTime_minutes, 0.0, 1000000.0),
#endif

#if defined(COMPILE_MQTT)
    SETTINGS_FIELD("MQTT", "debugMqttClientData", FieldType::Bool, debugMqttClientData, 0.0, 1.0),
    SETTINGS_FIELD("MQTT", "debugMqttClientState", FieldType::Bool, debugMqttClientState, 0.0, 1.0),
#endif

    SETTINGS_FIELD("Network", "mdnsEnable", FieldType::Bool, mdnsEnable, 0.0, 1.0),
    SETTINGS_FIELD("Network", "mdnsHostName", FieldType::String, mdnsHostName, 0.0, 0.0),
    SETTINGS_FIELD("Network", "debugAppleAccessory", FieldType::Bool, debugAppleAccessory, 0.0, 1.0),
    SETTINGS_FIELD("Network", "debugNetworkLayer", FieldType::Bool, debugNetworkLayer, 0.0, 1.0),
    SETTINGS_FIELD("Network", "printNetworkStatus", FieldType::Bool, printNetworkStatus, 0.0, 1.0),
    SETTINGS_FIELD("Network", "networkClientWriteTimeout_ms", FieldType::UInt32, networkClientWriteTimeout_ms, 1.0,
                   60000.0),

    SETTINGS_FIELD("OS", "btReadTaskCore", FieldType::UInt8, btReadTaskCore, 0.0, 1.0),
    SETTINGS_FIELD("OS", "btReadTaskPriority", FieldType::UInt8, btReadTaskPriority, 0.0, configMAX_PRIORITIES),
    SETTINGS_FIELD("OS", "debugMalloc", FieldType::Bool, debugMalloc, 0.0, 1.0),
    SETTINGS_FIELD("OS", "enableHeapReport", FieldType::Bool, enableHeapReport, 0.0, 1.0),
    SETTINGS_FIELD("OS", "enablePrintIdleTime", FieldType::Bool, enablePrintIdleTime, 0.0, 1.0),
    SETTINGS_FIELD("OS", "enablePsram", FieldType::Bool, enablePsram, 0.0, 1.0),
    SETTINGS_FIELD("OS", "enableTaskReports", FieldType::Bool, enableTaskReports, 0.0, 1.0),
    SETTINGS_FIELD("OS", "gnssReadTaskCore", FieldType::UInt8, gnssReadTaskCore, 0.0, 1.0),
    SETTINGS_FIELD("OS", "gnssReadTaskPriority", FieldType::UInt8, gnssReadTaskPriority, 0.0, configMAX_PRIORITIES),
    SETTINGS_FIELD("OS", "gnssReadTaskStackSize", FieldType::UInt16, gnssReadTaskStackSize, 1024.0, 65535.0),
    SETTINGS_FIELD("OS", "haltOnPanic", FieldType::Bool, haltOnPanic, 0.0, 1.0),
    SETTINGS_FIELD("OS", "measurementScale", FieldType::UInt8, measurementScale, 0.0, MEASUREMENT_UNITS_MAX - 1),
    SETTINGS_FIELD("OS", "printBootTimes", FieldType::Bool, printBootTimes, 0.0, 1.0),
    SETTINGS_FIELD("OS", "printPartitionTable", FieldType::Bool, printPartitionTable, 0.0, 1.0),
    SETTINGS_FIELD("OS", "printTaskStartStop", FieldType::Bool, printTaskStartStop, 0.0, 1.0),
    SETTINGS_FIELD("OS", "psramMallocLevel", FieldType::UInt16, psramMallocLevel, 0.0, 100.0),
    SETTINGS_FIELD("OS", "rebootMinutes", FieldType::UInt32, rebootMinutes, 0.0, 4294967295.0),
    SETTINGS_FIELD("OS", "resetCount", FieldType::Int32, resetCount, INT_MIN, INT_MAX),

    SETTINGS_FIELD("Profile", "profileName", FieldType::String, profileName, 0.0, 0.0),

    SETTINGS_FIELD("Rover", "dynamicModel", FieldType::UInt8, dynamicModel, 1.0, 254.0),
    SETTINGS_FIELD("Rover", "enablePrintRoverAccuracy", FieldType::Bool, enablePrintRoverAccuracy, 0.0, 1.0),
    SETTINGS_FIELD("Rover", "enableMultipathMitigation", FieldType::Bool, enableMultipathMitigation, 0.0, 1.0),
    SETTINGS_FIELD("Rover", "minCN0", FieldType::Int16, minCN0, -32768.0, 32767.0),
    SETTINGS_FIELD("Rover", "minElev", FieldType::UInt8, minElev, 0.0, 90.0),

    SETTINGS_FIELD("RTC", "enablePrintRtcSync", FieldType::Bool, enablePrintRtcSync, 0.0, 1.0),
    SETTINGS_FIELD("RTCM", "debugRtcmBuffers", FieldType::Bool, debugRtcmBuffers, 0.0, 1.0),

#if defined(COMPILE_SD_CARD)
    SETTINGS_FIELD("SDCard", "enablePrintBufferOverrun", FieldType::Bool, enablePrintBufferOverrun, 0.0, 1.0),
    SETTINGS_FIELD("SDCard", "enablePrintSDBuffers", FieldType::Bool, enablePrintSDBuffers, 0.0, 1.0),
    SETTINGS_FIELD("SDCard", "enableSD", FieldType::Bool, enableSD, 0.0, 1.0),
    SETTINGS_FIELD("SDCard", "forceResetOnSDFail", FieldType::Bool, forceResetOnSDFail, 0.0, 1.0),
#endif

    SETTINGS_FIELD("Serial", "dataPortBaud", FieldType::UInt32, dataPortBaud, 1200.0, 10000000.0),
    SETTINGS_FIELD("Serial", "enableGnssToUsbSerial", FieldType::Bool, enableGnssToUsbSerial, 0.0, 1.0),
    SETTINGS_FIELD("Serial", "radioPortBaud", FieldType::UInt32, radioPortBaud, 1200.0, 10000000.0),
    SETTINGS_FIELD("Serial", "serialTimeoutGNSS", FieldType::Int16, serialTimeoutGNSS, 0.0, 1000.0),
    SETTINGS_FIELD("Serial", "enableNmeaOnRadio", FieldType::Bool, enableNmeaOnRadio, 0.0, 1.0),

    SETTINGS_FIELD("Radio", "radioConfigNumber", FieldType::UInt8, radioConfigNumber, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigStatus", FieldType::UInt8, radioConfigStatus, 0.0, 1.0),
    SETTINGS_FIELD("Radio", "radioConfigWorkMode", FieldType::UInt8, radioConfigWorkMode, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigChannel", FieldType::UInt8, radioConfigChannel, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigTxFrequency", FieldType::Float, radioConfigTxFrequency, 0.0, 10000.0),
    SETTINGS_FIELD("Radio", "radioConfigRxFrequency", FieldType::Float, radioConfigRxFrequency, 0.0, 10000.0),
    SETTINGS_FIELD("Radio", "radioConfigPower", FieldType::UInt8, radioConfigPower, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigProtocol", FieldType::UInt8, radioConfigProtocol, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigAirRate", FieldType::UInt8, radioConfigAirRate, 0.0, 255.0),
    SETTINGS_FIELD("Radio", "radioConfigDataFormat", FieldType::UInt8, radioConfigDataFormat, 0.0, 255.0),

    SETTINGS_FIELD("State", "enablePrintDuplicateStates", FieldType::Bool, enablePrintDuplicateStates, 0.0, 1.0),
    SETTINGS_FIELD("State", "enablePrintStates", FieldType::Bool, enablePrintStates, 0.0, 1.0),
    SETTINGS_FIELD("State", "lastState", FieldType::Int32, lastState, 0.0, STATE_NOT_SET),

#if defined(COMPILE_TCP)
    SETTINGS_FIELD("TCPClient", "debugTcpClient", FieldType::Bool, debugTcpClient, 0.0, 1.0),
    SETTINGS_FIELD("TCPClient", "enableTcpClient", FieldType::Bool, enableTcpClient, 0.0, 1.0),
    SETTINGS_FIELD("TCPClient", "tcpClientHost", FieldType::String, tcpClientHost, 0.0, 0.0),
    SETTINGS_FIELD("TCPClient", "tcpClientPort", FieldType::UInt16, tcpClientPort, 1.0, 65535.0),
    SETTINGS_FIELD("TCPServer", "debugTcpServer", FieldType::Bool, debugTcpServer, 0.0, 1.0),
    SETTINGS_FIELD("TCPServer", "enableTcpServer", FieldType::Bool, enableTcpServer, 0.0, 1.0),
    SETTINGS_FIELD("TCPServer", "tcpServerPort", FieldType::UInt16, tcpServerPort, 1.0, 65535.0),
    SETTINGS_FIELD("TCPServer", "tcpOverWiFiStation", FieldType::Bool, tcpOverWiFiStation, 0.0, 1.0),
    SETTINGS_FIELD("TCPServer", "udpOverWiFiStation", FieldType::Bool, udpOverWiFiStation, 0.0, 1.0),
#endif

    SETTINGS_FIELD("TimeZone", "timeZoneHours", FieldType::Int8, timeZoneHours, -23.0, 23.0),
    SETTINGS_FIELD("TimeZone", "timeZoneMinutes", FieldType::Int8, timeZoneMinutes, -59.0, 59.0),
    SETTINGS_FIELD("TimeZone", "timeZoneSeconds", FieldType::Int8, timeZoneSeconds, -59.0, 59.0),

#if defined(COMPILE_UDP)
    SETTINGS_FIELD("UDP", "debugUdpServer", FieldType::Bool, debugUdpServer, 0.0, 1.0),
    SETTINGS_FIELD("UDP", "enableUdpServer", FieldType::Bool, enableUdpServer, 0.0, 1.0),
    SETTINGS_FIELD("UDP", "udpServerPort", FieldType::UInt16, udpServerPort, 1.0, 65535.0),
#endif

    SETTINGS_FIELD("UM980", "enableImuCompensationDebug", FieldType::Bool, enableImuCompensationDebug, 0.0, 1.0),
    SETTINGS_FIELD("UM980", "enableImuDebug", FieldType::Bool, enableImuDebug, 0.0, 1.0),
    SETTINGS_FIELD("UM980", "enableTiltCompensation", FieldType::Bool, enableTiltCompensation, 0.0, 1.0),

    SETTINGS_FIELD("Logging", "enableLoggingRINEX", FieldType::Bool, enableLoggingRINEX, 0.0, 1.0),
    SETTINGS_FIELD("Logging", "externalEventPolarity", FieldType::Bool, externalEventPolarity, 0.0, 1.0),

    SETTINGS_FIELD("WebServer", "httpPort", FieldType::UInt16, httpPort, 1.0, 65535.0),

#if defined(COMPILE_WIFI)
    SETTINGS_FIELD("WiFi", "debugWebServer", FieldType::Bool, debugWebServer, 0.0, 1.0),
    SETTINGS_FIELD("WiFi", "debugWifiState", FieldType::Bool, debugWifiState, 0.0, 1.0),
    SETTINGS_FIELD("WiFi", "wifiChannel", FieldType::UInt8, wifiChannel, 1.0, 14.0),
    SETTINGS_FIELD("WiFi", "wifiConfigOverAP", FieldType::Bool, wifiConfigOverAP, 0.0, 1.0),
    SETTINGS_FIELD("WiFi", "wifiConnectTimeoutMs", FieldType::UInt32, wifiConnectTimeoutMs, 100.0, 120000.0),
#endif

    SETTINGS_FIELD("Debug", "debugSettings", FieldType::Bool, debugSettings, 0.0, 1.0),
    SETTINGS_FIELD("Debug", "debugGnssConfig", FieldType::Bool, debugGnssConfig, 0.0, 1.0),

    SETTINGS_FIELD("PPP", "pppMode", FieldType::Int32, pppMode, 0.0, 255.0),
    SETTINGS_FIELD("PPP", "pppDatum", FieldType::Int32, pppDatum, 0.0, 255.0),
    SETTINGS_FIELD("PPP", "pppTimeout", FieldType::Int32, pppTimeout, 0.0, 86400.0),
    SETTINGS_FIELD("PPP", "pppHorizontalConvergence", FieldType::Float, pppHorizontalConvergence, 0.0, 1000.0),
    SETTINGS_FIELD("PPP", "pppVerticalConvergence", FieldType::Float, pppVerticalConvergence, 0.0, 1000.0),
};

#undef SETTINGS_FIELD

void
setError(char* error, const size_t errorLength, const char* message) {
    if ((error != nullptr) && (errorLength > 0)) {
        snprintf(error, errorLength, "%s", message ? message : "");
    }
}

void*
fieldPointer(settings_t* target, const SettingsField& field) {
    return reinterpret_cast<uint8_t*>(target) + field.offset;
}

const void*
fieldPointer(const settings_t* source, const SettingsField& field) {
    return reinterpret_cast<const uint8_t*>(source) + field.offset;
}

bool
parseSigned(const char* text, int64_t* value) {
    if ((text == nullptr) || (value == nullptr)) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = strtoll(text, &end, 0);
    if ((errno != 0) || (end == text)) {
        return false;
    }
    while ((end != nullptr) && (*end != 0)) {
        if (!isspace(static_cast<unsigned char>(*end))) {
            return false;
        }
        end++;
    }
    *value = parsed;
    return true;
}

bool
parseUnsigned(const char* text, uint64_t* value) {
    if ((text == nullptr) || (value == nullptr) || (text[0] == '-')) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = strtoull(text, &end, 0);
    if ((errno != 0) || (end == text)) {
        return false;
    }
    while ((end != nullptr) && (*end != 0)) {
        if (!isspace(static_cast<unsigned char>(*end))) {
            return false;
        }
        end++;
    }
    *value = parsed;
    return true;
}

bool
parseDoubleValue(const char* text, double* value) {
    if ((text == nullptr) || (value == nullptr)) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = strtod(text, &end);
    if ((errno != 0) || (end == text) || !isfinite(parsed)) {
        return false;
    }
    while ((end != nullptr) && (*end != 0)) {
        if (!isspace(static_cast<unsigned char>(*end))) {
            return false;
        }
        end++;
    }
    *value = parsed;
    return true;
}

bool
parseBoolValue(const char* text, bool* value) {
    if ((text == nullptr) || (value == nullptr)) {
        return false;
    }
    if ((strcasecmp(text, "true") == 0) || (strcasecmp(text, "yes") == 0) || (strcmp(text, "1") == 0)
        || (strcasecmp(text, "on") == 0)) {
        *value = true;
        return true;
    }
    if ((strcasecmp(text, "false") == 0) || (strcasecmp(text, "no") == 0) || (strcmp(text, "0") == 0)
        || (strcasecmp(text, "off") == 0)) {
        *value = false;
        return true;
    }
    return false;
}

bool
writeSignedValue(void* target, const size_t size, const int64_t value) {
    switch (size) {
        case sizeof(int8_t): {
            const int8_t typedValue = static_cast<int8_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        case sizeof(int16_t): {
            const int16_t typedValue = static_cast<int16_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        case sizeof(int32_t): {
            const int32_t typedValue = static_cast<int32_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        default: return false;
    }
}

bool
writeUnsignedValue(void* target, const size_t size, const uint64_t value) {
    switch (size) {
        case sizeof(uint8_t): {
            const uint8_t typedValue = static_cast<uint8_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        case sizeof(uint16_t): {
            const uint16_t typedValue = static_cast<uint16_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        case sizeof(uint32_t): {
            const uint32_t typedValue = static_cast<uint32_t>(value);
            memcpy(target, &typedValue, sizeof(typedValue));
            return true;
        }
        default: return false;
    }
}

int64_t
readSignedValue(const void* source, const size_t size) {
    switch (size) {
        case sizeof(int8_t): {
            int8_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        case sizeof(int16_t): {
            int16_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        case sizeof(int32_t): {
            int32_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        default: return 0;
    }
}

uint64_t
readUnsignedValue(const void* source, const size_t size) {
    switch (size) {
        case sizeof(uint8_t): {
            uint8_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        case sizeof(uint16_t): {
            uint16_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        case sizeof(uint32_t): {
            uint32_t value = 0;
            memcpy(&value, source, sizeof(value));
            return value;
        }
        default: return 0;
    }
}

bool
loadField(const CSimpleIniA& ini, const SettingsField& field, settings_t* target, char* error, size_t errorLength) {
    const char* value = ini.GetValue(field.section, field.key, nullptr);
    if (value == nullptr) {
        return true;
    }

    void* targetValue = fieldPointer(target, field);
    switch (field.type) {
        case FieldType::Bool: {
            bool parsed = false;
            if (!parseBoolValue(value, &parsed)) {
                setError(error, errorLength, "Invalid boolean value");
                return false;
            }
            *static_cast<bool*>(targetValue) = parsed;
            return true;
        }
        case FieldType::Int8:
        case FieldType::Int16:
        case FieldType::Int32: {
            int64_t parsed = 0;
            if (!parseSigned(value, &parsed) || (parsed < field.minValue) || (parsed > field.maxValue)) {
                setError(error, errorLength, "Invalid signed integer value");
                return false;
            }
            if (!writeSignedValue(targetValue, field.size, parsed)) {
                setError(error, errorLength, "Unsupported signed integer size");
                return false;
            }
            return true;
        }
        case FieldType::UInt8:
        case FieldType::UInt16:
        case FieldType::UInt32: {
            uint64_t parsed = 0;
            if (!parseUnsigned(value, &parsed) || (parsed < field.minValue) || (parsed > field.maxValue)) {
                setError(error, errorLength, "Invalid unsigned integer value");
                return false;
            }
            if (!writeUnsignedValue(targetValue, field.size, parsed)) {
                setError(error, errorLength, "Unsupported unsigned integer size");
                return false;
            }
            return true;
        }
        case FieldType::Float:
        case FieldType::Double: {
            double parsed = 0.0;
            if (!parseDoubleValue(value, &parsed) || (parsed < field.minValue) || (parsed > field.maxValue)) {
                setError(error, errorLength, "Invalid floating point value");
                return false;
            }
            if (field.type == FieldType::Float) {
                *static_cast<float*>(targetValue) = static_cast<float>(parsed);
            } else {
                *static_cast<double*>(targetValue) = parsed;
            }
            return true;
        }
        case FieldType::String: {
            char* stringTarget = static_cast<char*>(targetValue);
            snprintf(stringTarget, field.size, "%s", value);
            return true;
        }
        default: break;
    }

    setError(error, errorLength, "Unsupported field type");
    return false;
}

void
saveField(CSimpleIniA& ini, const SettingsField& field, const settings_t* source) {
    const void* sourceValue = fieldPointer(source, field);
    char value[80] = {};

    switch (field.type) {
        case FieldType::Bool:
            snprintf(value, sizeof(value), "%s", *static_cast<const bool*>(sourceValue) ? "true" : "false");
            break;
        case FieldType::Int8:
        case FieldType::Int16:
        case FieldType::Int32:
            snprintf(value, sizeof(value), "%lld", static_cast<long long>(readSignedValue(sourceValue, field.size)));
            break;
        case FieldType::UInt8:
        case FieldType::UInt16:
        case FieldType::UInt32:
            snprintf(value, sizeof(value), "%llu",
                     static_cast<unsigned long long>(readUnsignedValue(sourceValue, field.size)));
            break;
        case FieldType::Float:
            snprintf(value, sizeof(value), "%.7g", static_cast<double>(*static_cast<const float*>(sourceValue)));
            break;
        case FieldType::Double:
            snprintf(value, sizeof(value), "%.15g", *static_cast<const double*>(sourceValue));
            break;
        case FieldType::String: ini.SetValue(field.section, field.key, static_cast<const char*>(sourceValue)); return;
        default: return;
    }

    ini.SetValue(field.section, field.key, value);
}

#if defined(COMPILE_WIFI)
bool
loadWifiNetwork(const CSimpleIniA& ini, settings_t* target, char* error, size_t errorLength) {
    char key[32] = {};

    for (int index = 0; index < MAX_WIFI_NETWORKS; index++) {
        snprintf(key, sizeof(key), "network%dSsid", index);
        const char* ssid = ini.GetValue("WiFi", key, nullptr);
        if (ssid != nullptr) {
            if (strlen(ssid) >= WIFI_SSID_LENGTH) {
                setError(error, errorLength, "WiFi SSID is too long");
                return false;
            }
            snprintf(target->wifiNetworks[index].ssid, sizeof(target->wifiNetworks[index].ssid), "%s", ssid);
        }

        snprintf(key, sizeof(key), "network%dPassword", index);
        const char* password = ini.GetValue("WiFi", key, nullptr);
        if (password != nullptr) {
            if (strlen(password) >= WIFI_PASSWORD_LENGTH) {
                setError(error, errorLength, "WiFi password is too long");
                return false;
            }
            snprintf(target->wifiNetworks[index].password, sizeof(target->wifiNetworks[index].password), "%s",
                     password);
        }
    }
    return true;
}

void
saveWifiNetwork(CSimpleIniA& ini, const settings_t* source) {
    char key[32] = {};
    for (int index = 0; index < MAX_WIFI_NETWORKS; index++) {
        snprintf(key, sizeof(key), "network%dSsid", index);
        ini.SetValue("WiFi", key, source->wifiNetworks[index].ssid);

        snprintf(key, sizeof(key), "network%dPassword", index);
        ini.SetValue("WiFi", key, source->wifiNetworks[index].password);
    }
}
#endif

#if defined(COMPILE_ETHERNET)
bool
loadIpAddress(const CSimpleIniA& ini, const char* key, IPAddress* target, char* error, size_t errorLength) {
    const char* value = ini.GetValue("Ethernet", key, nullptr);
    if (value == nullptr) {
        return true;
    }

    IPAddress parsed;
    if (!parsed.fromString(value)) {
        snprintf(error, errorLength, "Ethernet.%s: invalid IP address", key);
        return false;
    }

    *target = parsed;
    return true;
}

bool
loadEthernetAddresses(const CSimpleIniA& ini, settings_t* target, char* error, size_t errorLength) {
    return loadIpAddress(ini, "ethernetDNS", &target->ethernetDNS, error, errorLength)
           && loadIpAddress(ini, "ethernetGateway", &target->ethernetGateway, error, errorLength)
           && loadIpAddress(ini, "ethernetIP", &target->ethernetIP, error, errorLength)
           && loadIpAddress(ini, "ethernetSubnet", &target->ethernetSubnet, error, errorLength);
}

void
saveIpAddress(CSimpleIniA& ini, const char* key, const IPAddress& value) {
    const String text = value.toString();
    ini.SetValue("Ethernet", key, text.c_str());
}

void
saveEthernetAddresses(CSimpleIniA& ini, const settings_t* source) {
    saveIpAddress(ini, "ethernetDNS", source->ethernetDNS);
    saveIpAddress(ini, "ethernetGateway", source->ethernetGateway);
    saveIpAddress(ini, "ethernetIP", source->ethernetIP);
    saveIpAddress(ini, "ethernetSubnet", source->ethernetSubnet);
}
#endif

#if defined(COMPILE_MOSAICX5)
bool
loadUint8Array(const CSimpleIniA& ini, const char* section, const char* keyPrefix, uint8_t* values, size_t count,
               char* error, size_t errorLength) {
    char key[48] = {};
    for (size_t index = 0; index < count; index++) {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index));
        const char* value = ini.GetValue(section, key, nullptr);
        if (value == nullptr) {
            continue;
        }
        uint64_t parsed = 0;
        if (!parseUnsigned(value, &parsed) || (parsed > UINT8_MAX)) {
            snprintf(error, errorLength, "%s.%s: invalid uint8 value", section, key);
            return false;
        }
        values[index] = static_cast<uint8_t>(parsed);
    }
    return true;
}

bool
loadFloatArray(const CSimpleIniA& ini, const char* section, const char* keyPrefix, float* values, size_t count,
               char* error, size_t errorLength) {
    char key[48] = {};
    for (size_t index = 0; index < count; index++) {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index));
        const char* value = ini.GetValue(section, key, nullptr);
        if (value == nullptr) {
            continue;
        }
        double parsed = 0.0;
        if (!parseDoubleValue(value, &parsed) || (parsed < -FLT_MAX) || (parsed > FLT_MAX)) {
            snprintf(error, errorLength, "%s.%s: invalid float value", section, key);
            return false;
        }
        values[index] = static_cast<float>(parsed);
    }
    return true;
}

void
saveUint8Array(CSimpleIniA& ini, const char* section, const char* keyPrefix, const uint8_t* values, size_t count) {
    char key[48] = {};
    char value[16] = {};
    for (size_t index = 0; index < count; index++) {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index));
        snprintf(value, sizeof(value), "%u", static_cast<unsigned>(values[index]));
        ini.SetValue(section, key, value);
    }
}

void
saveFloatArray(CSimpleIniA& ini, const char* section, const char* keyPrefix, const float* values, size_t count) {
    char key[48] = {};
    char value[32] = {};
    for (size_t index = 0; index < count; index++) {
        snprintf(key, sizeof(key), "%s%u", keyPrefix, static_cast<unsigned>(index));
        snprintf(value, sizeof(value), "%.7g", static_cast<double>(values[index]));
        ini.SetValue(section, key, value);
    }
}

bool
loadMosaicArrays(const CSimpleIniA& ini, settings_t* target, char* error, size_t errorLength) {
    return loadUint8Array(ini, "Mosaic", "constellation", target->mosaicConstellations, MAX_MOSAIC_CONSTELLATIONS,
                          error, errorLength)
           && loadUint8Array(ini, "Mosaic", "nmeaStream", target->mosaicMessageStreamNMEA, MAX_MOSAIC_NMEA_MSG, error,
                             errorLength)
           && loadUint8Array(ini, "Mosaic", "nmeaStreamInterval", target->mosaicStreamIntervalsNMEA,
                             MOSAIC_NUM_NMEA_STREAMS, error, errorLength)
           && loadFloatArray(ini, "Mosaic", "rtcmRoverInterval", target->mosaicMessageIntervalsRTCMv3Rover,
                             MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS, error, errorLength)
           && loadFloatArray(ini, "Mosaic", "rtcmBaseInterval", target->mosaicMessageIntervalsRTCMv3Base,
                             MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS, error, errorLength)
           && loadUint8Array(ini, "Mosaic", "rtcmRoverEnabled", target->mosaicMessageEnabledRTCMv3Rover,
                             MAX_MOSAIC_RTCM_V3_MSG, error, errorLength)
           && loadUint8Array(ini, "Mosaic", "rtcmBaseEnabled", target->mosaicMessageEnabledRTCMv3Base,
                             MAX_MOSAIC_RTCM_V3_MSG, error, errorLength);
}

void
saveMosaicArrays(CSimpleIniA& ini, const settings_t* source) {
    saveUint8Array(ini, "Mosaic", "constellation", source->mosaicConstellations, MAX_MOSAIC_CONSTELLATIONS);
    saveUint8Array(ini, "Mosaic", "nmeaStream", source->mosaicMessageStreamNMEA, MAX_MOSAIC_NMEA_MSG);
    saveUint8Array(ini, "Mosaic", "nmeaStreamInterval", source->mosaicStreamIntervalsNMEA, MOSAIC_NUM_NMEA_STREAMS);
    saveFloatArray(ini, "Mosaic", "rtcmRoverInterval", source->mosaicMessageIntervalsRTCMv3Rover,
                   MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS);
    saveFloatArray(ini, "Mosaic", "rtcmBaseInterval", source->mosaicMessageIntervalsRTCMv3Base,
                   MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS);
    saveUint8Array(ini, "Mosaic", "rtcmRoverEnabled", source->mosaicMessageEnabledRTCMv3Rover, MAX_MOSAIC_RTCM_V3_MSG);
    saveUint8Array(ini, "Mosaic", "rtcmBaseEnabled", source->mosaicMessageEnabledRTCMv3Base, MAX_MOSAIC_RTCM_V3_MSG);
}
#endif

bool
profileExists(const char* name) {
    char path[96] = {};
    return settingsFileBuildProfilePath(name, path, sizeof(path)) && LittleFS.exists(path);
}

} // namespace

bool
settingsFileNameIsSafe(const char* name) {
    if ((name == nullptr) || (name[0] == 0)) {
        return false;
    }

    const size_t length = strlen(name);
    if ((length > kProfileMaxNameLength) || (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0)
        || (strcmp(name, "active.txt") == 0)) {
        return false;
    }

    for (size_t index = 0; index < length; index++) {
        const char c = name[index];
        const bool valid = isalnum(static_cast<unsigned char>(c)) || (c == '-') || (c == '_') || (c == '.');
        if (!valid) {
            return false;
        }
    }

    return (strstr(name, "..") == nullptr) && (strchr(name, '/') == nullptr) && (strchr(name, '\\') == nullptr);
}

bool
settingsFileBuildProfilePath(const char* name, char* path, const size_t pathLength) {
    if (!settingsFileNameIsSafe(name)) {
        return false;
    }
    const int written = snprintf(path, pathLength, "%s/%s", SETTINGS_FILE_PROFILE_DIR, name);
    return (written > 0) && (static_cast<size_t>(written) < pathLength);
}

bool
settingsFileEnsureProfileDir() {
    if (!online_devices.littlefs) {
        return false;
    }
    if (LittleFS.exists(SETTINGS_FILE_PROFILE_DIR)) {
        return true;
    }
    return LittleFS.mkdir(SETTINGS_FILE_PROFILE_DIR);
}

bool
settingsFileReadActiveProfile(char* name, const size_t nameLength) {
    if ((name == nullptr) || (nameLength == 0)) {
        return false;
    }
    name[0] = 0;
    if (!online_devices.littlefs || !LittleFS.exists(SETTINGS_FILE_ACTIVE_PROFILE_FILE)) {
        return false;
    }

    File file = LittleFS.open(SETTINGS_FILE_ACTIVE_PROFILE_FILE, "r");
    if (!file) {
        return false;
    }

    const size_t bytes = file.readBytes(name, nameLength - 1);
    name[bytes] = 0;
    file.close();

    char* newline = strpbrk(name, "\r\n");
    if (newline != nullptr) {
        *newline = 0;
    }
    return settingsFileNameIsSafe(name);
}

bool
settingsFileWriteActiveProfile(const char* name) {
    if (!settingsFileEnsureProfileDir() || !profileExists(name)) {
        return false;
    }

    File file = LittleFS.open(SETTINGS_FILE_ACTIVE_PROFILE_FILE, "w");
    if (!file) {
        return false;
    }
    const size_t written = file.print(name);
    file.close();
    return written == strlen(name);
}

bool
settingsFileLoad(const char* path, settings_t* target, char* error, const size_t errorLength) {
    if ((path == nullptr) || (target == nullptr)) {
        setError(error, errorLength, "Invalid load arguments");
        return false;
    }

    CSimpleIniA ini;
    ini.SetUnicode();
    const SI_Error loadStatus = ini.LoadFile(path);
    if (loadStatus < 0) {
        setError(error, errorLength, "Failed to load INI file");
        return false;
    }

    const long schemaVersion = ini.GetLongValue("Meta", "schemaVersion", kSettingsSchemaVersion);
    if (schemaVersion > kSettingsSchemaVersion) {
        setError(error, errorLength, "Unsupported settings schema version");
        return false;
    }

    for (const SettingsField& field : settingsFields) {
        char fieldError[80] = {};
        if (!loadField(ini, field, target, fieldError, sizeof(fieldError))) {
            if ((error != nullptr) && (errorLength > 0)) {
                snprintf(error, errorLength, "%s.%s: %s", field.section, field.key, fieldError);
            }
            return false;
        }
    }

#if defined(COMPILE_WIFI)
    if (!loadWifiNetwork(ini, target, error, errorLength)) {
        return false;
    }
#endif
#if defined(COMPILE_ETHERNET)
    if (!loadEthernetAddresses(ini, target, error, errorLength)) {
        return false;
    }
#endif
#if defined(COMPILE_MOSAICX5)
    if (!loadMosaicArrays(ini, target, error, errorLength)) {
        return false;
    }
#endif

    target->sizeOfSettings = sizeof(settings_t);
    return true;
}

bool
settingsFileSave(const char* path, const settings_t* source) {
    if ((path == nullptr) || (source == nullptr) || !settingsFileEnsureProfileDir()) {
        return false;
    }

    CSimpleIniA ini;
    ini.SetUnicode();
    ini.SetSpaces(true);
    ini.SetValue("Meta", "schemaVersion", "1");
    ini.SetValue("Meta", "product", productPropertiesTable[productType].name);
    ini.SetLongValue("Meta", "settingsSize", sizeof(settings_t));

    for (const SettingsField& field : settingsFields) {
        saveField(ini, field, source);
    }
#if defined(COMPILE_WIFI)
    saveWifiNetwork(ini, source);
#endif
#if defined(COMPILE_ETHERNET)
    saveEthernetAddresses(ini, source);
#endif
#if defined(COMPILE_MOSAICX5)
    saveMosaicArrays(ini, source);
#endif

    const SI_Error saveStatus = ini.SaveFile(path, false);
    if (saveStatus < 0) {
        ESP_LOGE(TAG, "Failed to save %s: %d", path, saveStatus);
        return false;
    }
    return true;
}

bool
settingsFileValidate(const char* path, char* error, const size_t errorLength) {
    settings_t candidate = settings;
    return settingsFileLoad(path, &candidate, error, errorLength);
}

bool
settingsFileApplyActiveProfile() {
    char name[kProfileMaxNameLength + 1] = {};
    char path[96] = {};
    char error[120] = {};

    if (!settingsFileReadActiveProfile(name, sizeof(name))) {
        snprintf(name, sizeof(name), "%s", SETTINGS_FILE_DEFAULT_PROFILE);
    }
    if (!settingsFileBuildProfilePath(name, path, sizeof(path)) || !LittleFS.exists(path)) {
        ESP_LOGW(TAG, "Active profile missing, creating %s", SETTINGS_FILE_DEFAULT_PROFILE);
        snprintf(name, sizeof(name), "%s", SETTINGS_FILE_DEFAULT_PROFILE);
        snprintf(settings.profileName, sizeof(settings.profileName), "%s", name);
        if (!settingsFileBuildProfilePath(name, path, sizeof(path)) || !settingsFileSave(path, &settings)
            || !settingsFileWriteActiveProfile(name)) {
            ESP_LOGE(TAG, "Failed to create default settings profile");
            return false;
        }
    }

    settings_t loaded = settings;
    if (!settingsFileLoad(path, &loaded, error, sizeof(error))) {
        ESP_LOGE(TAG, "Profile %s rejected: %s", name, error);
        return false;
    }

    settings = loaded;
    snprintf(settings.profileName, sizeof(settings.profileName), "%s", name);
    settingsDirty = false;
    ESP_LOGI(TAG, "Loaded settings profile: %s", name);
    return true;
}

bool
settingsFileActivateProfile(const char* name) {
    char path[96] = {};
    char error[120] = {};

    if (!settingsFileBuildProfilePath(name, path, sizeof(path)) || !LittleFS.exists(path)) {
        return false;
    }
    settings_t candidate = settings;
    if (!settingsFileLoad(path, &candidate, error, sizeof(error))) {
        ESP_LOGE(TAG, "Profile %s rejected: %s", name, error);
        return false;
    }
    if (!settingsFileWriteActiveProfile(name)) {
        return false;
    }

    settings = candidate;
    snprintf(settings.profileName, sizeof(settings.profileName), "%s", name);
    settingsDirty = false;
    ESP_LOGI(TAG, "Activated settings profile: %s", name);
    return true;
}

void
settingsFileInit() {
    if (!online_devices.littlefs || !settingsFileEnsureProfileDir()) {
        ESP_LOGW(TAG, "LittleFS unavailable; settings profile load skipped");
        return;
    }
    if (!settingsFileApplyActiveProfile()) {
        ESP_LOGW(TAG, "Using compiled defaults");
    }
}

void
settingsFileMarkDirty() {
    settingsDirty = true;
}

bool
settingsFileSaveActive() {
    char name[kProfileMaxNameLength + 1] = {};
    char path[96] = {};
    if (!settingsFileReadActiveProfile(name, sizeof(name))) {
        snprintf(name, sizeof(name), "%s", SETTINGS_FILE_DEFAULT_PROFILE);
    }
    if (!settingsFileBuildProfilePath(name, path, sizeof(path))) {
        return false;
    }
    if (!settingsFileSave(path, &settings)) {
        return false;
    }
    settingsFileWriteActiveProfile(name);
    settingsDirty = false;
    ESP_LOGI(TAG, "Saved settings profile: %s", name);
    return true;
}

bool
settingsFileSaveIfDirty() {
    if (!settingsDirty) {
        return true;
    }
    return settingsFileSaveActive();
}

bool
settingsFileIsDirty() {
    return settingsDirty;
}
