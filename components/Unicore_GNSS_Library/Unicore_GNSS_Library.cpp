#include "Unicore_GNSS_Library.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp32-hal.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace {
constexpr uint16_t kParserBufferBytes = 1024;
constexpr uint16_t kParserScratchBytes = 0;

enum ParserIndex : uint16_t {
    kParserNmea = 0,
    kParserRtcm,
    kParserUnicoreBinary,
    kParserUnicoreHash,
};

const SEMP_PARSE_ROUTINE kParserTable[] = {
    sempNmeaPreamble,
    sempRtcmPreamble,
    sempUnicoreBinaryPreamble,
    sempUnicoreHashPreamble,
};

const char* const kParserNames[] = {
    "NMEA",
    "RTCM",
    "UnicoreBinary",
    "UnicoreHash",
};

uint8_t
readU1(const uint8_t* data, const uint16_t offset) {
    return data[offset];
}

uint16_t
readU2(const uint8_t* data, const uint16_t offset) {
    uint16_t value = 0;
    memcpy(&value, &data[offset], sizeof(value));
    return value;
}

uint32_t
readU4(const uint8_t* data, const uint16_t offset) {
    uint32_t value = 0;
    memcpy(&value, &data[offset], sizeof(value));
    return value;
}

float
readF4(const uint8_t* data, const uint16_t offset) {
    float value = 0.0f;
    memcpy(&value, &data[offset], sizeof(value));
    return value;
}

double
readF8(const uint8_t* data, const uint16_t offset) {
    double value = 0.0;
    memcpy(&value, &data[offset], sizeof(value));
    return value;
}

void
copyFixedString(char* destination, const size_t destinationSize, const uint8_t* source, const uint16_t offset,
                const uint16_t available, const uint16_t maxBytes) {
    if (!destination || (destinationSize == 0)) {
        return;
    }

    destination[0] = 0;
    if (offset >= available) {
        return;
    }

    size_t bytes = available - offset;
    if (bytes > maxBytes) {
        bytes = maxBytes;
    }
    if (bytes >= destinationSize) {
        bytes = destinationSize - 1;
    }

    memcpy(destination, &source[offset], bytes);
    destination[bytes] = 0;

    for (int index = static_cast<int>(bytes) - 1; index >= 0; index--) {
        const char value = destination[index];
        if ((value == 0) || std::isspace(static_cast<unsigned char>(value))) {
            destination[index] = 0;
        } else {
            break;
        }
    }
}

bool
equalsIgnoreCase(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) {
        return false;
    }

    while (*lhs && *rhs) {
        if (std::toupper(static_cast<unsigned char>(*lhs)) != std::toupper(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        lhs++;
        rhs++;
    }

    return (*lhs == 0) && (*rhs == 0);
}

bool
containsIgnoreCase(const char* text, const char* needle) {
    if (!text || !needle || (*needle == 0)) {
        return false;
    }

    const size_t needleLength = strlen(needle);
    for (const char* cursor = text; *cursor; cursor++) {
        size_t index = 0;
        while ((index < needleLength) && cursor[index]
               && (std::toupper(static_cast<unsigned char>(cursor[index]))
                   == std::toupper(static_cast<unsigned char>(needle[index])))) {
            index++;
        }
        if (index == needleLength) {
            return true;
        }
    }

    return false;
}

bool
startsWithIgnoreCase(const char* text, const char* prefix) {
    if (!text || !prefix) {
        return false;
    }

    while (*prefix) {
        if (!*text
            || (std::toupper(static_cast<unsigned char>(*text)) != std::toupper(static_cast<unsigned char>(*prefix)))) {
            return false;
        }
        text++;
        prefix++;
    }
    return true;
}

bool
copyCommandToken(char* destination, const size_t destinationSize, const char*& cursor) {
    if (!destination || (destinationSize == 0) || !cursor) {
        return false;
    }

    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) {
        cursor++;
    }

    size_t index = 0;
    while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && (*cursor != ',') && (*cursor != '*')) {
        if (index < (destinationSize - 1)) {
            destination[index++] = *cursor;
        }
        cursor++;
    }
    destination[index] = 0;
    return index > 0;
}

bool
commandAckMatchesPendingCommand(const char* sentence, const char* pendingCommand) {
    if (!sentence || !pendingCommand || !startsWithIgnoreCase(sentence, "$command,")) {
        return false;
    }

    const char* commandCursor = pendingCommand;
    char commandToken[24] = {};
    if (!copyCommandToken(commandToken, sizeof(commandToken), commandCursor)) {
        return false;
    }

    if ((equalsIgnoreCase(commandToken, "COM1") || equalsIgnoreCase(commandToken, "COM2")
         || equalsIgnoreCase(commandToken, "COM3") || equalsIgnoreCase(commandToken, "USB"))
        && !copyCommandToken(commandToken, sizeof(commandToken), commandCursor)) {
        return false;
    }

    const char* responseCursor = sentence + strlen("$command,");
    char responseToken[24] = {};
    if (!copyCommandToken(responseToken, sizeof(responseToken), responseCursor)) {
        return false;
    }

    return equalsIgnoreCase(commandToken, responseToken);
}

/**
 * @brief Extracts a delimited field from a text string, such as a CSV or NMEA sentence. The output is always null-terminated.
 * 
 * @param text 
 * @param fieldIndex 
 * @param output 
 * @param outputSize 
 * @param delimiter 
 * @return true 
 * @return false 
 */
bool
getCsvField(const char* text, const uint8_t fieldIndex, char* output, const size_t outputSize,
            const char delimiter = ',') {
    if (!text || !output || (outputSize == 0)) {
        return false;
    }

    output[0] = 0;
    uint8_t currentField = 0;
    const char* start = text;

    while (*start && (currentField < fieldIndex)) {
        if (*start == delimiter) {
            currentField++;
        }
        start++;
    }

    if (currentField != fieldIndex) {
        return false;
    }

    const char* end = start;
    while (*end && (*end != delimiter) && (*end != '*') && (*end != '\r') && (*end != '\n')) {
        end++;
    }

    size_t bytes = static_cast<size_t>(end - start);
    if (bytes >= outputSize) {
        bytes = outputSize - 1;
    }
    memcpy(output, start, bytes);
    output[bytes] = 0;
    return true;
}

void
copyToken(char* destination, const size_t destinationSize, const char* source) {
    if (!destination || (destinationSize == 0)) {
        return;
    }

    destination[0] = 0;
    if (!source) {
        return;
    }

    size_t bytes = 0;
    while (source[bytes] && (source[bytes] != ',') && (source[bytes] != '*') && (source[bytes] != '\r')
           && (source[bytes] != '\n') && (bytes < (destinationSize - 1))) {
        destination[bytes] = source[bytes];
        bytes++;
    }
    destination[bytes] = 0;
}

const char*
binaryMessageName(const uint16_t messageId) {
    switch (messageId) {
        case messageIdBestnav: return "BESTNAVB";
        case messageIdBestnavXyz: return "BESTNAVXYZB";
        case messageIdRectime: return "RECTIMEB";
        case messageIdVersion: return "VERSIONB";
        default: return "";
    }
}

