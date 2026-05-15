#ifndef UNICORE_GNSS_ARDUINO_LIBRARY_H
#define UNICORE_GNSS_ARDUINO_LIBRARY_H

#include <cstdint>
#include "HardwareSerial.h"
#include "SparkFun_Extensible_Message_Parser.h"
#include "Unicore_Struct.h"

#ifndef UNICORE_NULLPTR_CHECK
#define UNICORE_NULLPTR_CHECK
#endif

enum class UnicorePort : uint8_t {
    Current = 0,
    Com1,
    Com2,
    Com3,
    Usb,
};

enum class UnicoreLogTrigger : uint8_t {
    Once = 0,
    OnTime,
    OnChanged,
};

enum class UnicoreLogLevel : uint8_t {
    Off = 0,
    Error,
    Warn,
    Info,
    Debug,
    Verbose,
};

enum UnicoreLogMask : uint32_t {
    UNICORE_LOG_NONE = 0,
    UNICORE_LOG_COMMAND = 1UL << 0,
    UNICORE_LOG_RX = 1UL << 1,
    UNICORE_LOG_TX = 1UL << 2,
    UNICORE_LOG_PARSER = 1UL << 3,
    UNICORE_LOG_DATA = 1UL << 4,
    UNICORE_LOG_TASK = 1UL << 5,
    UNICORE_LOG_CHILD_CLASS = 1UL << 6,
    UNICORE_LOG_ALL = 0xFFFFFFFFUL,
};

struct UnicoreBinaryHeader {
    uint8_t cpuIdlePercent = 0;
    uint16_t messageId = 0;
    uint16_t messageLength = 0;
    uint8_t referenceTime = 0;
    uint8_t timeStatus = 0;
    uint16_t weekNumber = 0;
    uint32_t secondsOfWeek = 0;
    uint8_t releaseVersion = 0;
    uint8_t leapSeconds = 0;
    uint16_t outputDelayMs = 0;
};

typedef void (*UnicoreRtcmCallback)(const uint8_t* message, uint16_t length, uint16_t messageNumber, void* userdata);
typedef void (*UnicoreNmeaCallback)(const char* sentence, uint16_t length, void* userdata);
typedef void (*UnicoreBinaryCallback)(const UnicoreBinaryHeader& header, const uint8_t* payload, uint16_t length,
                                      void* userdata);
typedef void (*UnicoreHashCallback)(const char* sentence, uint16_t length, void* userdata);

class UnicoreGNSSLibrary {
  public:
    UnicoreGNSSLibrary();
    virtual ~UnicoreGNSSLibrary();

    /**
     * @brief Begin communication with the GNSS module over the specified serial port. Optionally provide Print objects for
     * debugging and error output.
     * 
     * @param serialPort The HardwareSerial port connected to the GNSS module
     * @param parserDebug  Print object for debugging output from the message parser (optional)
     * @param parserError Print object for error output from the message parser (optional)
     * @return true 
     * @return false 
     */
    bool begin(HardwareSerial& serialPort, Print* parserDebug = nullptr, Print* parserError = &Serial,
               uint16_t rxBufferSize = 512);
    void end();
    bool isConnected() const;
    bool isOnline();
    bool disableOutput();

    bool startRxTask(uint32_t stackSize = 4096, UBaseType_t priority = 3, BaseType_t coreId = 0);
    void stopRxTask(uint32_t timeoutMs = 1000);
    bool isRxTaskRunning() const;

    size_t poll();
    size_t available() const;

    UnicoreResult_t sendCommand(const char* command);
    UnicoreResult_t sendCommand(const String& command);
    UnicoreResult_t sendCommandAsync(const char* command, const char* expectedResponse = nullptr);
    UnicoreResult_t sendCommandAsync(const String& command, const char* expectedResponse = nullptr);
    UnicoreResult_t sendCommandAndWait(const char* command, uint32_t timeoutMs = 1000,
                                       const char* expectedResponse = nullptr);
    UnicoreResult_t sendCommandAndWait(const String& command, uint32_t timeoutMs = 1000,
                                       const char* expectedResponse = nullptr);
    bool isCommandPending() const;
    UnicoreResult_t getLastCommandResult() const;
    // Data Output commands
    //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    // Set the output rate of a given message on a given COM port/Use
    // 1, 0.5, 0.2, 0.1 corresponds to 1Hz, 2Hz, 5Hz, 10Hz respectively.
    // Ex: GPGGA 0.5 <- 2 times per second
    // Returns true if successful
    UnicoreResult_t requestMessage(const char* messageName, uint32_t timeoutMs = 1000);
    UnicoreResult_t logMessage(const char* messageName, UnicorePort port = UnicorePort::Current,
                               UnicoreLogTrigger trigger = UnicoreLogTrigger::OnTime, float periodSeconds = 1.0f);
    UnicoreResult_t unlogMessage(const char* messageName, UnicorePort port = UnicorePort::Current);
    UnicoreResult_t unlogPort(UnicorePort port = UnicorePort::Current);

