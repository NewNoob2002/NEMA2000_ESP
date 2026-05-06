#ifndef NMEA0183_TO_N2K_H_
#define NMEA0183_TO_N2K_H_

#include <stddef.h>
#include <stdint.h>

#include "N2kMsg.h"
#include "N2kTypes.h"
#include "NMEA2000.h"

struct SEMP_PARSE_STATE;

struct tGatewayGnssData {
    bool HasRmc = false;
    bool HasGga = false;
    bool PositionValid = false;
    bool TimeValid = false;
    bool DateValid = false;
    bool SpeedCourseValid = false;
    bool FixValid = false;

    double Latitude = N2kDoubleNA;
    double Longitude = N2kDoubleNA;
    double Sog = N2kDoubleNA;
    double Cog = N2kDoubleNA;
    double SecondsSinceMidnight = N2kDoubleNA;
    uint16_t DaysSince1970 = N2kUInt16NA;
    double Altitude = N2kDoubleNA;
    double GeoidalSeparation = N2kDoubleNA;
    double Hdop = N2kDoubleNA;
    uint8_t Satellites = N2kUInt8NA;
    tN2kGNSSmethod GnssMethod = N2kGNSSm_Unavailable;
};

struct tGatewayNmea0183Stats {
    uint32_t ParsedSentences;
    uint32_t AcceptedRmc;
    uint32_t AcceptedGga;
    uint32_t UnsupportedSentences;
    uint32_t BadChecksum;
    uint32_t InvalidFields;
    uint32_t GeneratedMessages;
    char LastSentence[8];
    char LastError[128];
};

struct tGatewayN2kMessages {
    tN2kMsg LatLonRapid;
    tN2kMsg CogSogRapid;
    tN2kMsg Gnss;
};

using tGatewayN2kMessageCallback = void (*)(const tGatewayN2kMessages& messages, void* userContext);

class tGatewayNmea0183Parser {
public:
    tGatewayNmea0183Parser();
    ~tGatewayNmea0183Parser();

    bool Begin();
    void End();
    bool IsStarted() const;

    void FeedByte(uint8_t byte);
    void FeedBytes(const uint8_t* bytes, size_t length);

    void SetMessageCallback(tGatewayN2kMessageCallback callback, void* userContext);
    bool TakeMessages(tGatewayN2kMessages& messages);
    bool BuildMessages(tGatewayN2kMessages& messages) const;
    void ResetFix();

    const tGatewayGnssData& GetGnssData() const;
    const tGatewayNmea0183Stats& GetStats() const;

    void HandleEndOfMessage(SEMP_PARSE_STATE* parse, uint16_t type);
    bool HandleBadChecksum(SEMP_PARSE_STATE* parse);

private:
    SEMP_PARSE_STATE* Parser;
    tGatewayGnssData Gnss;
    tGatewayNmea0183Stats Stats;
    tGatewayN2kMessages PendingMessages;
    bool HasPendingMessages;
    tGatewayN2kMessageCallback MessageCallback;
    void* MessageCallbackContext;
};

void ConfigureGatewayNmea2000(tNMEA2000& nmea2000);
bool BuildGatewayN2kMessages(const tGatewayGnssData& gnss, tGatewayN2kMessages& messages);

#endif // NMEA0183_TO_N2K_H_