void
espLogCallback(const UnicoreLogLevel level, const uint32_t mask, const char* message, void* userdata) {
    (void)mask;
    (void)userdata;

    static constexpr const char* kTag = "[UnicoreLibrary]";
    switch (level) {
        case UnicoreLogLevel::Error: ESP_LOGE(kTag, "%s", message); break;
        case UnicoreLogLevel::Warn: ESP_LOGW(kTag, "%s", message); break;
        case UnicoreLogLevel::Info: ESP_LOGI(kTag, "%s", message); break;
        case UnicoreLogLevel::Debug: ESP_LOGD(kTag, "%s", message); break;
        case UnicoreLogLevel::Verbose: ESP_LOGV(kTag, "%s", message); break;
        case UnicoreLogLevel::Off:
        default: break;
    }
}

const char*
resultName(const UnicoreResult_t result) {
    switch (result) {
        case Unicore_RESULT_SEND_COMMAND_OK: return "SEND_COMMAND_OK";
        case Unicore_RESULT_TIMEOUT_START_BYTE: return "TIMEOUT_START_BYTE";
        case Unicore_RESULT_TIMEOUT_DATA_BYTE: return "TIMEOUT_DATA_BYTE";
        case Unicore_RESULT_TIMEOUT_END_BYTE: return "TIMEOUT_END_BYTE";
        case Unicore_RESULT_TIMEOUT_RESPONSE: return "TIMEOUT_RESPONSE";
        case Unicore_RESULT_WRONG_COMMAND: return "WRONG_COMMAND";
        case Unicore_RESULT_WRONG_MESSAGE_ID: return "WRONG_MESSAGE_ID";
        case Unicore_RESULT_BAD_START_BYTE: return "BAD_START_BYTE";
        case Unicore_RESULT_BAD_CHECKSUM: return "BAD_CHECKSUM";
        case Unicore_RESULT_BAD_CRC: return "BAD_CRC";
        case Unicore_RESULT_MISSING_CRC: return "MISSING_CRC";
        case Unicore_RESULT_TIMEOUT: return "TIMEOUT";
        case Unicore_RESULT_RESPONSE_OVERFLOW: return "RESPONSE_OVERFLOW";
        case Unicore_RESULT_RESPONSE_COMMAND_OK: return "COMMAND_RESPONSE_OK";
        case Unicore_RESULT_RESPONSE_COMMAND_ERROR: return "COMMAND_RESPONSE_ERROR";
        case Unicore_RESULT_RESPONSE_COMMAND_WAITING: return "COMMAND_RESPONSE_WAITING";
        case Unicore_RESULT_RESPONSE_COMMAND_CONFIG: return "COMMAND_RESPONSE_CONFIG";
        case Unicore_RESULT_CONFIG_PRESENT: return "CONFIG_PRESENT";
        default: return "UNKNOWN";
    }
}
} // namespace

UnicoreGNSSLibrary* UnicoreGNSSLibrary::_activeInstance = nullptr;

UnicoreGNSSLibrary::UnicoreGNSSLibrary() = default;

UnicoreGNSSLibrary::~UnicoreGNSSLibrary() { end(); }

bool
UnicoreGNSSLibrary::begin(HardwareSerial& serialPort, uint16_t rxBufferSize) {
    UnicoreLogCallback configuredLogCallback = _logCallback;
    void* configuredLogCallbackUserdata = _logCallbackUserdata;
    end();

    if (_activeInstance && (_activeInstance != this)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "begin failed: another Unicore instance is active");
        return false;
    }

    _hwSerialPort = &serialPort;
    _rxBufferSize = rxBufferSize;
    _logCallback = configuredLogCallback;
    _logCallbackUserdata = configuredLogCallbackUserdata;
    _activeInstance = this;

    _rxBuffer = static_cast<uint8_t*>(malloc(_rxBufferSize));

    _sempParse = sempBeginParser(kParserTable, sizeof(kParserTable) / sizeof(kParserTable[0]), kParserNames,
                                 sizeof(kParserNames) / sizeof(kParserNames[0]), kParserScratchBytes,
                                 kParserBufferBytes, parserEomCallback, "UnicoreGNSS", parserErrorPrintf,
                                 parserDebugPrintf, parserBadChecksumCallback);

    if (!_sempParse || !_rxBuffer) {
        _activeInstance = nullptr;
        _hwSerialPort = nullptr;
        log(UnicoreLogLevel::Error, UNICORE_LOG_PARSER, "begin failed: parser allocation failed");
        end();
        return false;
    }

    if (!startRxTask(1024 * 5, configMAX_PRIORITIES - 6)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "begin failed: RX task failed");
        end();
        return false;
    }

    _commandStateMutex = xSemaphoreCreateMutex();
    _commandDoneSemaphore = xSemaphoreCreateBinary();

    if (!_commandStateMutex || !_commandDoneSemaphore) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "begin failed: command synchronization allocation failed");
        end();
        return false;
    }

    if (isOnline() != true) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "begin failed: module not responding");
        end();
        return false;
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_TASK, "begin ok");
    return true;
}

void
UnicoreGNSSLibrary::rxTaskEntry(void* context) {
    auto* instance = static_cast<UnicoreGNSSLibrary*>(context);
    if (instance) {
        instance->rxTask();
    }
    vTaskDelete(nullptr);
}

void
UnicoreGNSSLibrary::end() {
    stopRxTask();

    if (_sempParse) {
        sempStopParser(&_sempParse);
    }

    if (_rxBuffer) {
        free(_rxBuffer);
        _rxBuffer = nullptr;
    }

    if (_commandStateMutex) {
        vSemaphoreDelete(_commandStateMutex);
        _commandStateMutex = nullptr;
    }
    if (_commandDoneSemaphore) {
        vSemaphoreDelete(_commandDoneSemaphore);
        _commandDoneSemaphore = nullptr;
    }

    if (_activeInstance == this) {
        _activeInstance = nullptr;
    }

    _hwSerialPort = nullptr;
}

bool
UnicoreGNSSLibrary::isConnected() const {
    return (_hwSerialPort != nullptr) && (_sempParse != nullptr);
}

bool
UnicoreGNSSLibrary::isOnline() {
    bool ret = false;
    for (int x = 0; x < 3; x++) {
        disableOutput(); // send UNLOG to elicit a response from the module

        if (sendCommandAndWait("MODE", 2000, "MODE") == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            ret = true;
            break;
        }
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "isOnline failed: Device not responding to MODE command, try %d",
            x);
        delay(100 * (x + 1));
    }
    return ret;
}

bool
UnicoreGNSSLibrary::isNmeaFixed() const {
    if (nmeaPositionStatus >= 1) {
        return (true);
    }
    return (false);
}

bool
UnicoreGNSSLibrary::disableOutput() {
    for (int x = 0; x < 3; x++) {
        if (unlogPort() == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            return true;
        }
        delay(10 * (x + 1));
    }
    return false;
}

