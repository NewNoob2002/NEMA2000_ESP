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

// constexpr uint8_t kMsgSenderApp = 0x00;
constexpr uint8_t kMsgSenderDevice = 0x01;
// constexpr uint8_t kMsgSenderPannel = 0x02;

constexpr uint8_t kMsgQueryType = 0x00;
constexpr uint8_t kMsgQueryRespType = 0x01;
constexpr uint8_t kMsgSetType = 0x02;
constexpr uint8_t kMsgSetRespType = 0x03;
constexpr uint8_t kResponseError = 0x05;

constexpr size_t kBluetoothRxBufferSize = 512;
constexpr size_t kBluetoothMaxPayload = 256;
constexpr size_t kBluetoothTxBufferSize = sizeof(SEMP_CUSTOM_HEADER) + kBluetoothMaxPayload + sizeof(uint32_t);
constexpr uint32_t kBluetoothParserBufferSize = 1024 * 2;
constexpr uint32_t kBluetoothReadTaskStack = 3072;

enum BluetoothParserType : uint16_t {
    BluetoothAPPType = 0,
    BluetoothRTCMType = 1,
};

const SEMP_PARSE_ROUTINE kBluetoothParserTable[] = {
    sempCustomPreamble,
    sempRtcmPreamble,
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
    bool allocated = false;
};

