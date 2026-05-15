#ifndef UNICORE_UM980_H
#define UNICORE_UM980_H

#include "Unicore_GNSS_Library.h"
#include "soc/gpio_num.h"

struct Um980ConstellationCommand {
    const char* displayName;
    const char* commandName;
};

struct Um980MessageConfig {
    const char* name;
    float defaultPeriodSeconds;
};

enum Um980DynamicModel : uint8_t {
    UM980_DYN_MODEL_SURVEY = 0,
    UM980_DYN_MODEL_UAV,
    UM980_DYN_MODEL_AUTOMOTIVE,
};

enum class Um980Mode : uint8_t {
    Unknown = 0,
    Rover,
    Base,
};

inline constexpr Um980ConstellationCommand kUm980ConstellationCommands[] = {
    {"BeiDou", "BDS"}, {"Galileo", "GAL"}, {"GLONASS", "GLO"}, {"GPS", "GPS"}, {"QZSS", "QZSS"},
};

inline constexpr Um980MessageConfig kUm980NmeaMessages[] = {
    {"GPDTM", 0.0f}, {"GPGBS", 0.0f}, {"GPGGA", 1.0f}, {"GPGLL", 0.0f}, {"GPGNS", 0.0f},
    {"GPGRS", 0.0f}, {"GPGSA", 3.0f}, {"GPGST", 1.0f}, {"GPGSV", 2.0f}, {"GPRMC", 1.0f},
    {"GPROT", 0.0f}, {"GPTHS", 0.0f}, {"GPVTG", 0.0f}, {"GPZDA", 1.0f},
};

inline constexpr Um980MessageConfig kUm980RtcmMessages[] = {
    {"RTCM1001", 0.0f}, {"RTCM1002", 0.0f}, {"RTCM1003", 0.0f}, {"RTCM1004", 0.0f}, {"RTCM1005", 1.0f},
    {"RTCM1006", 0.0f}, {"RTCM1007", 0.0f}, {"RTCM1009", 0.0f}, {"RTCM1010", 0.0f}, {"RTCM1011", 0.0f},
    {"RTCM1012", 0.0f}, {"RTCM1013", 0.0f}, {"RTCM1019", 0.0f}, {"RTCM1020", 0.0f}, {"RTCM1033", 10.0f},
    {"RTCM1042", 0.0f}, {"RTCM1044", 0.0f}, {"RTCM1045", 0.0f}, {"RTCM1046", 0.0f}, {"RTCM1071", 0.0f},
    {"RTCM1072", 0.0f}, {"RTCM1073", 0.0f}, {"RTCM1074", 1.0f}, {"RTCM1075", 0.0f}, {"RTCM1076", 0.0f},
    {"RTCM1077", 0.0f}, {"RTCM1081", 0.0f}, {"RTCM1082", 0.0f}, {"RTCM1083", 0.0f}, {"RTCM1084", 1.0f},
    {"RTCM1085", 0.0f}, {"RTCM1086", 0.0f}, {"RTCM1087", 0.0f}, {"RTCM1091", 0.0f}, {"RTCM1092", 0.0f},
    {"RTCM1093", 0.0f}, {"RTCM1094", 1.0f}, {"RTCM1095", 0.0f}, {"RTCM1096", 0.0f}, {"RTCM1097", 0.0f},
    {"RTCM1104", 0.0f}, {"RTCM1111", 0.0f}, {"RTCM1112", 0.0f}, {"RTCM1113", 0.0f}, {"RTCM1114", 0.0f},
    {"RTCM1115", 0.0f}, {"RTCM1116", 0.0f}, {"RTCM1117", 0.0f}, {"RTCM1121", 0.0f}, {"RTCM1122", 0.0f},
    {"RTCM1123", 0.0f}, {"RTCM1124", 1.0f}, {"RTCM1125", 0.0f}, {"RTCM1126", 0.0f}, {"RTCM1127", 0.0f},
};

static constexpr size_t MAX_UM980_CONSTELLATIONS =
    sizeof(kUm980ConstellationCommands) / sizeof(kUm980ConstellationCommands[0]);
static constexpr size_t MAX_UM980_NMEA_MSG = sizeof(kUm980NmeaMessages) / sizeof(kUm980NmeaMessages[0]);
static constexpr size_t MAX_UM980_RTCM_MSG = sizeof(kUm980RtcmMessages) / sizeof(kUm980RtcmMessages[0]);
static constexpr int16_t UM980_MESSAGE_NOT_FOUND = -1;

class UnicoreUM980 : public UnicoreGNSSLibrary {
  public:
    static constexpr const char* MSG_BESTNAVB = "BESTNAVB";
    static constexpr const char* MSG_BESTNAVXYZB = "BESTNAVXYZB";
    static constexpr const char* MSG_RECTIMEB = "RECTIMEB";
    static constexpr const char* MSG_VERSION = "VERSION";

    UnicoreUM980(gpio_num_t PowerPin);

    void init();
    void powerOn();
    void powerOff();

    void isOnline(bool online);

    void resetDefaults();
    void baseRtcmDefault();
    void baseRtcmLowDataRate();

    bool configure();
    bool configureRover();

