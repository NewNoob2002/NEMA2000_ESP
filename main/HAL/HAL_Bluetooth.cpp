#include "Bluetooth.h"
#include "HAL.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "HardwareSerial.h"
#include "SparkFun_Extensible_Message_Parser.h"
#include "States.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "esp_app_desc.h"
#include "mcu_settings.h"

namespace HAL {
extern HardwareSerial* gnssSerial;
extern UnicoreUM980* gUm980;
} // namespace HAL

namespace {

#define TAG "[HAL_Bluetooth] "
// constexpr uint8_t kMsgSenderApp = 0x00;
constexpr uint8_t kMsgSenderDevice = 0x01;
// constexpr uint8_t kMsgSenderPannel = 0x02;

constexpr uint8_t kMsgQueryType = 0x00;
constexpr uint8_t kMsgQueryRespType = 0x01;
constexpr uint8_t kMsgSetType = 0x02;
constexpr uint8_t kMsgSetRespType = 0x03;
constexpr uint8_t kResponseError = 0x05;

constexpr uint8_t kWorkModeRover = 0x00;
constexpr uint8_t kWorkModeBase = 0x01;
constexpr uint8_t kBaseKnownPoint = 0x01;
constexpr uint8_t kBaseSinglePoint = 0x02;
constexpr uint8_t kBaseDisabled = 0x00;
constexpr uint8_t kBaseEnabled = 0x01;

constexpr size_t kBluetoothRxBufferSize = 512;
constexpr size_t kBluetoothMaxPayload = 256;
constexpr size_t kBluetoothTxBufferSize = sizeof(SEMP_CUSTOM_HEADER) + kBluetoothMaxPayload + sizeof(uint32_t);
constexpr uint32_t kBluetoothParserBufferSize = 1024 * 2;
constexpr uint32_t kBluetoothReadTaskStack = 3072;

enum BluetoothMessageId : uint16_t {
    kMsgIdModelQuery = 0x0001,
    kMsgIdDeviceStatusQuery = 0x0002,
    kMsgIdBatteryStorageQuery = 0x0003,
    kMsgIdGPGGAGnssMessageSet = 0x0004,
    kMsgIdSATSINFOAMessageSet = 0x0005,
    kMsgIdSatelliteTrackingSet = 0x0006,
    kMsgIdWorkModeConfig = 0x0007,
    kMsgIdCOMConfig = 0x000D,
    kMsgIdRadioConfig = 0x0014,
    kMsgIdLogConfig = 0x0015,
    kMsgIdGPGSTGnssMessageSet = 0x0021,
    kMsgIdGPRMCGnssMessageSet = 0x0022,
    kMsgIdGPGSAGnssMessageSet = 0x0023,
    kMsgIdImuAntennaHeightSet = 0x0024,
    kMsgIdImuDataQuery = 0x0025,
    kMsgIdTiltCompensationConfig = 0x0026,
    kMsgIdPhoneNetworkConfig = 0x0027,
    kMsgIdWifiControl = 0x0028,
    kMsgIdBASEINFOPacket = 0x0029,
    kMsgIdRegister = 0x0030,
    kMsgIdShutdown = 0x0031,
    kMsgIdLogAntennaHeightConfig = 0x0032,
    kMsgIdPPPConfig = 0x0033,
    kMsgIdExternalRadioConfig = 0x0034,
};

struct BluetoothMessageName {
    uint16_t id;
    const char* name;
};

constexpr BluetoothMessageName kMessageNames[] = {
    {kMsgIdModelQuery, "ModelQuery"},
    {kMsgIdDeviceStatusQuery, "DeviceStatusQuery"},
    {kMsgIdBatteryStorageQuery, "BatteryStorageQuery"},
    {kMsgIdGPGGAGnssMessageSet, "GPGGAGnssMessageSet"},
    {kMsgIdSATSINFOAMessageSet, "SATSINFOAMessageSet"},
    {kMsgIdSatelliteTrackingSet, "SatelliteTrackingSet"},
    {kMsgIdWorkModeConfig, "WorkModeConfig"},
    {kMsgIdCOMConfig, "COMConfig"},
    {kMsgIdRadioConfig, "RadioConfig"},
    {kMsgIdLogConfig, "LogConfig"},
    {kMsgIdGPGSTGnssMessageSet, "GPGSTGnssMessageSet"},
    {kMsgIdGPRMCGnssMessageSet, "GPRMCGnssMessageSet"},
    {kMsgIdGPGSAGnssMessageSet, "GPGSAGnssMessageSet"},
    {kMsgIdImuAntennaHeightSet, "ImuAntennaHeightSet"},
    {kMsgIdImuDataQuery, "ImuDataQuery"},
    {kMsgIdTiltCompensationConfig, "TiltCompensationConfig"},
    {kMsgIdPhoneNetworkConfig, "PhoneNetworkConfig"},
    {kMsgIdWifiControl, "WifiControl"},
    {kMsgIdBASEINFOPacket, "BASEINFOPacket"},
    {kMsgIdRegister, "Register"},
    {kMsgIdShutdown, "Shutdown"},
    {kMsgIdLogAntennaHeightConfig, "LogAntennaHeightConfig"},
    {kMsgIdPPPConfig, "PPPConfig"},
    {kMsgIdExternalRadioConfig, "ExternalRadioConfig"},
};

const SEMP_PARSE_ROUTINE kBluetoothParserTable[] = {
    sempCustomPreamble,
    sempRtcmPreamble,
};

enum BluetoothParserIndex : uint16_t {
    kBluetoothParserCustom = 0,
    kBluetoothParserRtcm = 1,
};

const char* const kBluetoothParserNames[] = {
    "BluetoothAPP",
    "BluetoothRTCM",
};

constexpr uint16_t kBluetoothParserCount = sizeof(kBluetoothParserTable) / sizeof(kBluetoothParserTable[0]);
constexpr uint16_t kBluetoothParserNameCount = sizeof(kBluetoothParserNames) / sizeof(kBluetoothParserNames[0]);

TaskHandle_t btReadTaskHandle = nullptr;
SEMP_PARSE_STATE* btParser = nullptr;
uint8_t bluetoothRxBuffer[kBluetoothRxBufferSize] = {};
uint8_t messageTxBuffer[kBluetoothTxBufferSize] = {};

void
parserDebugPrintf(const char* format, ...) {
    if (!format) {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}

uint16_t
getMessageId(const SEMP_CUSTOM_HEADER& header) {
    return static_cast<uint16_t>(header.messageId_L) | (static_cast<uint16_t>(header.messageId_H) << 8);
}

const char*
bluetoothMessageName(const uint16_t messageId) {
    for (const BluetoothMessageName& message : kMessageNames) {
        if (message.id == messageId) {
            return message.name;
        }
    }
    return "Unknown";
}

void
setMessageId(SEMP_CUSTOM_HEADER& header, const uint16_t messageId) {
    header.messageId_L = static_cast<uint8_t>(messageId & 0xFF);
    header.messageId_H = static_cast<uint8_t>((messageId >> 8) & 0xFF);
}

uint32_t
computeBluetoothCrc(const uint8_t* data, const size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t index = 0; index < length; index++) {
        crc = semp_crc32Table[(crc ^ data[index]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

void
writeLe32(uint8_t* dest, const uint32_t value) {
    dest[0] = static_cast<uint8_t>(value & 0xFF);
    dest[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dest[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dest[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

double
applyHemisphere(double value, const char positiveHemisphere, const char negativeHemisphere, const char hemisphere) {
    if ((hemisphere == positiveHemisphere) || (hemisphere == negativeHemisphere)) {
        value = std::fabs(value);
        if (hemisphere == negativeHemisphere) {
            value = -value;
        }
    }
    return value;
}

bool
isValidGeodeticPosition(const double longitude, const double latitude, const double altitude) {
    return std::isfinite(longitude) && std::isfinite(latitude) && std::isfinite(altitude) && (longitude >= -180.0)
           && (longitude <= 180.0) && (latitude >= -90.0) && (latitude <= 90.0) && (altitude > -10000.0)
           && (altitude < 100000.0);
}

void
copyFixedString(uint8_t* dest, const size_t length, const char* source) {
    std::memset(dest, 0, length);
    if (source) {
        std::strncpy(reinterpret_cast<char*>(dest), source, length);
    }
}

bool
requestHasPayload(const SEMP_PARSE_STATE* parse, const SEMP_CUSTOM_HEADER& header) {
    return parse && (parse->length >= (sizeof(SEMP_CUSTOM_HEADER) + header.messageLength + sizeof(uint32_t)));
}

struct BluetoothResponse {
    uint16_t messageId = 0;
    uint8_t messageType = 0;
    uint8_t msgInterval = 0;
    uint8_t* payload = nullptr;
    uint16_t payloadLength = 0;
    char* msgName = nullptr;
    bool allocated = false;
};

bool
allocateResponse(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint16_t payloadLength) {
    if (payloadLength > kBluetoothMaxPayload) {
        systemPrintf("Bluetooth response too large: %u\n", payloadLength);
        return false;
    }

    std::memset(messageTxBuffer, 0, kBluetoothTxBufferSize);
    auto* header = reinterpret_cast<SEMP_CUSTOM_HEADER*>(messageTxBuffer);
    header->syncA = 0xAA;
    header->syncB = 0x44;
    header->syncC = 0x18;
    header->headerLength = sizeof(SEMP_CUSTOM_HEADER);
    setMessageId(*header, response.messageId);
    header->messageLength = payloadLength;
    header->sender = kMsgSenderDevice;
    header->messageType = response.messageType;
    header->Protocol = requestHeader.Protocol ? requestHeader.Protocol : 1;
    header->MsgInterval = response.msgInterval;

    response.payload = messageTxBuffer + sizeof(SEMP_CUSTOM_HEADER);
    response.payloadLength = payloadLength;
    response.allocated = true;
    return true;
}

bool
sendResponse(const BluetoothResponse& response) {
    if (!response.allocated) {
        return false;
    }

    const size_t frameLength = sizeof(SEMP_CUSTOM_HEADER) + response.payloadLength;
    const size_t totalLength = frameLength + sizeof(uint32_t);
    const uint32_t crc = computeBluetoothCrc(messageTxBuffer, frameLength);
    writeLe32(messageTxBuffer + frameLength, crc);
    const int bytesWritten = bluetoothWrite(messageTxBuffer, totalLength);
#if 1
    ESP_LOGI(TAG, "=====>> Send Res %s(0x%04X) (%d/%u): ", response.msgName, response.messageId, bytesWritten,
             static_cast<unsigned int>(totalLength));
#endif
    if (bytesWritten != static_cast<int>(totalLength)) {
        ESP_LOGW(TAG, "Response write incomplete: %s(0x%04X) wrote=%d expected=%u", response.msgName,
                 response.messageId, bytesWritten, static_cast<unsigned int>(totalLength));
        return false;
    }
    return true;
}

bool
ack(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, uint8_t ackresponse) {
    if (!allocateResponse(response, requestHeader, 2)) {
        return false;
    }
    response.payload[0] = ackresponse;
    response.payload[1] = ackresponse;
    return true;
}

// float
// decodeMessagePeriod(const int8_t interval) {
//     if (interval == 0) {
//         return 0.0f;
//     }
//     if (interval > 0) {
//         return 1.0f / static_cast<float>(interval);
//     }
//     return static_cast<float>(-interval);
// }

bool
setNmeaPeriod(const char* messageName, const int8_t interval) {
    if (!messageName) {
        return false;
    }
    // return HAL::gUm980->setNmeaMessagePeriod(messageName, decodeMessagePeriod(interval));
    return true;
}

void
handleModelQuery(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader) {
    if (!allocateResponse(response, requestHeader, 136)) {
        return;
    }
    bool gUm980Present = HAL::gUm980 != nullptr;

    const esp_app_desc_t* app = esp_app_get_description();
    copyFixedString(response.payload + 0, 8, productPropertiesTable[RTK_S20].name);
    copyFixedString(response.payload + 8, 16, productPropertiesTable[RTK_S20].productPlanUID);
    copyFixedString(response.payload + 24, 8, "V1.2");
    copyFixedString(response.payload + 32, 16, app ? app->version : "");
    copyFixedString(response.payload + 56, 8, gUm980Present ? HAL::gUm980->getModelName() : "");
    copyFixedString(response.payload + 64, 16, gUm980Present ? HAL::gUm980->getSerialNumber() : "");
    copyFixedString(response.payload + 80, 16, gUm980Present ? HAL::gUm980->getFirmwareVersion() : "");
    copyFixedString(response.payload + 96, 8, "NONE");
    copyFixedString(response.payload + 104, 16, "NONE");
}

void
handleDeviceStatusQuery(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader) {
    if (!allocateResponse(response, requestHeader, 43)) {
        return;
    }

    response.payload[0] = 0x03;                                          // support Work Mode
    response.payload[1] = 0x01;                                          // Position or Head
    response.payload[2] = online_devices.gnss ? 0x81 : 0x01;             // Gnss Board status
    response.payload[3] = settings.enableTiltCompensation ? 0x81 : 0x01; // IMU
    response.payload[4] = 0x81;                                          // Radio
    response.payload[5] = 0x00;                                          // 4G/5G
    response.payload[9] = 0x00;                                          // Battery
    response.payload[10] = 0x81;                                         // WIfi
    response.payload[11] = 0x81;                                         // Bluetooth
    response.payload[12] = 0x00;                                         // Display
    response.payload[13] = 0x01;                                         // PPP
}

void
handleBatteryStorageQuery(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader) {
    if (!allocateResponse(response, requestHeader, 16)) {
        return;
    }
    response.payload[0] = online_devices.bq40z50 ? 0x01 : 0x00;
    response.payload[2] = online_devices.bq40z50 ? 0x01 : 0x00;
    response.payload[12] = online_devices.bq40z50 ? 0x01 : 0x00;
}

void
handleGnssMessageToggle(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const char* messageName) {
    if ((requestHeader.messageType == kMsgSetType) && HAL::gUm980) {
        setNmeaPeriod(messageName, requestHeader.MsgInterval);
    }
    ack(response, requestHeader, 0x01);
}

void
handleSatelliteTracking(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                        const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 12)) {
            return;
        }
        response.payload[0] = 0x01;
        response.payload[1] = settings.minElev;
        response.payload[4] = 1; // GPS
        response.payload[5] = 1; // GLONASS
        response.payload[6] = 1; // BDS
        response.payload[7] = 1; // Galileo
        return;
    } else if (requestHeader.messageType == kMsgSetType) {
        bool valid = payload && (payloadLength >= 8);
        if (valid) {
            settings.minElev = payload[1];
            if (HAL::gUm980) {
                // HAL::gUm980->setElevation(settings.minElev);
                // HAL::gUm980->setConstellationEnabled("GPS", payload[4] != 0);
                // HAL::gUm980->setConstellationEnabled("GLO", payload[5] != 0);
                // HAL::gUm980->setConstellationEnabled("BDS", payload[6] != 0);
                // HAL::gUm980->setConstellationEnabled("GAL", payload[7] != 0);
                // HAL::gUm980->setConstellations();
            }
        }
        ack(response, requestHeader, 0x01);
    }
}

void
handleWorkMode(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
               const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 48)) {
            return;
        }
        const SystemState_t reportedState = getSystemStateForReporting();
        const bool baseMode =
            (reportedState >= STATE_BASE_CASTER_NOT_STARTED) && (reportedState <= STATE_BASE_FIXED_TRANSMITTING);
        response.payload[0] = baseMode ? kWorkModeBase : kWorkModeRover;
        response.payload[1] = settings.fixedBase ? kBaseKnownPoint : kBaseSinglePoint;
        response.payload[2] = baseMode ? kBaseEnabled : kBaseDisabled;
        std::memcpy(response.payload + 4, &settings.baseId, sizeof(settings.baseId));
        std::memcpy(response.payload + 12, &settings.fixedLong, sizeof(settings.fixedLong));
        response.payload[20] = (settings.fixedLong >= 0.0) ? 'E' : 'W';
        std::memcpy(response.payload + 24, &settings.fixedLat, sizeof(settings.fixedLat));
        response.payload[32] = (settings.fixedLat >= 0.0) ? 'N' : 'S';
        std::memcpy(response.payload + 36, &settings.fixedAltitude, sizeof(settings.fixedAltitude));
        return;
    }
    if (!payload || (payloadLength < 3)) {
        ack(response, requestHeader, kResponseError);
        return;
    }

    const uint8_t requestedMode = payload[0];
    const uint8_t fixedBaseMode = payload[1];
    const uint8_t baseEnable = payload[2];

    if ((requestedMode != kWorkModeRover) && (requestedMode != kWorkModeBase)) {
        ack(response, requestHeader, kResponseError);
        return;
    }
    if ((baseEnable != kBaseDisabled) && (baseEnable != kBaseEnabled)) {
        ack(response, requestHeader, kResponseError);
        return;
    }
    if ((fixedBaseMode != kBaseKnownPoint) && (fixedBaseMode != kBaseSinglePoint)) {
        ack(response, requestHeader, kResponseError);
        return;
    }

    if ((payloadLength >= 12) && payload[4] != 0) {
        copyFixedString(reinterpret_cast<uint8_t*>(settings.baseId), sizeof(settings.baseId),
                        reinterpret_cast<const char*>(payload + 4));
    }

    if ((requestedMode == kWorkModeBase) && (baseEnable == kBaseEnabled) && (fixedBaseMode == kBaseKnownPoint)) {
        if (payloadLength < 44) {
            ack(response, requestHeader, kResponseError);
            return;
        }

        double longitude = 0.0;
        double latitude = 0.0;
        double altitude = 0.0;
        std::memcpy(&longitude, &payload[12], sizeof(longitude));
        std::memcpy(&latitude, &payload[24], sizeof(latitude));
        std::memcpy(&altitude, &payload[36], sizeof(altitude));
        longitude = applyHemisphere(longitude, 'E', 'W', static_cast<char>(payload[20]));
        latitude = applyHemisphere(latitude, 'N', 'S', static_cast<char>(payload[32]));

        if (!isValidGeodeticPosition(longitude, latitude, altitude)) {
            ack(response, requestHeader, kResponseError);
            return;
        }

        settings.fixedBase = true;
        settings.fixedBaseCoordinateType = COORD_TYPE_GEODETIC;
        settings.fixedLong = longitude;
        settings.fixedLat = latitude;
        settings.fixedAltitude = altitude;
    } else {
        settings.fixedBase = false;
    }

    systemPrintf("[Bluetooth] Work mode set: mode=0x%02X fixedBase=0x%02X baseEnable=0x%02X baseId=%s\n", requestedMode,
                 fixedBaseMode, baseEnable, settings.baseId);

    if ((requestedMode == kWorkModeBase) && (baseEnable == kBaseEnabled)) {
        requestChangeState(STATE_BASE_NOT_STARTED);
    } else {
        requestChangeState(STATE_ROVER_NOT_STARTED);
    }

    ack(response, requestHeader, 1);
}

void
handleComConfig(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        allocateResponse(response, requestHeader, 28);
    } else if (requestHeader.messageType == kMsgSetType) {
        ack(response, requestHeader, 1);
    }
}

void
handleRadioConfig(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader) {
    if (requestHeader.messageType == kMsgQueryType) {
        allocateResponse(response, requestHeader, 28);
        response.payload[0] = 0x01; //radio number
        response.payload[1] = 0x01; //radio status 0x00:disabled, 0x01:enabled
        response.payload[2] = 0x01; //radio work mode 0x00:transmit, 0x01:receive
        response.payload[3] = 0x01; //radio channel

        std::memcpy(response.payload + 4, "460.05", sizeof(float));
        std::memcpy(response.payload + 8, "460.05", sizeof(float));

        response.payload[12] = 0x01; //radio power 0x00:low, 0x01:high
        response.payload[13] =
            0x01; //radio protocol 0x01-TRIMTALK，0x02-TRIMMK3，0x04-TT450S，0x05-TRANSEOT，0x09-SOUTH，0x0a-HUACE，0x0d-SATEL，0xf0-CCS
        response.payload[14] = 0x02; //radio airrate 0x02- 9600 and 0x04- 19200

        response.payload[17] = 0x03; //radio data format 0x03-RTCM23，0x04-RTCM30，0x05-RTCM32，0x06-CMR。
        return;
    }
    ack(response, requestHeader, 1);
}

void
handleLogConfig(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 50)) {
            return;
        }
        response.payload[0] = 0x01;
        response.payload[3] = 0x00;
        response.payload[4] = 0x02;
        response.payload[31] = 0x01;
        return;
    }

    if (payload && (payloadLength >= 4)) {
        // settings.enableLogging = payload[3] != 0;
    }
    ack(response, requestHeader, 1);
}