    // Called mask (disable) and unmask (enable), this is how to ignore certain
    // constellations, or signal/frequencies, or satellite elevations Returns true
    // if successful
    UnicoreResult_t enableSystem(const char* systemName);
    UnicoreResult_t disableSystem(const char* systemName);
    // Config commands
    //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    UnicoreResult_t setPortBaudrate(UnicorePort port, uint32_t baudrate);
    UnicoreResult_t queryConfigContains(const char* configText, uint32_t timeoutMs = 2000, uint32_t quietMs = 150);
    UnicoreResult_t saveConfiguration();
    UnicoreResult_t factoryReset();

    // void setBestNavCallback(void (*callback)(UNICORE_BESTNAV_data_t*), UNICORE_BESTNAV_data_t* callbackData = nullptr);
    // void setBestNavXyzCallback(void (*callback)(UNICORE_BESTNAVXYZ_data_t*),
    //                            UNICORE_BESTNAVXYZ_data_t* callbackData = nullptr);
    // void setRecTimeCallback(void (*callback)(UNICORE_RECTIME_data_t*), UNICORE_RECTIME_data_t* callbackData = nullptr);
    void setNmeaCallback(UnicoreNmeaCallback callback, void* context = nullptr);
    void setRtcmCallback(UnicoreRtcmCallback callback, void* context = nullptr);
    void setBinaryCallback(UnicoreBinaryCallback callback, void* context = nullptr);
    void setHashCallback(UnicoreHashCallback callback, void* context = nullptr);

    void enableBinaryBeforeFix();
    void disableBinaryBeforeFix();

    void setLogOutput(Print* logPort);
    void setLogLevel(UnicoreLogLevel level);
    void setLogMask(uint32_t mask);
    void enableLogCategory(uint32_t mask);
    void disableLogCategory(uint32_t mask);
    void enableDebugLogging(Print& logPort, UnicoreLogLevel level = UnicoreLogLevel::Debug,
                            uint32_t mask = UNICORE_LOG_COMMAND | UNICORE_LOG_RX | UNICORE_LOG_TASK);
    void disableDebugLogging();
    UnicoreLogLevel getLogLevel() const;
    uint32_t getLogMask() const;
    bool isLogEnabled(UnicoreLogLevel level, uint32_t mask) const;

    static const char* portName(UnicorePort port);
    static const char* triggerName(UnicoreLogTrigger trigger);

    // By default, library will attempt to start RECTIME and BESTNAV regardless of GNSS fix.
    // This may lead to command timeouts as the UM980 does not appear to respond to BESTNAVB commands if 3D fix is not
    // achieved. Set startBinartBeforeFix = false via disableBinaryBeforeFix() to block binary commands before a fix is
    // achieved
    bool startBinaryBeforeFix = true;

    bool _printBadChecksum = false;       // Display bad checksum message from the parser
    bool _printParserTransitions = false; // Display the parser transitions
    bool _printRxMessages = false;        // Display the received message summary
    bool _dumpRxMessages = false;         // Display the received message hex dump

    uint8_t nmeaPositionStatus = 0; // Position psition status obtained from GNGGA NMEA

  public:
    void log(UnicoreLogLevel level, uint32_t mask, const char* format, ...);
    const UnicoreBinaryHeader& getLastBinaryHeader() const;
    uint32_t getLastBestNavMs() const;
    uint32_t getLastBestNavXyzMs() const;
    uint32_t getLastRecTimeMs() const;
    uint32_t getLastVersionMs() const;