bool
allocateResponse(BluetoothResponse& response, const SEMP_CUSTOM_HEADER& requestHeader, const uint16_t payloadLength) {
    if (payloadLength > kBluetoothMaxPayload) {
        systemPrintf("Bluetooth response too large: %u\r\n", payloadLength);
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
    const uint32_t crc = computeBluetoothCrc(messageTxBuffer, frameLength);
    writeLe32(messageTxBuffer + frameLength, crc);
    bluetoothWrite(messageTxBuffer, frameLength + sizeof(uint32_t));
#if 0
    systemPrintf("Send Res: ");
    for (int i = 0; i < sizeof(SEMP_CUSTOM_HEADER) + response.payloadLength + 4; i++) {
        systemPrintf("0x%02x ", messageTxBuffer[i]);
    }
    systemPrintln();
#endif
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
    copyFixedString(response.payload + 56, 8, gUm980Present && HAL::gUm980->getModelType() == 18 ? "UM980" : "Unknown");
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
    response.payload[4] = 0x00;                                          // Radio
    response.payload[5] = 0x00;                                          // 4G/5G
    response.payload[9] = 0x81;                                          // Battery
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
        const bool baseMode = HAL::gUm980 && (HAL::gUm980->getMode() == Um980Mode::Base);
        response.payload[0] = baseMode ? 0x01 : 0x00;
        response.payload[1] = settings.fixedBase ? 0x01 : 0x00;
        response.payload[2] = baseMode ? 0x01 : 0x00;
        std::memcpy(response.payload + 4, &settings.baseId, sizeof(settings.baseId));
        std::memcpy(response.payload + 12, &settings.fixedLong, sizeof(settings.fixedLong));
        response.payload[20] = (settings.fixedLong >= 0.0) ? 'E' : 'W';
        std::memcpy(response.payload + 24, &settings.fixedLat, sizeof(settings.fixedLat));
        response.payload[32] = (settings.fixedLat >= 0.0) ? 'N' : 'S';
        std::memcpy(response.payload + 36, &settings.fixedAltitude, sizeof(settings.fixedAltitude));
        return;
    } else if (requestHeader.messageType == kMsgSetType) {
        bool valid = payload && (payloadLength >= 1);
        if (valid && HAL::gUm980) {
            uint8_t RTK_MODE_SET = payload[0];
            uint8_t FixedBase = payload[1];
            uint8_t BaseEnable = payload[2];
            double lon, lat, alt;
            std::memcpy(&lon, &payload[12], 8);
            std::memcpy(&lat, &payload[24], 8);
            std::memcpy(&alt, &payload[36], 8);
            systemPrintf("RTK_MODE:%d, FixedBase:%d, BaseEnable:%d, lon:%0.6f, lat:%0.6f, alt:%0.6f", RTK_MODE_SET,
                         FixedBase, BaseEnable, lon, lat, alt);
        }
        ack(response, requestHeader, 1);
    }
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
            changeState(STATE_WEB_CONFIG_NOT_STARTED);
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
        systemPrintf("Bluetooth APP frame too short: %u\r\n", parse->length);
        return;
    }

    const uint16_t messageId = getMessageId(*requestHeader);
    const uint16_t payloadLength = requestHeader->messageLength;
    const uint8_t* payload = parse->buffer + sizeof(SEMP_CUSTOM_HEADER);
    if (payloadLength == 0) {
        payload = nullptr;
    }

    BluetoothResponse response;
    response.messageId = messageId;
    if (requestHeader->messageType == kMsgQueryType) {
        response.messageType = kMsgQueryRespType;
    } else if (requestHeader->messageType == kMsgSetType) {
        response.messageType = kMsgSetRespType;
    } else {
        response.messageType = kMsgSetRespType;
    }
    response.msgInterval = requestHeader->MsgInterval;

    // systemPrintf("[Bluetooth]Rev 0x%02x, type 0x%02x, length: %d :", messageId, response.messageType, payloadLength);
    // for (int i = 0; i < payloadLength; i++) {
    //     systemPrintf("0x%02x ", parse->buffer[sizeof(SEMP_CUSTOM_HEADER) + i]);
    // }
    // systemPrintln();

    switch (messageId) {
        case 0x01: handleModelQuery(response, *requestHeader); break;
        case 0x02: handleDeviceStatusQuery(response, *requestHeader); break;
        case 0x03: handleBatteryStorageQuery(response, *requestHeader); break;
        case 0x04: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGGA");
        } break;
        case 0x05: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGSV");
        } break;
        case 0x06: handleSatelliteTracking(response, *requestHeader, payload, payloadLength); break;
        case 0x07: handleWorkMode(response, *requestHeader, payload, payloadLength); break;
        case 0x0D: handleComConfig(response, *requestHeader, payload, payloadLength); break;
        case 0x14: handleRadioConfig(response, *requestHeader); break;
        case 0x15: handleLogConfig(response, *requestHeader, payload, payloadLength); break;
        case 0x21: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGST");
        } break;
        case 0x22: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPRMC");
        } break;
        case 0x23: {
            response.messageType = kMsgSetRespType;
            handleGnssMessageToggle(response, *requestHeader, "GPGSA");
        } break;
        case 0x24: handleImuAntennaHeight(response, *requestHeader, payload, payloadLength); break;
        case 0x25: {
            response.messageType = kMsgSetRespType;
            handleImuData(response, *requestHeader, payload, payloadLength);
        } break;
        case 0x26: handleTiltCompensation(response, *requestHeader, payload, payloadLength); break;
        case 0x27: handlePhoneNetworkConfig(response, *requestHeader, payload, payloadLength); break;
        case 0x28: handleWifiControl(response, *requestHeader, payload, payloadLength); break;
        case 0x29: {
            response.messageType = kMsgSetRespType;
            handleBASEINFOPacket(response, *requestHeader, payload, payloadLength);
        } break;
        case 0x30: handleRegister(response, *requestHeader); break;
        case 0x31: handleShutdown(response, *requestHeader, payload, payloadLength); break;
        case 0x32: handleLogAntennaHeight(response, *requestHeader, payload, payloadLength); break;
        case 0x33: {
            response.messageType = kMsgSetRespType;
            handlePppControl(response, *requestHeader, payload, payloadLength);
        } break;
        case 0x34: ack(response, *requestHeader, 0x01); break;
        case 0x36: ack(response, *requestHeader, 0x01); break;
        default:
            systemPrintf("Unknown Bluetooth message: id=0x%02x type=0x%02x\r\n", messageId, requestHeader->messageType);
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
        HAL::gnssSerial->write(parse->buffer, parse->length);
    } else {
        systemPrintf("Dropped RTCM%u: GNSS serial not ready\r\n", sempRtcmGetMessageNumber(parse));
    }
}

void
btDataProcess(SEMP_PARSE_STATE* parse, uint16_t type) {
    if (type == BluetoothRTCMType) {
        processRtcmMessage(parse);
    } else if (type == BluetoothAPPType) {
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
        systemPrintf("Failed to initialize the Bluetooth parser\r\n");
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
        systemPrintf("Bluetooth not enabled\r\n");
        return;
    }

    if (btReadTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(btReadTask, "btReadTask", kBluetoothReadTaskStack, nullptr, settings.btReadTaskPriority,
                                &btReadTaskHandle, settings.btReadTaskCore);
        systemPrintf("Bluetooth read task created on core %d\r\n", settings.btReadTaskCore);
    }
}
} // namespace HAL