void
handleImuAntennaHeight(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                       const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 16)) {
            return;
        }
        const double heightM = static_cast<double>(settings.antennaHeight_mm) / 1000.0;
        std::memcpy(response.payload, &heightM, sizeof(heightM));
        return;
    }

    if (payload && (payloadLength >= sizeof(double))) {
        double heightM = 0.0;
        std::memcpy(&heightM, payload, sizeof(heightM));
        if (std::isfinite(heightM) && (heightM >= 0.0) && (heightM <= 20.0)) {
            settings.antennaHeight_mm = static_cast<int16_t>(std::lround(heightM * 1000.0));
            ack(response, requestHeader, 1);
            return;
        }
    }
    ack(response, requestHeader, 0x01);
}

void
handleImuData(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
              const uint16_t payloadLength) {
    ack(response, requestHeader, 0x01);
}

void
handleTiltCompensation(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                       const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 4)) {
            return;
        }
        response.payload[0] = settings.enableTiltCompensation ? 0x01 : 0x00;
        return;
    }

    if (payload && (payloadLength >= 1)) {
        settings.enableTiltCompensation = payload[0] != 0;
    }
    ack(response, requestHeader, 0x01);
}

void
handlePhoneNetworkConfig(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                         const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 4)) {
            return;
        }
        response.payload[0] = 0x02;
        return;
    }
    ack(response, requestHeader, 0x01);
}