bool
UnicoreGNSSLibrary::startRxTask(const uint32_t stackSize, const UBaseType_t priority) {
    if (!isConnected()) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "RX task start failed: not connected");
        return false;
    }
    if (_rxTaskHandle || _rxTaskRunning) {
        log(UnicoreLogLevel::Debug, UNICORE_LOG_TASK, "RX task already running");
        return true;
    }

    _rxTaskShouldRun = true;
    BaseType_t result = pdFAIL;
    result = xTaskCreatePinnedToCore(rxTaskEntry, "unicore_rx", stackSize, this, priority, &_rxTaskHandle, 1);

    if (result != pdPASS) {
        _rxTaskHandle = nullptr;
        _rxTaskShouldRun = false;
        log(UnicoreLogLevel::Error, UNICORE_LOG_TASK, "RX task start failed");
        return false;
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_TASK, "RX task start requested stack=%lu priority=%lu core=1",
        static_cast<unsigned long>(stackSize), static_cast<unsigned long>(priority));
    return true;
}

void
UnicoreGNSSLibrary::stopRxTask(const uint32_t timeoutMs) {
    if (!_rxTaskHandle && !_rxTaskRunning) {
        _rxTaskShouldRun = false;
        return;
    }

    _rxTaskShouldRun = false;
    const uint32_t startMs = millis();
    while (_rxTaskRunning && ((millis() - startMs) < timeoutMs)) {
        delay(1);
    }

    if (_rxTaskHandle) {
        vTaskDelete(_rxTaskHandle);
        _rxTaskHandle = nullptr;
        _rxTaskRunning = false;
    }
    log(UnicoreLogLevel::Info, UNICORE_LOG_TASK, "RX task stopped");
}

bool
UnicoreGNSSLibrary::isRxTaskRunning() const {
    return _rxTaskRunning;
}

size_t
UnicoreGNSSLibrary::poll() {
    if (!isConnected()) {
        return 0;
    }

    size_t parsed = 0;
    while (_hwSerialPort->available() > 0) {
        int bytesIncoming = _hwSerialPort->readBytes(_rxBuffer, _rxBufferSize);
        if (bytesIncoming <= 0) {
            break;
        }
        for (int i = 0; i < bytesIncoming; i++) {
            sempParseNextByte(_sempParse, _rxBuffer[i]);
        }
        parsed += bytesIncoming;
    }

    return parsed;
}