    UNICORE_BESTNAV_data_t _bestNav = {};
    UNICORE_BESTNAVXYZ_data_t _bestNavXyz = {};
    UNICORE_RECTIME_data_t _recTime = {};
    UNICORE_VERSION_data_t _version = {};
    float _horizontalAccuracy = 0.0f;
    bool _validDate = false;
    bool _validTime = false;
    bool _fullyResolved = false;
    unsigned long _pvtArrivalMillis = 0;

  private:
    Print* _debugPort = nullptr;
    UnicoreLogLevel _logLevel = UnicoreLogLevel::Off;
    uint32_t _logMask = UNICORE_LOG_NONE;
    SEMP_PARSE_STATE* _sempParse = nullptr;
    Print* _parserErrorPort = nullptr;
    unsigned long lastUpdateGeodetic = 0;
    unsigned long lastUpdateEcef = 0;
    unsigned long lastUpdateDateTime = 0;
    unsigned long lastUpdateVersion = 0;

    UnicoreBinaryHeader _lastBinaryHeader = {};
    volatile UnicoreResult_t _lastCommandResult = Unicore_RESULT_RESPONSE_COMMAND_WAITING;

    TaskHandle_t _rxTaskHandle = nullptr;
    volatile bool _rxTaskShouldRun = false;
    volatile bool _rxTaskRunning = false;
    SemaphoreHandle_t _commandStateMutex = nullptr;
    SemaphoreHandle_t _commandDoneSemaphore = nullptr;
    volatile bool _commandPending = false;
    char _pendingCommand[64] = {};
    char _pendingExpectedResponse[24] = {};
    volatile bool _configQueryActive = false;
    volatile bool _configQueryMatched = false;
    volatile uint32_t _lastConfigSentenceMs = 0;
    char _configQueryText[96] = {};
    UnicoreNmeaCallback _nmeaCallback = nullptr;
    void* _nmeaCallbackUserdata = nullptr;
    UnicoreRtcmCallback _rtcmCallback = nullptr;
    void* _rtcmCallbackUserdata = nullptr;
    UnicoreBinaryCallback _binaryCallback = nullptr;
    void* _binaryCallbackUserdata = nullptr;
    UnicoreHashCallback _hashCallback = nullptr;
    void* _hashCallbackUserdata = nullptr;

    static UnicoreGNSSLibrary* _activeInstance;
    static void rxTaskEntry(void* context);
    static void parserEomCallback(SEMP_PARSE_STATE* parse, uint16_t type);
    static bool parserBadChecksumCallback(SEMP_PARSE_STATE* parse);
    static void parserDebugPrintf(const char* format, ...);
    static void parserErrorPrintf(const char* format, ...);
    // handleMessage
    //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    void handleParsedMessage(SEMP_PARSE_STATE* parse, uint16_t type);
    void handleNmeaSentence(const char* sentence, const uint16_t length);
    void handleConfigSentence(const char* sentence, const uint16_t length);
    void handleRtcmMessage(const uint8_t* message, const uint16_t length, uint16_t messageNumber);
    void handleHashSentence(const char* sentence, const uint16_t length);
    void handleBinaryMessage(const uint8_t* message, const uint16_t length);
    // decoders
    //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    void decodeBestNav(const uint8_t* payload, uint16_t length);
    void decodeBestNavXyz(const uint8_t* payload, uint16_t length);
    void decodeRecTime(const uint8_t* payload, uint16_t length);
    void decodeVersionBinary(const uint8_t* payload, uint16_t length);
    void decodeVersionHash(const char* sentence);
    // Command result handling
    //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    void updateCommandResultFromSentence(const char* sentence);
    void updateCommandResultFromBinary(uint16_t messageId);
    void clearCommandResult();
    void completePendingCommand(UnicoreResult_t result);
    UnicoreResult_t waitForCommandResponse(uint32_t timeoutMs);
    bool pendingExpectedMatches(const char* text) const;
    void rxTask();

  protected:
    HardwareSerial* _hwSerialPort = nullptr;
    uint16_t _rxBufferSize = 0;
    uint8_t* _rxBuffer = nullptr;

    // virtual void onBestNavUpdated(const UNICORE_BESTNAV_data_t& data);
    // virtual void onBestNavXyzUpdated(const UNICORE_BESTNAVXYZ_data_t& data);
    // virtual void onRecTimeUpdated(const UNICORE_RECTIME_data_t& data);
    // virtual void onVersionUpdated(const UNICORE_VERSION_data_t& data);
};

#endif // UNICORE_GNSS_ARDUINO_LIBRARY_H