void
handleWifiControl(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                  const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 8)) {
            return;
        }
        response.payload[0] = 0x00;
        response.payload[1] = 192;
        response.payload[2] = 168;
        response.payload[3] = 10;
        response.payload[4] = 12;
        return;
    } else if (requestHeader.messageType == kMsgSetType) {
        uint8_t wifiStatus = payload[0];
        char wifiInfo[4] = {};
        std::memcpy(wifiInfo, &payload[1], 4);
        systemPrintf("[Bluetooth] Set Wifi Info :%d, %d.%d.%d.%d\n", wifiStatus, wifiInfo[0], wifiInfo[1], wifiInfo[2],
                     wifiInfo[3]);
        ack(response, requestHeader, 0x01);
        if (wifiStatus == 0x01) {
            requestChangeState(STATE_WEB_CONFIG_NOT_STARTED);
        }
    }
}

void
handleBASEINFOPacket(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                     const uint16_t payloadLength) {
    ack(response, requestHeader, 0x01);
}

void
handleRegister(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader) {
    if (requestHeader.messageType == kMsgQueryType) {
        allocateResponse(response, requestHeader, 82);
        return;
    }
    ack(response, requestHeader, 0x01);
}

void
handleShutdown(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
               const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgSetType && payload && (payloadLength >= 1) && payload[0] == 0x01) {
        // requestedSystemState = STATE_SHUTDOWN;
        // newSystemStateRequested = true;
    }
    ack(response, requestHeader, (payload && (payloadLength >= 1)) ? 1 : kResponseError);
}