size_t
UnicoreGNSSLibrary::available() const {
    if (!_hwSerialPort) {
        return 0;
    }
    const int count = _hwSerialPort->available();
    return (count > 0) ? static_cast<size_t>(count) : 0;
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommand(const char* command) {
    if (!isConnected() || !command || (command[0] == 0)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_TX | UNICORE_LOG_COMMAND, "TX command rejected");
        return Unicore_RESULT_WRONG_COMMAND;
    }
    log(UnicoreLogLevel::Info, UNICORE_LOG_TX | UNICORE_LOG_COMMAND, "TX [%s]", command);
    _hwSerialPort->println(command);
    return Unicore_RESULT_SEND_COMMAND_OK;
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommand(const String& command) {
    return sendCommand(command.c_str());
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommandAsync(const char* command, const char* expectedResponse) {
    if (!isConnected() || !command || (command[0] == 0)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "\n||async command rejected");
        return Unicore_RESULT_WRONG_COMMAND;
    }

    if (_commandStateMutex && (xSemaphoreTake(_commandStateMutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_COMMAND, "\n||async command lock timeout: %s", command);
        return Unicore_RESULT_TIMEOUT;
    }

    if (_commandPending) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_COMMAND, "\n||async command busy: pending=%s rejected=%s",
            _pendingCommand, command);
        if (_commandStateMutex) {
            xSemaphoreGive(_commandStateMutex);
        }
        return Unicore_RESULT_RESPONSE_COMMAND_WAITING;
    }

    clearCommandResult();
    strncpy(_pendingCommand, command, sizeof(_pendingCommand) - 1);
    _pendingCommand[sizeof(_pendingCommand) - 1] = 0;
    _pendingExpectedResponse[0] = 0;
    if (expectedResponse && (expectedResponse[0] != 0)) {
        strncpy(_pendingExpectedResponse, expectedResponse, sizeof(_pendingExpectedResponse) - 1);
        _pendingExpectedResponse[sizeof(_pendingExpectedResponse) - 1] = 0;
    }
    log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "||async command pending=%s expected=%s", _pendingCommand,
        _pendingExpectedResponse[0] ? _pendingExpectedResponse : "<auto>");
    while (_commandDoneSemaphore && (xSemaphoreTake(_commandDoneSemaphore, 0) == pdTRUE)) {}
    _commandPending = true;

    if (_commandStateMutex) {
        xSemaphoreGive(_commandStateMutex);
    }

    const UnicoreResult_t result = sendCommand(command);
    if (result != Unicore_RESULT_SEND_COMMAND_OK) {
        completePendingCommand(result);
    }
    return result;
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommandAsync(const String& command, const char* expectedResponse) {
    return sendCommandAsync(command.c_str(), expectedResponse);
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommandAndWait(const char* command, const uint32_t timeoutMs, const char* expectedResponse) {
    const UnicoreResult_t result = sendCommandAsync(command, expectedResponse);
    if (result != Unicore_RESULT_SEND_COMMAND_OK) {
        return result;
    }

    return waitForCommandResponse(timeoutMs);
}

UnicoreResult_t
UnicoreGNSSLibrary::sendCommandAndWait(const String& command, const uint32_t timeoutMs, const char* expectedResponse) {
    return sendCommandAndWait(command.c_str(), timeoutMs, expectedResponse);
}

bool
UnicoreGNSSLibrary::isCommandPending() const {
    return _commandPending;
}

UnicoreResult_t
UnicoreGNSSLibrary::getLastCommandResult() const {
    return _lastCommandResult;
}

UnicoreResult_t
UnicoreGNSSLibrary::requestMessage(const char* messageName, const uint32_t timeoutMs) {
    if (!messageName) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    if (equalsIgnoreCase(messageName, "VERSION")) {
        return sendCommandAndWait(messageName, timeoutMs, "#VERSION");
    }

    return sendCommandAndWait(messageName, timeoutMs, messageName);
}

UnicoreResult_t
UnicoreGNSSLibrary::logMessage(const char* messageName, const UnicorePort port, const UnicoreLogTrigger trigger,
                               const float periodSeconds) {
    if (!messageName || (messageName[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    char command[96] = {};
    const char* portText = portName(port);

    if (trigger == UnicoreLogTrigger::Once) {
        if (port == UnicorePort::Current) {
            snprintf(command, sizeof(command), "%s", messageName);
        } else {
            snprintf(command, sizeof(command), "%s %s", portText, messageName);
        }
    } else {
        if (port == UnicorePort::Current) {
            snprintf(command, sizeof(command), "%s %0.2f", messageName, periodSeconds);
        } else {
            snprintf(command, sizeof(command), "%s %s %0.2f", messageName, portText, periodSeconds);
        }
    }

    return sendCommandAndWait(command);
}

UnicoreResult_t
UnicoreGNSSLibrary::unlogMessage(const char* messageName, const UnicorePort port) {
    if (!messageName || (messageName[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    char command[64] = {};
    if (port == UnicorePort::Current) {
        snprintf(command, sizeof(command), "UNLOG %s", messageName);
    } else {
        snprintf(command, sizeof(command), "UNLOG %s %s", portName(port), messageName);
    }
    return sendCommandAndWait(command);
}

UnicoreResult_t
UnicoreGNSSLibrary::unlogPort(const UnicorePort port) {
    char command[32] = {};
    if (port == UnicorePort::Current) {
        snprintf(command, sizeof(command), "UNLOG");
    } else {
        snprintf(command, sizeof(command), "UNLOG %s", portName(port));
    }
    return sendCommandAndWait(command, 1000, "UNLOG");
}

UnicoreResult_t
UnicoreGNSSLibrary::enableSystem(const char* systemName) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!systemName || (systemName[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif // UNICORE_NULLPTR_CHECK

    char command[32] = {};
    snprintf(command, sizeof(command), "UNMASK %s", systemName);

    return sendCommandAndWait(command, 2000, "UNMASK");
}

UnicoreResult_t
UnicoreGNSSLibrary::disableSystem(const char* systemName) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!systemName || (systemName[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }
#endif // UNICORE_NULLPTR_CHECK

    char command[32] = {};
    snprintf(command, sizeof(command), "MASK %s", systemName);

    return sendCommandAndWait(command, 2000, "MASK");
}

UnicoreResult_t
UnicoreGNSSLibrary::setPortBaudrate(UnicorePort port, uint32_t baudrate) {
    if (baudrate == 0) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    char command[32] = {};
    snprintf(command, sizeof(command), "CONFIG %s %ld", portName(port), static_cast<unsigned long>(baudrate));
    return sendCommandAndWait(command, 2000, "CONFIG");
}

UnicoreResult_t
UnicoreGNSSLibrary::queryConfigContains(const char* configText, const uint32_t timeoutMs, const uint32_t quietMs) {
    if (!configText || (configText[0] == 0)) {
        return Unicore_RESULT_WRONG_COMMAND;
    }

    strncpy(_configQueryText, configText, sizeof(_configQueryText) - 1);
    _configQueryText[sizeof(_configQueryText) - 1] = 0;
    _configQueryMatched = false;
    _lastConfigSentenceMs = 0;
    _configQueryActive = true;

    const uint32_t startMs = millis();
    const UnicoreResult_t commandResult = sendCommandAndWait("CONFIG", timeoutMs, configText);
    if (commandResult != Unicore_RESULT_RESPONSE_COMMAND_OK) {
        _configQueryActive = false;
        return commandResult;
    }

    while ((millis() - startMs) < timeoutMs) {
        if (!isRxTaskRunning()) {
            poll();
        }

        const uint32_t lastConfigMs = _lastConfigSentenceMs;
        if ((lastConfigMs != 0) && ((millis() - lastConfigMs) >= quietMs)) {
            break;
        }

        delay(1);
    }

    _configQueryActive = false;
    return _configQueryMatched ? Unicore_RESULT_CONFIG_PRESENT : Unicore_RESULT_RESPONSE_COMMAND_CONFIG;
}

UnicoreResult_t
UnicoreGNSSLibrary::saveConfiguration() {
    return sendCommandAndWait("SAVECONFIG", 2000, "SAVECONFIG");
}

UnicoreResult_t
UnicoreGNSSLibrary::factoryReset() {
    return sendCommandAndWait("FRESET", 3000);
}

void
UnicoreGNSSLibrary::setNmeaCallback(const UnicoreNmeaCallback callback, void* userdata) {
    _nmeaCallback = callback;
    _nmeaCallbackUserdata = callback ? userdata : nullptr;
}

void
UnicoreGNSSLibrary::setRtcmCallback(const UnicoreRtcmCallback callback, void* userdata) {
    _rtcmCallback = callback;
    _rtcmCallbackUserdata = callback ? userdata : nullptr;
}

void
UnicoreGNSSLibrary::setBinaryCallback(const UnicoreBinaryCallback callback, void* userdata) {
    _binaryCallback = callback;
    _binaryCallbackUserdata = callback ? userdata : nullptr;
}

void
UnicoreGNSSLibrary::setHashCallback(UnicoreHashCallback callback, void* context) {
    _hashCallback = callback;
    _hashCallbackUserdata = callback ? context : nullptr;
}

void
UnicoreGNSSLibrary::enableBinaryBeforeFix() {
    startBinaryBeforeFix = true;
}

void
UnicoreGNSSLibrary::disableBinaryBeforeFix() {
    startBinaryBeforeFix = false;
}

void
UnicoreGNSSLibrary::setLogCallback(UnicoreLogCallback callback, void* context) {
    _logCallback = callback;
    _logCallbackUserdata = callback ? context : nullptr;
}

void
UnicoreGNSSLibrary::setLogLevel(const UnicoreLogLevel level) {
    _logLevel = level;
}

void
UnicoreGNSSLibrary::setLogMask(const uint32_t mask) {
    _logMask = mask;
}

void
UnicoreGNSSLibrary::enableLogCategory(const uint32_t mask) {
    _logMask |= mask;
}

void
UnicoreGNSSLibrary::disableLogCategory(const uint32_t mask) {
    _logMask &= ~mask;
}

void
UnicoreGNSSLibrary::enableDebugLogging(const UnicoreLogLevel level, const uint32_t mask) {
    _logCallback = espLogCallback;
    _logCallbackUserdata = nullptr;
    _logLevel = level;
    _logMask = mask;
}

void
UnicoreGNSSLibrary::disableDebugLogging() {
    _logCallback = nullptr;
    _logCallbackUserdata = nullptr;
    _logLevel = UnicoreLogLevel::Off;
    _logMask = UNICORE_LOG_NONE;
}

UnicoreLogLevel
UnicoreGNSSLibrary::getLogLevel() const {
    return _logLevel;
}

uint32_t
UnicoreGNSSLibrary::getLogMask() const {
    return _logMask;
}

bool
UnicoreGNSSLibrary::isLogEnabled(const UnicoreLogLevel level, const uint32_t mask) const {
    return _logCallback && (level != UnicoreLogLevel::Off)
           && (static_cast<uint8_t>(level) <= static_cast<uint8_t>(_logLevel)) && ((_logMask & mask) != 0);
}

const char*
UnicoreGNSSLibrary::portName(const UnicorePort port) {
    switch (port) {
        case UnicorePort::Com1: return "COM1";
        case UnicorePort::Com2: return "COM2";
        case UnicorePort::Com3: return "COM3";
        case UnicorePort::Usb: return "USB";
        case UnicorePort::Current:
        default: return "";
    }
}

const char*
UnicoreGNSSLibrary::triggerName(const UnicoreLogTrigger trigger) {
    switch (trigger) {
        case UnicoreLogTrigger::Once: return "ONCE";
        case UnicoreLogTrigger::OnChanged: return "ONCHANGED";
        case UnicoreLogTrigger::OnTime:
        default: return "ONTIME";
    }
}

const UnicoreBinaryHeader&
UnicoreGNSSLibrary::getLastBinaryHeader() const {
    return _lastBinaryHeader;
}

uint32_t
UnicoreGNSSLibrary::getLastBestNavMs() const {
    return lastUpdateGeodetic;
}

uint32_t
UnicoreGNSSLibrary::getLastBestNavXyzMs() const {
    return lastUpdateEcef;
}

uint32_t
UnicoreGNSSLibrary::getLastRecTimeMs() const {
    return lastUpdateDateTime;
}

uint32_t
UnicoreGNSSLibrary::getLastVersionMs() const {
    return lastUpdateVersion;
}

void
UnicoreGNSSLibrary::stopAutoReports() {
    if (_bestNav != nullptr) {
        delete _bestNav;
        _bestNav = nullptr;
    }
    if (_bestNavXyz != nullptr) {
        delete _bestNavXyz;
        _bestNavXyz = nullptr;
    }
    if (_recTime != nullptr) {
        delete _recTime;
        _recTime = nullptr;
    }
}

bool
UnicoreGNSSLibrary::initBestnav(float rate) {
    if ((startBinaryBeforeFix == false) && (isNmeaFixed() == false)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "bestnav init delayed until fix");
        return (false);
    }

    _bestNav = new UNICORE_BESTNAV_data_t;
    if (_bestNav == nullptr) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "failed to allocate bestnav data");
        return (false);
    }

    // Start outputting BESTNAV in Binary on this COM port
    char command[50];
    snprintf(command, sizeof(command), "BESTNAVB %0.2f", rate);
    if (sendCommandAndWait(command, 2000) != Unicore_RESULT_RESPONSE_COMMAND_OK) {
        delete _bestNav;
        _bestNav = nullptr; // Remove pointer so we will re-init next check
        return (false);
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_COMMAND, "bestnav init ok");
    lastUpdateGeodetic = 0;
    uint16_t maxWait = (1000 / rate) + 100; // Wait for one response to come in
    unsigned long startTime = millis();

    while (1) {
        if (lastUpdateGeodetic > 0) {
            break;
        }
        if (millis() - startTime > maxWait) {
            log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "Failed to get response from BestNav start");
            delete _bestNav;
            _bestNav = nullptr;
            return (false);
        }
        delay(10);
    }
    return true;
}

bool
UnicoreGNSSLibrary::initBestnavXyz(float rate) {
    if ((startBinaryBeforeFix == false) && (isNmeaFixed() == false)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "bestnavxyz init delayed until fix");
        return (false);
    }

    _bestNavXyz = new UNICORE_BESTNAVXYZ_data_t;
    if (_bestNavXyz == nullptr) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "failed to allocate bestnavxyz data");
        return (false);
    }

    // Start outputting BESTNAV_XYZ in Binary on this COM port
    char command[50];
    snprintf(command, sizeof(command), "BESTNAVXYZB %0.2f", rate);
    if (sendCommandAndWait(command, 2000) != Unicore_RESULT_RESPONSE_COMMAND_OK) {
        delete _bestNavXyz;
        _bestNavXyz = nullptr; // Remove pointer so we will re-init next check
        return (false);
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_COMMAND, "bestnavxyz init ok");
    lastUpdateEcef = 0;
    uint16_t maxWait = (1000 / rate) + 100; // Wait for one response to come in
    unsigned long startTime = millis();

    while (1) {
        if (lastUpdateEcef > 0) {
            break;
        }
        if (millis() - startTime > maxWait) {
            log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "Failed to get response from BestNavXyz start");
            delete _bestNavXyz;
            _bestNavXyz = nullptr;
            return (false);
        }
        delay(10);
    }
    return true;
}