    UnicoreResult_t configureOnceTime();
    UnicoreResult_t configureGNSS(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t configureBase(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t configureRoverOutput(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t configureBaseOutput(UnicorePort port = UnicorePort::Current);

    UnicoreResult_t requestVersion(uint32_t timeoutMs = 1000);
    UnicoreResult_t enableBinaryNavigation(UnicorePort port = UnicorePort::Current, float periodSeconds = 1.0f);
    UnicoreResult_t disableBinaryNavigation(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t disableAllOutput();
    UnicoreResult_t enableNmeaMessages(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t disableNmeaMessages(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t enableRtcmRoverMessages(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t enableRtcmBaseMessages(UnicorePort port = UnicorePort::Current);
    UnicoreResult_t disableRtcmMessages(UnicorePort port = UnicorePort::Current);

    bool setModel(uint8_t modelNumber);
    uint8_t getModel();
    UnicoreResult_t setMode(const char* modeCommand);
    UnicoreResult_t setRoverMode(const char* roverType);
    UnicoreResult_t setBaseMode();
    UnicoreResult_t setRate(double secondsBetweenSolutions);
    UnicoreResult_t setElevation(uint8_t elevationDegrees);
    UnicoreResult_t setMinCno(uint8_t cnoValue);
    UnicoreResult_t setMultipathMitigation(bool enable);
    UnicoreResult_t setConstellations();
    UnicoreResult_t setConstellationEnabled(const char* commandName, bool enabled);

    bool setNmeaMessagePeriod(const char* msgName, float periodSeconds);
    bool setRtcmRoverMessagePeriod(const char* msgName, float periodSeconds);
    bool setRtcmBaseMessagePeriod(const char* msgName, float periodSeconds);
    float getNmeaMessagePeriod(const char* msgName) const;
    float getRtcmRoverMessagePeriod(const char* msgName) const;
    float getRtcmBaseMessagePeriod(const char* msgName) const;

    uint8_t getActiveNmeaMessageCount() const;
    uint8_t getActiveRtcmRoverMessageCount() const;
    uint8_t getActiveRtcmBaseMessageCount() const;
    int16_t getNmeaMessageNumberByName(const char* msgName) const;
    int16_t getRtcmMessageNumberByName(const char* msgName) const;
    bool isGgaActive() const;

    double getLatitude() const;
    double getLongitude() const;
    double getAltitude() const;
    float getHorizontalAccuracy() const;
    uint8_t getFixType() const;
    uint8_t getCarrierSolution() const;
    uint8_t getSatellitesInView() const;
    uint8_t getDay() const;
    uint8_t getMonth() const;
    uint16_t getYear() const;
    uint8_t getHour() const;
    uint8_t getMinute() const;
    uint8_t getSecond() const;
    uint16_t getMillisecond() const;
    uint8_t getLeapSeconds() const;
    double getEcefX() const;
    double getEcefY() const;
    double getEcefZ() const;
    uint16_t getFixAgeMilliseconds() const;
    double getRateS() const;
    Um980Mode getMode() const;
    const char* getFirmwareVersion() const;
    const char* getSerialNumber() const;
    uint8_t getModelType() const;
    const char* getId() const;

    bool inRoverMode() const;
    bool isFixed() const;
    bool isDgpsFixed() const;
    bool isRTKFix() const;
    bool isRTKFloat() const;
    bool isValidDate() const;
    bool isValidTime() const;
    bool isFullyResolved() const;
    // process
    void processNmeaSentence(const char* sentence, uint16_t length = 0);
    //handle
    void handleModeSentence(const char* sentence, uint16_t length);

  private:
    gpio_num_t _powerPin;

  private:
    float _nmeaPeriods[MAX_UM980_NMEA_MSG] = {};
    float _rtcmRoverPeriods[MAX_UM980_RTCM_MSG] = {};
    float _rtcmBasePeriods[MAX_UM980_RTCM_MSG] = {};
    bool _constellationEnabled[MAX_UM980_CONSTELLATIONS] = {};

    double _rateSeconds = 1.0;
    bool _online = false;
    Um980Mode _mode = Um980Mode::Unknown;

    static void RtcmCallback(const uint8_t* message, uint16_t length, uint16_t messageNumber, void* userdata);
    static void NmeaCallback(const char* sentence, uint16_t length, void* userdata);
    static void BinaryCallback(const UnicoreBinaryHeader& header, const uint8_t* payload, uint16_t length,
                               void* userdata);
    static void HashCallback(const char* sentence, uint16_t length, void* userdata);

    UnicoreResult_t applyMessagePeriods(const Um980MessageConfig* messages, const float* periods, size_t count,
                                        UnicorePort port);
    UnicoreResult_t unlogMessages(const Um980MessageConfig* messages, size_t count, UnicorePort port);
    bool setMessagePeriod(const Um980MessageConfig* messages, float* periods, size_t count, const char* msgName,
                          float periodSeconds);
    float getMessagePeriod(const Um980MessageConfig* messages, const float* periods, size_t count,
                           const char* msgName) const;
    int16_t findMessageIndex(const Um980MessageConfig* messages, size_t count, const char* msgName) const;
    UnicoreResult_t firstError(UnicoreResult_t current, UnicoreResult_t next,
                               UnicoreResult_t request = Unicore_RESULT_RESPONSE_COMMAND_OK) const;
};

#endif // UNICORE_UM980_H