void
handleLogAntennaHeight(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                       const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 12)) {
            return;
        }
        const float heightM = static_cast<float>(settings.antennaHeight_mm) / 1000.0f;
        std::memcpy(response.payload, &heightM, sizeof(heightM));
        return;
    }

    if (payload && (payloadLength >= sizeof(float))) {
        float heightM = 0.0f;
        std::memcpy(&heightM, payload, sizeof(heightM));
        if (std::isfinite(heightM) && (heightM >= 0.0f) && (heightM <= 20.0f)) {
            settings.antennaHeight_mm = static_cast<int16_t>(std::lround(heightM * 1000.0f));
            ack(response, requestHeader, 1);
            return;
        }
    }
    ack(response, requestHeader, kResponseError);
}

void
handlePppControl(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint8_t* payload,
                 const uint16_t payloadLength) {
    if (requestHeader.messageType == kMsgQueryType) {
        if (!allocateResponse(response, requestHeader, 4)) {
            return;
        }
        response.payload[0] = settings.pppMode == PPP_MODE_DISABLE ? 0x00 : 0x01;
        return;
    }

    if (payload && (payloadLength >= 1)) {
        settings.pppMode = payload[0] ? PPP_MODE_HAS : PPP_MODE_DISABLE;
    }
    ack(response, requestHeader, (payload && (payloadLength >= 1)) ? 1 : kResponseError);
}