bool
UnicoreGNSSLibrary::initRecTime(float rate) {
    if ((startBinaryBeforeFix == false) && (isNmeaFixed() == false)) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "rectime init delayed until fix");
        return (false);
    }
    _recTime = new UNICORE_RECTIME_data_t;
    if (_recTime == nullptr) {
        log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "failed to allocate rectime data");
        return (false);
    }

    char command[50];
    snprintf(command, sizeof(command), "RECTIMEB %0.2f", rate);
    if (sendCommandAndWait(command, 2000) != Unicore_RESULT_RESPONSE_COMMAND_OK) {
        delete _recTime;
        _recTime = nullptr; // Remove pointer so we will re-init next check
        return (false);
    }

    log(UnicoreLogLevel::Info, UNICORE_LOG_COMMAND, "rectime init ok");
    lastUpdateDateTime = 0;
    uint16_t maxWait = (1000 / rate) + 100; // Wait for one response to come in
    unsigned long startTime = millis();

    while (1) {
        if (lastUpdateDateTime > 0) {
            break;
        }
        if (millis() - startTime > maxWait) {
            log(UnicoreLogLevel::Error, UNICORE_LOG_COMMAND, "Failed to get response from RecTime start");
            delete _recTime;
            _recTime = nullptr;
            return (false);
        }
        delay(10);
    }
    return true;
}

void
UnicoreGNSSLibrary::parserEomCallback(SEMP_PARSE_STATE* parse, const uint16_t type) {
    if (_activeInstance) {
        _activeInstance->handleParsedMessage(parse, type);
    }
}

bool
UnicoreGNSSLibrary::parserBadChecksumCallback(SEMP_PARSE_STATE* parse) {
    if (_activeInstance) {
        if (parse && parse->buffer) {
            _activeInstance->log(UnicoreLogLevel::Debug, UNICORE_LOG_PARSER, "bad checksum, received : %s",
                                 parse->buffer);
        } else {
            _activeInstance->log(UnicoreLogLevel::Debug, UNICORE_LOG_PARSER, "bad checksum, empty parser frame");
        }
    }
    return false;
}

void
UnicoreGNSSLibrary::parserDebugPrintf(const char* format, ...) {
    if (!_activeInstance || !format) {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    _activeInstance->log(UnicoreLogLevel::Debug, UNICORE_LOG_PARSER, "%s", buffer);
}

void
UnicoreGNSSLibrary::parserErrorPrintf(const char* format, ...) {
    if (!_activeInstance || !format) {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    _activeInstance->log(UnicoreLogLevel::Error, UNICORE_LOG_PARSER, "%s", buffer);
}

void
UnicoreGNSSLibrary::handleParsedMessage(SEMP_PARSE_STATE* parse, const uint16_t type) {
    if (!parse || !parse->buffer) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_PARSER, "parser delivered an empty message");
        return;
    }

    if (type == kParserNmea) {
        handleNmeaSentence(reinterpret_cast<const char*>(parse->buffer), parse->length);
    } else if (type == kParserRtcm) {
        handleRtcmMessage(parse->buffer, parse->length, sempRtcmGetMessageNumber(parse));
    } else if (type == kParserUnicoreBinary) {
        handleBinaryMessage(parse->buffer, parse->length);
    } else if (type == kParserUnicoreHash) {
        handleHashSentence(reinterpret_cast<const char*>(parse->buffer), parse->length);
    } else {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_PARSER, "unknown parser type=%u length=%u", type, parse->length);
    }
}

