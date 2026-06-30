#include "App/Utils/StorageServiceTest.h"

#include <cstdio>

#include "App/Config/Config.h"
#include "App/Utils/StorageService.h"
#include "Support.h"
#include "mcu_settings.h"

namespace {

void
boundedString(char* out, const size_t outLength, const char* value, const size_t valueLength) {
    if (out == nullptr || outLength == 0) {
        return;
    }

    const int written = snprintf(out, outLength, "%.*s", static_cast<int>(valueLength), value ? value : "");
    if (written < 0) {
        out[0] = 0;
    }
}

bool
addCurrentSettings(StorageService& config) {
    char text[64] = {};

    bool ok = true;

    ok = config.setInt("Meta", "sizeOfSettings", settings.sizeOfSettings) && ok;

    ok = config.setInt("Antenna", "antennaHeight_mm", settings.antennaHeight_mm) && ok;
    ok = config.setDouble("Antenna", "antennaPhaseCenter_mm", settings.antennaPhaseCenter_mm) && ok;
    ok = config.setInt("Antenna", "ARPLoggingInterval_s", settings.ARPLoggingInterval_s) && ok;
    ok = config.setBool("Antenna", "enableARPLogging", settings.enableARPLogging) && ok;

    boundedString(text, sizeof(text), settings.baseId, sizeof(settings.baseId));
    ok = config.setString("Base", "baseId", text) && ok;
    ok = config.setBool("Base", "baseCasterOverride", settings.baseCasterOverride) && ok;
    ok = config.setDouble("Base", "fixedAltitude", settings.fixedAltitude) && ok;
    ok = config.setBool("Base", "fixedBase", settings.fixedBase) && ok;
    ok = config.setBool("Base", "fixedBaseCoordinateType", settings.fixedBaseCoordinateType) && ok;
    ok = config.setDouble("Base", "fixedEcefX", settings.fixedEcefX) && ok;
    ok = config.setDouble("Base", "fixedEcefY", settings.fixedEcefY) && ok;
    ok = config.setDouble("Base", "fixedEcefZ", settings.fixedEcefZ) && ok;
    ok = config.setDouble("Base", "fixedLat", settings.fixedLat) && ok;
    ok = config.setDouble("Base", "fixedLong", settings.fixedLong) && ok;
    ok = config.setInt("Base", "observationSeconds", settings.observationSeconds) && ok;
    ok = config.setDouble("Base", "observationPositionAccuracy", settings.observationPositionAccuracy) && ok;
    ok = config.setDouble("Base", "surveyInStartingAccuracy", settings.surveyInStartingAccuracy) && ok;

    ok = config.setBool("Battery", "enablePrintBatteryMessages", settings.enablePrintBatteryMessages) && ok;
    ok = config.setInt("Battery", "shutdownNoChargeTimeoutMinutes", settings.shutdownNoChargeTimeoutMinutes) && ok;

    ok = config.setInt("Bluetooth", "bluetoothRadioType", settings.bluetoothRadioType) && ok;
    ok = config.setInt("Bluetooth", "sppRxQueueSize", settings.sppRxQueueSize) && ok;
    ok = config.setInt("Bluetooth", "sppTxQueueSize", settings.sppTxQueueSize) && ok;

    ok = config.setInt("Corrections", "correctionsSourcesLifetime_s", settings.correctionsSourcesLifetime_s) && ok;
    ok = config.setBool("Corrections", "debugCorrections", settings.debugCorrections) && ok;
    ok = config.setInt("Corrections", "enableExtCorrRadio", settings.enableExtCorrRadio) && ok;

    ok = config.setBool("GNSS", "debugGnss", settings.debugGnss) && ok;
    ok = config.setBool("GNSS", "enablePrintPosition", settings.enablePrintPosition) && ok;
    ok = config.setInt("GNSS", "serialGNSSRxFullThreshold", settings.serialGNSSRxFullThreshold) && ok;
    ok = config.setInt("GNSS", "uartReceiveBufferSize", settings.uartReceiveBufferSize) && ok;

    ok = config.setBool("Network", "mdnsEnable", settings.mdnsEnable) && ok;
    ok = config.setString("Network", "mdnsHostName", settings.mdnsHostName) && ok;
    ok = config.setBool("Network", "debugAppleAccessory", settings.debugAppleAccessory) && ok;
    ok = config.setBool("Network", "debugNetworkLayer", settings.debugNetworkLayer) && ok;
    ok = config.setBool("Network", "printNetworkStatus", settings.printNetworkStatus) && ok;
    ok = config.setInt("Network", "networkClientWriteTimeout_ms", settings.networkClientWriteTimeout_ms) && ok;

    ok = config.setInt("OS", "btReadTaskCore", settings.btReadTaskCore) && ok;
    ok = config.setInt("OS", "btReadTaskPriority", settings.btReadTaskPriority) && ok;
    ok = config.setBool("OS", "debugMalloc", settings.debugMalloc) && ok;
    ok = config.setBool("OS", "enableHeapReport", settings.enableHeapReport) && ok;
    ok = config.setBool("OS", "enablePrintIdleTime", settings.enablePrintIdleTime) && ok;
    ok = config.setBool("OS", "enablePsram", settings.enablePsram) && ok;
    ok = config.setBool("OS", "enableTaskReports", settings.enableTaskReports) && ok;
    ok = config.setInt("OS", "gnssReadTaskCore", settings.gnssReadTaskCore) && ok;
    ok = config.setInt("OS", "gnssReadTaskPriority", settings.gnssReadTaskPriority) && ok;
    ok = config.setInt("OS", "gnssReadTaskStackSize", settings.gnssReadTaskStackSize) && ok;
    ok = config.setBool("OS", "haltOnPanic", settings.haltOnPanic) && ok;
    ok = config.setInt("OS", "measurementScale", settings.measurementScale) && ok;
    ok = config.setBool("OS", "printBootTimes", settings.printBootTimes) && ok;
    ok = config.setBool("OS", "printPartitionTable", settings.printPartitionTable) && ok;
    ok = config.setBool("OS", "printTaskStartStop", settings.printTaskStartStop) && ok;
    ok = config.setInt("OS", "psramMallocLevel", settings.psramMallocLevel) && ok;
    ok = config.setInt("OS", "rebootMinutes", settings.rebootMinutes) && ok;
    ok = config.setInt("OS", "resetCount", settings.resetCount) && ok;

    ok = config.setString("Profiles", "profileName", settings.profileName) && ok;

    ok = config.setInt("Rover", "dynamicModel", settings.dynamicModel) && ok;
    ok = config.setBool("Rover", "enablePrintRoverAccuracy", settings.enablePrintRoverAccuracy) && ok;
    ok = config.setBool("Rover", "enableMultipathMitigation", settings.enableMultipathMitigation) && ok;
    ok = config.setInt("Rover", "minCN0", settings.minCN0) && ok;
    ok = config.setInt("Rover", "minElev", settings.minElev) && ok;

    ok = config.setBool("RTC", "enablePrintRtcSync", settings.enablePrintRtcSync) && ok;
    ok = config.setBool("RTCM", "debugRtcmBuffers", settings.debugRtcmBuffers) && ok;

    ok = config.setInt("Serial", "dataPortBaud", settings.dataPortBaud) && ok;
    ok = config.setBool("Serial", "enableGnssToUsbSerial", settings.enableGnssToUsbSerial) && ok;
    ok = config.setInt("Serial", "radioPortBaud", settings.radioPortBaud) && ok;
    ok = config.setInt("Serial", "serialTimeoutGNSS", settings.serialTimeoutGNSS) && ok;
    ok = config.setBool("Serial", "enableNmeaOnRadio", settings.enableNmeaOnRadio) && ok;

    ok = config.setInt("Radio", "radioConfigNumber", settings.radioConfigNumber) && ok;
    ok = config.setInt("Radio", "radioConfigStatus", settings.radioConfigStatus) && ok;
    ok = config.setInt("Radio", "radioConfigWorkMode", settings.radioConfigWorkMode) && ok;
    ok = config.setInt("Radio", "radioConfigChannel", settings.radioConfigChannel) && ok;
    ok = config.setDouble("Radio", "radioConfigTxFrequency", settings.radioConfigTxFrequency) && ok;
    ok = config.setDouble("Radio", "radioConfigRxFrequency", settings.radioConfigRxFrequency) && ok;
    ok = config.setInt("Radio", "radioConfigPower", settings.radioConfigPower) && ok;
    ok = config.setInt("Radio", "radioConfigProtocol", settings.radioConfigProtocol) && ok;
    ok = config.setInt("Radio", "radioConfigAirRate", settings.radioConfigAirRate) && ok;
    ok = config.setInt("Radio", "radioConfigDataFormat", settings.radioConfigDataFormat) && ok;

    ok = config.setBool("State", "enablePrintDuplicateStates", settings.enablePrintDuplicateStates) && ok;
    ok = config.setBool("State", "enablePrintStates", settings.enablePrintStates) && ok;
    ok = config.setInt("State", "lastState", settings.lastState) && ok;

    ok = config.setInt("TimeZone", "timeZoneHours", settings.timeZoneHours) && ok;
    ok = config.setInt("TimeZone", "timeZoneMinutes", settings.timeZoneMinutes) && ok;
    ok = config.setInt("TimeZone", "timeZoneSeconds", settings.timeZoneSeconds) && ok;

    ok = config.setBool("UM980", "enableImuCompensationDebug", settings.enableImuCompensationDebug) && ok;
    ok = config.setBool("UM980", "enableImuDebug", settings.enableImuDebug) && ok;
    ok = config.setBool("UM980", "enableTiltCompensation", settings.enableTiltCompensation) && ok;

    ok = config.setBool("Logging", "enableLoggingRINEX", settings.enableLoggingRINEX) && ok;
    ok = config.setBool("Logging", "externalEventPolarity", settings.externalEventPolarity) && ok;

    ok = config.setInt("Web Server", "httpPort", settings.httpPort) && ok;

#if defined(COMPILE_WIFI)
    ok = config.setBool("WiFi", "debugWebServer", settings.debugWebServer) && ok;
    ok = config.setBool("WiFi", "debugWifiState", settings.debugWifiState) && ok;
    ok = config.setInt("WiFi", "wifiChannel", settings.wifiChannel) && ok;
    ok = config.setBool("WiFi", "wifiConfigOverAP", settings.wifiConfigOverAP) && ok;
    ok = config.setInt("WiFi", "wifiConnectTimeoutMs", settings.wifiConnectTimeoutMs) && ok;
    for (int index = 0; index < MAX_WIFI_NETWORKS; ++index) {
        char section[24] = {};
        snprintf(section, sizeof(section), "WiFi Network %d", index);
        ok = config.setString(section, "ssid", settings.wifiNetworks[index].ssid) && ok;
        ok = config.setString(section, "password", settings.wifiNetworks[index].password) && ok;
    }
#endif

    ok = config.setBool("Settings", "debugSettings", settings.debugSettings) && ok;
    ok = config.setInt("Settings", "gnssConfigureRequest", settings.gnssConfigureRequest) && ok;
    ok = config.setBool("Settings", "debugGnssConfig", settings.debugGnssConfig) && ok;

    ok = config.setInt("PPP", "pppMode", settings.pppMode) && ok;
    ok = config.setInt("PPP", "pppDatum", settings.pppDatum) && ok;
    ok = config.setInt("PPP", "pppTimeout", settings.pppTimeout) && ok;
    ok = config.setDouble("PPP", "pppHorizontalConvergence", settings.pppHorizontalConvergence) && ok;
    ok = config.setDouble("PPP", "pppVerticalConvergence", settings.pppVerticalConvergence) && ok;

    return ok;
}

void
printEntry(const char* section, const char* key, const char* value, void* context) {
    uint32_t* count = static_cast<uint32_t*>(context);
    if (count != nullptr) {
        (*count)++;
    }
    systemPrintf("[%s] %s=%s\n", section ? section : "", key ? key : "", value ? value : "");
}

} // namespace

bool
storageServiceSaveReadPrintTest() {
    StorageService writer;
    if (!addCurrentSettings(writer)) {
        systemPrintf("StorageService test: add settings failed: %s\n", writer.lastError());
        return false;
    }

    char configPath[48] = {};
    const int written = snprintf(configPath, sizeof(configPath), CONFIG_FILE_PATTERN,
                                 static_cast<unsigned>(CONFIG_DEFAULT_INDEX));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(configPath)) {
        systemPrintln("StorageService test: failed to build config path");
        return false;
    }

    if (!writer.saveAs(configPath)) {
        systemPrintf("StorageService test: save failed: %s\n", writer.lastError());
        return false;
    }

    StorageService reader;
    if (!reader.loadFrom(configPath)) {
        systemPrintf("StorageService test: load failed: %s\n", reader.lastError());
        return false;
    }

    uint32_t count = 0;
    systemPrintln("StorageService test: loaded key-values");
    reader.forEach(printEntry, &count);
    systemPrintf("StorageService test: printed %lu key-values\n", static_cast<unsigned long>(count));
    return true;
}