void
processBluetoothAppMessage(SEMP_PARSE_STATE* parse) {
    const auto* requestHeader = reinterpret_cast<const SEMP_CUSTOM_HEADER*>(parse->buffer);
    if (!requestHasPayload(parse, *requestHeader)) {
        systemPrintf("Bluetooth APP frame too short: %u\n", parse->length);
        return;
    }

    const uint16_t messageId = getMessageId(*requestHeader);
    const char* messageName = bluetoothMessageName(messageId);
    const uint16_t payloadLength = requestHeader->messageLength;
    const uint8_t* payload = parse->buffer + sizeof(SEMP_CUSTOM_HEADER);
    if (payloadLength == 0) {
        payload = nullptr;
    }

    BluetoothResponse response;
    response.messageId = messageId;
    response.msgName = const_cast<char*>(messageName);
    if (requestHeader->messageType == kMsgQueryType) {
        response.messageType = kMsgQueryRespType;
    } else if (requestHeader->messageType == kMsgSetType) {
        response.messageType = kMsgSetRespType;
    } else {
        response.messageType = kMsgSetRespType;
    }
    response.msgInterval = requestHeader->MsgInterval;

    ESP_LOGI(TAG, "<<===== Recv %s(0x%04X), reqType 0x%02X, resType 0x%02X, length: %u :", response.msgName, messageId,
             requestHeader->messageType, response.messageType, payloadLength);

    switch (messageId) {
        case kMsgIdModelQuery: handleModelQuery(response, *requestHeader); break;
        case kMsgIdDeviceStatusQuery: handleDeviceStatusQuery(response, *requestHeader); break;
        case kMsgIdBatteryStorageQuery: handleBatteryStorageQuery(response, *requestHeader); break;
        case kMsgIdGPGGAGnssMessageSet: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGGA");
        } break;
        case kMsgIdSATSINFOAMessageSet: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGSV");
        } break;
        case kMsgIdSatelliteTrackingSet:
            handleSatelliteTracking(response, *requestHeader, payload, payloadLength);
            break;
        case kMsgIdWorkModeConfig: handleWorkMode(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdCOMConfig: handleComConfig(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdRadioConfig: handleRadioConfig(response, *requestHeader); break;
        case kMsgIdLogConfig: handleLogConfig(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdGPGSTGnssMessageSet: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGST");
        } break;
        case kMsgIdGPRMCGnssMessageSet: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPRMC");
        } break;
        case kMsgIdGPGSAGnssMessageSet: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGSA");
        } break;
        case kMsgIdImuAntennaHeightSet: handleImuAntennaHeight(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdImuDataQuery: {
            response.messageType = kMsgSetRespType;
            handleImuData(response, *requestHeader, payload, payloadLength);
        } break;
        case kMsgIdTiltCompensationConfig:
            handleTiltCompensation(response, *requestHeader, payload, payloadLength);
            break;
        case kMsgIdPhoneNetworkConfig:
            handlePhoneNetworkConfig(response, *requestHeader, payload, payloadLength);
            break;
        case kMsgIdWifiControl: handleWifiControl(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdBASEINFOPacket: {
            response.messageType = kMsgSetRespType;
            handleBASEINFOPacket(response, *requestHeader, payload, payloadLength);
        } break;
        case kMsgIdRegister: handleRegister(response, *requestHeader); break;
        case kMsgIdShutdown: handleShutdown(response, *requestHeader, payload, payloadLength); break;
        case kMsgIdLogAntennaHeightConfig:
            handleLogAntennaHeight(response, *requestHeader, payload, payloadLength);
            break;
        case kMsgIdPPPConfig: {
            response.messageType = kMsgSetRespType;
            handlePppControl(response, *requestHeader, payload, payloadLength);
        } break;
        case kMsgIdExternalRadioConfig: ack(response, *requestHeader, 0x01); break;
        case 0x36: ack(response, *requestHeader, 0x01); break;
        default:
            ESP_LOGW(TAG, "Unknown message: %s(0x%04X) type=0x%02X", messageName, messageId,
                     requestHeader->messageType);
            ack(response, *requestHeader, 1);
            break;
    }

    sendResponse(response);
}