void
UnicoreGNSSLibrary::handleNmeaSentence(const char* sentence, const uint16_t length) {
    if (!sentence) {
        return;
    }

    if (strncmp(sentence, "$CONFIG,", 8) == 0) {
        handleConfigSentence(sentence, length);
    }

    updateCommandResultFromSentence(sentence);

    log(UnicoreLogLevel::Debug, UNICORE_LOG_RX, "RX Unicore NMEA %s", sentence);

    const size_t sentenceLength = (length > 0) ? length : strlen(sentence);
    if (sentenceLength < 6) {
        return;
    }

    char msgName[8] = {};
    const char* start = (*sentence == '$') ? (sentence + 1) : sentence;
    size_t index = 0;
    while (start[index] && (start[index] != ',') && (index < (sizeof(msgName) - 1))) {
        msgName[index] = start[index];
        index++;
    }

    if (strcasecmp(msgName, "GNGGA") == 0 || strcasecmp(msgName, "GPGGA") == 0) {
        const char* cursor = sentence;
        uint8_t field = 0;
        while (*cursor && (field < 6)) {
            if (*cursor == ',') {
                field++;
            }
            cursor++;
        }
        nmeaPositionStatus = static_cast<uint8_t>(strtoul(cursor, nullptr, 10));
        /*0 = Invalid / no fix
  1 = GPS/SPS fix
  2 = DGPS fix
  4 = RTK fixed
  5 = RTK float
  6 = Estimated / dead reckoning
  7 = Manual input
  8 = Simulation */
        //log(UnicoreLogLevel::Info, UNICORE_LOG_DATA, "GGA position status :%d", nmeaPositionStatus);
    }
    if (_nmeaCallback) {
        _nmeaCallback(sentence, length, _nmeaCallbackUserdata);
    }
}

void
UnicoreGNSSLibrary::handleConfigSentence(const char* sentence, const uint16_t length) {
    if (!sentence || !_configQueryActive) {
        return;
    }

    _lastConfigSentenceMs = millis();
    if (!_configQueryMatched && containsIgnoreCase(sentence, _configQueryText)) {
        _configQueryMatched = true;
        log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "CONFIG matched: %s", _configQueryText);
    }
}

void
UnicoreGNSSLibrary::handleRtcmMessage(const uint8_t* message, const uint16_t length, const uint16_t messageNumber) {
    if (!message && (length > 0)) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_RX, "RX RTCM rejected: null message length=%u", length);
        return;
    }

    log(UnicoreLogLevel::Debug, UNICORE_LOG_RX, "RX Unicore RTCM%u length=%u", messageNumber, length);
    if (_rtcmCallback) {
        _rtcmCallback(message, length, messageNumber, _rtcmCallbackUserdata);
    }
}

void
UnicoreGNSSLibrary::handleHashSentence(const char* sentence, const uint16_t length) {
    if (!sentence) {
        return;
    }

    log(UnicoreLogLevel::Debug, UNICORE_LOG_RX, "RX Unicore HASH %s", sentence);

    if (strncmp(sentence, "#VERSION", 8) == 0) {
        decodeVersionHash(sentence);
    }

    updateCommandResultFromSentence(sentence);

    if (_hashCallback) {
        _hashCallback(sentence, length, _hashCallbackUserdata);
    }
}

void
UnicoreGNSSLibrary::handleBinaryMessage(const uint8_t* message, const uint16_t length) {
    if (!message || (length < (UnicoreHeaderLength + 4))) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_RX, "RX binary rejected length=%u", length);
        return;
    }

    _lastBinaryHeader.cpuIdlePercent = readU1(message, offsetHeaderCpuIdle);
    _lastBinaryHeader.messageId = readU2(message, offsetHeaderMessageId);
    _lastBinaryHeader.messageLength = readU2(message, offsetHeaderMessageLength);
    _lastBinaryHeader.referenceTime = readU1(message, offsetHeaderReferenceTime);
    _lastBinaryHeader.timeStatus = readU1(message, offsetHeaderTimeStatus);
    _lastBinaryHeader.weekNumber = readU2(message, offsetHeaderWeekNumber);
    _lastBinaryHeader.secondsOfWeek = readU4(message, offsetHeaderSecondsOfWeek);
    _lastBinaryHeader.releaseVersion = readU1(message, offsetHeaderReleaseVersion);
    _lastBinaryHeader.leapSeconds = readU1(message, offsetHeaderLeapSecond);
    _lastBinaryHeader.outputDelayMs = readU2(message, offsetHeaderOutputDelay);

    const uint8_t* payload = &message[UnicoreHeaderLength];
    const uint16_t payloadLength = _lastBinaryHeader.messageLength;

    if ((UnicoreHeaderLength + payloadLength + 4U) > length) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_RX, "RX binary truncated id=%u payload=%u frame=%u",
            _lastBinaryHeader.messageId, payloadLength, length);
        return;
    }

    updateCommandResultFromBinary(_lastBinaryHeader.messageId);

    log(UnicoreLogLevel::Debug, UNICORE_LOG_RX, "RX Unicore Binary id=%u payload=%u week=%u sow=%lu",
        _lastBinaryHeader.messageId, payloadLength, _lastBinaryHeader.weekNumber,
        static_cast<unsigned long>(_lastBinaryHeader.secondsOfWeek));

    if (_binaryCallback) {
        _binaryCallback(_lastBinaryHeader, payload, payloadLength, _binaryCallbackUserdata);
    }

    switch (_lastBinaryHeader.messageId) {
        case messageIdBestnav: {
            if (_bestNav == nullptr) {
                _bestNav = new UNICORE_BESTNAV_data_t;
            }
            if (_bestNav == nullptr) {
                log(UnicoreLogLevel::Error, UNICORE_LOG_DATA, "failed to allocate bestnav data");
                return;
            }
            decodeBestNav(payload, payloadLength);
            break;
        }
        case messageIdBestnavXyz: {
            if (_bestNavXyz == nullptr) {
                _bestNavXyz = new UNICORE_BESTNAVXYZ_data_t;
            }
            if (_bestNavXyz == nullptr) {
                log(UnicoreLogLevel::Error, UNICORE_LOG_DATA, "failed to allocate bestnavxyz data");
                return;
            }
            decodeBestNavXyz(payload, payloadLength);
            break;
        }
        case messageIdRectime: {
            if (_recTime == nullptr) {
                _recTime = new UNICORE_RECTIME_data_t;
            }
            if (_recTime == nullptr) {
                log(UnicoreLogLevel::Error, UNICORE_LOG_DATA, "failed to allocate rectime data");
                return;
            }
            decodeRecTime(payload, payloadLength);
            break;
        }
        case messageIdVersion: decodeVersionBinary(payload, payloadLength); break;
        default: break;
    }
}