void
processRtcmMessage(SEMP_PARSE_STATE* parse) {
    if (!parse || !parse->buffer || parse->length == 0) {
        return;
    }

    if (HAL::gnssSerial) {
        size_t bytesWritten = HAL::gnssSerial->write(parse->buffer, parse->length);
        if (bytesWritten != parse->length) {
            ESP_LOGE(TAG, "Failed to write RTCM%u to GNSS serial: wrote %d of %u", sempRtcmGetMessageNumber(parse),
                     bytesWritten, parse->length);
        }
    } else {
        ESP_LOGW(TAG, "Dropped RTCM%u: GNSS serial not ready", sempRtcmGetMessageNumber(parse));
    }
}

void
btDataProcess(SEMP_PARSE_STATE* parse, uint16_t type) {
    if (type == kBluetoothParserRtcm) {
        processRtcmMessage(parse);
    } else if (type == kBluetoothParserCustom) {
        processBluetoothAppMessage(parse);
    }
}

void
btReadTask(void* e) {
    (void)e;

    btParser =
        sempBeginParser(kBluetoothParserTable, kBluetoothParserCount, kBluetoothParserNames, kBluetoothParserNameCount,
                        0, kBluetoothParserBufferSize, btDataProcess, "BluetoothDebug", parserDebugPrintf);
    if (!btParser) {
        systemPrintf("Failed to initialize the Bluetooth parser\n");
        btReadTaskHandle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    if (settings.printTaskStartStop) {
        systemPrintln("Task: btReadTask started");
    }
    int rxBytes = 0;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Start notification
    task.bluetoothReadTaskRunning = true;
    if (settings.printTaskStartStop) {
        systemPrintln("Task bluetoothReadTask started");
    }
    // Run task until a request is raised
    while (!task.bluetoothReadTaskStopRequest) {
        if ((bluetoothGetState() == BT_CONNECTED) && bluetoothDataInterfaceIsEnabled()) {
            while (bluetoothRxDataAvailable() > 0) {
                rxBytes = bluetoothRead(bluetoothRxBuffer, sizeof(bluetoothRxBuffer));
                if (rxBytes <= 0) {
                    break;
                }
                for (int index = 0; index < rxBytes; index++) {
                    sempParseNextByte(btParser, bluetoothRxBuffer[index]);
                }
            }
            vTaskDelayUntil(&xLastWakeTime, 2);
        } else {
            vTaskDelayUntil(&xLastWakeTime, 10);
        }
    }

    sempStopParser(&btParser);
    task.bluetoothReadTaskRunning = false;
    if (settings.printTaskStartStop) {
        systemPrintln("Task: btReadTask stopped");
    }
    btReadTaskHandle = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

namespace HAL {
void
bluetoothInit() {
    bluetoothStart();
    if (!online_devices.bluetooth) {
        ESP_LOGE(TAG, "Bluetooth not enabled");
        return;
    }

    if (btReadTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(btReadTask, "btReadTask", kBluetoothReadTaskStack, nullptr, settings.btReadTaskPriority,
                                &btReadTaskHandle, settings.btReadTaskCore);
        ESP_LOGI(TAG, "Bluetooth read task created on core %d", settings.btReadTaskCore);
    }
}
} // namespace HAL