void
UnicoreGNSSLibrary::decodeBestNav(const uint8_t* payload, const uint16_t length) {
    if (!payload || (length < (offsetBestnavHorspdStd + sizeof(float))) || !_bestNav) {
        return;
    }
    _pvtArrivalMillis = millis();
    lastUpdateGeodetic = millis();
    _bestNav->solutionStatus = static_cast<uint8_t>(readU4(payload, offsetBestnavPsolStatus));
    _bestNav->positionType = static_cast<uint8_t>(readU4(payload, offsetBestnavPosType));
    _bestNav->latitude = readF8(payload, offsetBestnavLat);
    _bestNav->longitude = readF8(payload, offsetBestnavLon);
    _bestNav->altitude = readF8(payload, offsetBestnavHgt);
    _bestNav->latitudeDeviation = readF4(payload, offsetBestnavLatDeviation);
    _bestNav->longitudeDeviation = readF4(payload, offsetBestnavLonDeviation);
    _bestNav->heightDeviation = readF4(payload, offsetBestnavHgtDeviation);
    _bestNav->satellitesTracked = readU1(payload, offsetBestnavSatsTracked);
    _bestNav->satellitesUsed = readU1(payload, offsetBestnavSatsUsed);
    const uint8_t extendedStatus = readU1(payload, offsetBestnavExtSolStat);
    _bestNav->rtkSolution = extendedStatus & 0x03U;
    _bestNav->pseudorangeCorrection = (extendedStatus >> 2U) & 0x03U;
    _bestNav->velocityType = static_cast<uint8_t>(readU4(payload, offsetBestnavVelType));
    _bestNav->horizontalSpeed = readF8(payload, offsetBestnavHorSpd);
    _bestNav->trackGround = readF8(payload, offsetBestnavTrkGnd);
    _bestNav->verticalSpeed = readF8(payload, offsetBestnavVertSpd);
    _bestNav->verticalSpeedDeviation = readF4(payload, offsetBestnavVerspdStd);
    _bestNav->horizontalSpeedDeviation = readF4(payload, offsetBestnavHorspdStd);

    _horizontalAccuracy = (_bestNav->latitudeDeviation > _bestNav->longitudeDeviation) ? _bestNav->latitudeDeviation
                                                                                       : _bestNav->longitudeDeviation;
    log(UnicoreLogLevel::Debug, UNICORE_LOG_DATA, "BESTNAV fix=%u solution=%u sats=%u used=%u lat=%.8f lon=%.8f",
        _bestNav->positionType, _bestNav->solutionStatus, _bestNav->satellitesTracked, _bestNav->satellitesUsed,
        _bestNav->latitude, _bestNav->longitude);
}

void
UnicoreGNSSLibrary::decodeBestNavXyz(const uint8_t* payload, const uint16_t length) {
    if (!payload || (length < (offsetBestnavXyzPZDeviation + sizeof(float)))) {
        return;
    }

    _bestNavXyz->ecefX = readF8(payload, offsetBestnavXyzPX);
    _bestNavXyz->ecefY = readF8(payload, offsetBestnavXyzPY);
    _bestNavXyz->ecefZ = readF8(payload, offsetBestnavXyzPZ);
    _bestNavXyz->ecefXDeviation = readF4(payload, offsetBestnavXyzPXDeviation);
    _bestNavXyz->ecefYDeviation = readF4(payload, offsetBestnavXyzPYDeviation);
    _bestNavXyz->ecefZDeviation = readF4(payload, offsetBestnavXyzPZDeviation);

    lastUpdateEcef = millis();
    log(UnicoreLogLevel::Debug, UNICORE_LOG_DATA, "BESTNAVXYZ x=%.3f y=%.3f z=%.3f", _bestNavXyz->ecefX,
        _bestNavXyz->ecefY, _bestNavXyz->ecefZ);
}

void
UnicoreGNSSLibrary::decodeRecTime(const uint8_t* payload, const uint16_t length) {
    if (!payload || (length < (offsetRectimeUtcStatus + sizeof(uint32_t)))) {
        return;
    }
    lastUpdateDateTime = millis();
    _recTime->timeOffset = readF8(payload, offsetRectimeOffset);
    _recTime->timeDeviation = readF8(payload, offsetRectimeOffsetStd);
    _recTime->year = static_cast<uint16_t>(readU4(payload, offsetRectimeUtcYear));
    _recTime->month = readU1(payload, offsetRectimeUtcMonth);
    _recTime->day = readU1(payload, offsetRectimeUtcDay);
    _recTime->hour = readU1(payload, offsetRectimeUtcHour);
    _recTime->minute = readU1(payload, offsetRectimeUtcMinute);

    const uint32_t milliseconds = readU4(payload, offsetRectimeUtcMillisecond);
    _recTime->second = static_cast<uint8_t>(milliseconds / 1000U);
    _recTime->millisecond = static_cast<uint16_t>(milliseconds % 1000U);

    const uint32_t clockStatus = readU4(payload, offsetRectimeClockStatus);
    const uint32_t utcStatus = readU4(payload, offsetRectimeUtcStatus);
    _recTime->timeStatus = static_cast<uint8_t>(clockStatus);
    _recTime->dateStatus = static_cast<uint8_t>(utcStatus);

    _validTime = _recTime->timeStatus != 3;
    _validDate = (_recTime->dateStatus == 1) || (_recTime->dateStatus == 2);
    _fullyResolved = _validDate && _validTime;

    log(UnicoreLogLevel::Debug, UNICORE_LOG_DATA, "RECTIME %04u-%02u-%02u %02u:%02u:%02u.%03u", _recTime->year,
        _recTime->month, _recTime->day, _recTime->hour, _recTime->minute, _recTime->second, _recTime->millisecond);
}

void
UnicoreGNSSLibrary::decodeVersionBinary(const uint8_t* payload, const uint16_t length) {
    if (!payload || (length <= offsetVersionFirmwareVersion)) {
        return;
    }

    _version.modelType = readU1(payload, offsetVersionModuleType);
    copyFixedString(_version.swVersion, sizeof(_version.swVersion), payload, offsetVersionFirmwareVersion, length,
                    sizeof(_version.swVersion) - 1);
    copyFixedString(_version.serialNumber, sizeof(_version.serialNumber), payload, offsetVersionPsn, length,
                    sizeof(_version.serialNumber) - 1);
    copyFixedString(_version.efuseID, sizeof(_version.efuseID), payload, offsetVersionEfuseID, length,
                    sizeof(_version.efuseID) - 1);
    copyFixedString(_version.compileTime, sizeof(_version.compileTime), payload, offsetVersionCompTime, length,
                    sizeof(_version.compileTime) - 1);

    lastUpdateVersion = millis();
    log(UnicoreLogLevel::Info, UNICORE_LOG_DATA, "VERSIONB model=%u sw=%s sn=%s", _version.modelType,
        _version.swVersion, _version.serialNumber);
}

void
UnicoreGNSSLibrary::decodeVersionHash(const char* sentence) {
#ifdef UNICORE_NULLPTR_CHECK
    if (!sentence) {
        return;
    }
#endif // UNICORE_NULLPTR_CHECK

    const char* payload = strchr(sentence, ';');
    if (!payload) {
        return;
    }
    payload++;

    char model[18] = {};
    getCsvField(payload, 0, model, sizeof(model));
    memcpy(_version.modelName, model, sizeof(_version.modelName) - 1);
    if (equalsIgnoreCase(model, "UM980")) {
        _version.modelType = 18;
    } else if (equalsIgnoreCase(model, "UM982")) {
        _version.modelType = 17;
    } else if (equalsIgnoreCase(model, "UM981")) {
        _version.modelType = 26;
    } else if (equalsIgnoreCase(model, "UM960")) {
        _version.modelType = 19;
    }

    const char* softwareField = strchr(payload, ',');
    if (softwareField) {
        copyToken(_version.swVersion, sizeof(_version.swVersion), softwareField + 1);
    }

    char field[64] = {};
    if (getCsvField(payload, 3, field, sizeof(field))) {
        copyToken(_version.serialNumber, sizeof(_version.serialNumber), field);
    }
    if (getCsvField(payload, 4, field, sizeof(field))) {
        copyToken(_version.efuseID, sizeof(_version.efuseID), field);
    }
    if (getCsvField(payload, 5, field, sizeof(field))) {
        copyToken(_version.compileTime, sizeof(_version.compileTime), field);
    }

    lastUpdateVersion = millis();
    log(UnicoreLogLevel::Info, UNICORE_LOG_DATA, "VERSION model=%u sw=%s sn=%s", _version.modelType, _version.swVersion,
        _version.serialNumber);
}

void
UnicoreGNSSLibrary::updateCommandResultFromSentence(const char* sentence) {
    if (!sentence || !_commandPending || (_lastCommandResult != Unicore_RESULT_RESPONSE_COMMAND_WAITING)) {
        return;
    }

    if (containsIgnoreCase(sentence, "ERROR") || containsIgnoreCase(sentence, "FAIL")
        || containsIgnoreCase(sentence, "INVALID")) {
        log(UnicoreLogLevel::Warn, UNICORE_LOG_COMMAND, "command error response: %s", sentence);
        completePendingCommand(Unicore_RESULT_RESPONSE_COMMAND_ERROR);
    } else if (pendingExpectedMatches(sentence)) {
        log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command response matched expected: %s",
            _pendingExpectedResponse);
        completePendingCommand(Unicore_RESULT_RESPONSE_COMMAND_OK);
    } else if (!_pendingExpectedResponse[0] && commandAckMatchesPendingCommand(sentence, _pendingCommand)) {
        log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command response matched pending command: %s",
            _pendingCommand);
        completePendingCommand(Unicore_RESULT_RESPONSE_COMMAND_OK);
    } else if (!_pendingExpectedResponse[0]
               && (containsIgnoreCase(sentence, "OK") || containsIgnoreCase(sentence, "SUCCESS"))) {
        log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command generic success response: %s", sentence);
        completePendingCommand(Unicore_RESULT_RESPONSE_COMMAND_OK);
    }
}

void
UnicoreGNSSLibrary::updateCommandResultFromBinary(const uint16_t messageId) {
    if (!_commandPending || (_lastCommandResult != Unicore_RESULT_RESPONSE_COMMAND_WAITING)) {
        return;
    }

    const char* name = binaryMessageName(messageId);
    if ((name[0] != 0) && pendingExpectedMatches(name)) {
        log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command binary response matched expected: %s", name);
        completePendingCommand(Unicore_RESULT_RESPONSE_COMMAND_OK);
    }
}

void
UnicoreGNSSLibrary::clearCommandResult() {
    _lastCommandResult = Unicore_RESULT_RESPONSE_COMMAND_WAITING;
    _pendingCommand[0] = 0;
    _pendingExpectedResponse[0] = 0;
}

void
UnicoreGNSSLibrary::completePendingCommand(const UnicoreResult_t result) {
    char command[sizeof(_pendingCommand)] = {};
    bool hadPendingCommand = false;

    if (_commandStateMutex) {
        xSemaphoreTake(_commandStateMutex, portMAX_DELAY);
    }

    hadPendingCommand = _commandPending;
    strncpy(command, _pendingCommand, sizeof(command) - 1);
    if (_commandPending || (result != Unicore_RESULT_RESPONSE_COMMAND_OK)) {
        _lastCommandResult = result;
        _commandPending = false;
    }

    if (_commandStateMutex) {
        xSemaphoreGive(_commandStateMutex);
    }

    if (_commandDoneSemaphore) {
        xSemaphoreGive(_commandDoneSemaphore);
    }

    if (hadPendingCommand || (result != Unicore_RESULT_RESPONSE_COMMAND_OK)) {
        log((result == Unicore_RESULT_RESPONSE_COMMAND_OK) ? UnicoreLogLevel::Info : UnicoreLogLevel::Warn,
            UNICORE_LOG_COMMAND, "command complete %s -> %s||\n", command[0] ? command : "<unknown>",
            resultName(result));
    }
}

UnicoreResult_t
UnicoreGNSSLibrary::waitForCommandResponse(const uint32_t timeoutMs) {
    if (isRxTaskRunning() && _commandDoneSemaphore) {
        if (xSemaphoreTake(_commandDoneSemaphore, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
            log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command [%s] wait done -> %s",
                _pendingCommand[0] ? _pendingCommand : "<unknown>", resultName(_lastCommandResult));
            return _lastCommandResult;
        }

        log(UnicoreLogLevel::Warn, UNICORE_LOG_COMMAND, "command [%s] wait timeout after %lu ms",
            _pendingCommand[0] ? _pendingCommand : "<unknown>", static_cast<unsigned long>(timeoutMs));
        completePendingCommand(Unicore_RESULT_TIMEOUT_RESPONSE);
        while (xSemaphoreTake(_commandDoneSemaphore, 0) == pdTRUE) {}
        return Unicore_RESULT_TIMEOUT_RESPONSE;
    }

    const uint32_t startMs = millis();
    while ((millis() - startMs) < timeoutMs) {
        poll();
        if (!_commandPending) {
            log(UnicoreLogLevel::Debug, UNICORE_LOG_COMMAND, "command polled wait done -> %s",
                resultName(_lastCommandResult));
            return _lastCommandResult;
        }
        delay(1);
    }

    log(UnicoreLogLevel::Warn, UNICORE_LOG_COMMAND, "command polled wait timeout after %lu ms",
        static_cast<unsigned long>(timeoutMs));
    completePendingCommand(Unicore_RESULT_TIMEOUT_RESPONSE);
    while (_commandDoneSemaphore && (xSemaphoreTake(_commandDoneSemaphore, 0) == pdTRUE)) {}
    return Unicore_RESULT_TIMEOUT_RESPONSE;
}

bool
UnicoreGNSSLibrary::pendingExpectedMatches(const char* text) const {
    if (!_pendingExpectedResponse[0]) {
        return false;
    }
    return containsIgnoreCase(text, _pendingExpectedResponse);
}

void
UnicoreGNSSLibrary::rxTask() {
    _rxTaskRunning = true;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    log(UnicoreLogLevel::Info, UNICORE_LOG_TASK, "RX task running");
    while (_rxTaskShouldRun) {
        const size_t parsed = poll();
        if (parsed == 0) {
            vTaskDelayUntil(&xLastWakeTime, 2);
        } else {
            vTaskDelayUntil(&xLastWakeTime, 1);
        }
    }
    _rxTaskRunning = false;
    _rxTaskHandle = nullptr;
    log(UnicoreLogLevel::Info, UNICORE_LOG_TASK, "RX task exited");
}

void
UnicoreGNSSLibrary::log(const UnicoreLogLevel level, const uint32_t mask, const char* format, ...) {
    if (!isLogEnabled(level, mask) || !format) {
        return;
    }

    char buffer[192] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    _logCallback(level, mask, buffer, _logCallbackUserdata);
}
