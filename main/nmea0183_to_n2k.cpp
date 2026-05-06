#include "nmea0183_to_n2k.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "N2kMessages.h"
#include "SparkFun_Extensible_Message_Parser.h"
#include "esp_log.h"

namespace {

constexpr const char* kTag = "nmea0183_to_n2k";

const char* const kParserNames[] = {"NMEA"};
const SEMP_PARSE_ROUTINE kParsers[] = {sempNmeaPreamble};

const unsigned long kGatewayTransmitMessages[] = {
    129025L,
    129026L,
    129029L,
    0,
};

enum class tNmeaSentenceResult {
    Handled,
    Unsupported,
    Invalid,
};

tGatewayNmea0183Parser* gActiveParser = nullptr;

/**
 * @brief Set the Last Error object
 * 
 * @param stats 
 * @param message 
 */
void
SetLastError(tGatewayNmea0183Stats& stats, const char* message) {
    snprintf(stats.LastError, sizeof(stats.LastError), "%s", message == nullptr ? "" : message);
}

/**
 * @brief Set the Last Sentence object
 * 
 * @param stats 
 * @param sentenceName 
 */
void
SetLastSentence(tGatewayNmea0183Stats& stats, const char* sentenceName) {
    snprintf(stats.LastSentence, sizeof(stats.LastSentence), "%s", sentenceName == nullptr ? "" : sentenceName);
}

/**
 * @brief Checks if a year is a leap year
 * 
 * @param year 
 * @return true 
 * @return false 
 */
bool
IsLeapYear(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

/**
 * @brief Calculates the number of days since January 1, 1970
 * 
 * @param year 
 * @param month 
 * @param day 
 * @return uint16_t 
 */
uint16_t
DaysSince1970(int year, int month, int day) {
    static const int kDaysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    int days = 0;
    for (int y = 1970; y < year; y++) {
        days += IsLeapYear(y) ? 366 : 365;
    }

    days += kDaysBeforeMonth[month - 1];
    if (month > 2 && IsLeapYear(year)) {
        days++;
    }
    days += day - 1;
    return static_cast<uint16_t>(days);
}

bool
ParseDoubleField(const char* field, double& value) {
    if (field == nullptr || field[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    const double parsed = strtod(field, &end);
    if (end == field || *end != '\0') {
        return false;
    }

    value = parsed;
    return true;
}

bool
ParseUint8Field(const char* field, uint8_t& value) {
    if (field == nullptr || field[0] == '\0') {
        return false;
    }

    char* end = nullptr;
    const long parsed = strtol(field, &end, 10);
    if (end == field || *end != '\0' || parsed < 0 || parsed > 255) {
        return false;
    }

    value = static_cast<uint8_t>(parsed);
    return true;
}

bool
ParseLatLon(const char* valueField, const char* hemisphereField, bool longitude, double& value) {
    double raw = 0.0;
    if (!ParseDoubleField(valueField, raw) || hemisphereField == nullptr || hemisphereField[0] == '\0') {
        return false;
    }

    const int degrees = static_cast<int>(raw / 100.0);
    const double minutes = raw - static_cast<double>(degrees * 100);
    if ((!longitude && degrees > 90) || (longitude && degrees > 180) || minutes < 0.0 || minutes >= 60.0) {
        return false;
    }

    value = static_cast<double>(degrees) + minutes / 60.0;
    if (hemisphereField[0] == 'S' || hemisphereField[0] == 'W') {
        value = -value;
    } else if (hemisphereField[0] != 'N' && hemisphereField[0] != 'E') {
        return false;
    }

    return true;
}

bool
ParseUtcSeconds(const char* field, double& secondsSinceMidnight) {
    double raw = 0.0;
    if (!ParseDoubleField(field, raw) || raw < 0.0) {
        return false;
    }

    const int hours = static_cast<int>(raw / 10000.0);
    const int minutes = static_cast<int>((raw - hours * 10000) / 100.0);
    const double seconds = raw - hours * 10000 - minutes * 100;
    if (hours > 23 || minutes > 59 || seconds < 0.0 || seconds >= 60.0) {
        return false;
    }

    secondsSinceMidnight = hours * 3600.0 + minutes * 60.0 + seconds;
    return true;
}

bool
ParseDate(const char* field, uint16_t& daysSince1970) {
    if (field == nullptr || strlen(field) != 6) {
        return false;
    }

    char part[3] = {};
    part[0] = field[0];
    part[1] = field[1];
    const int day = atoi(part);
    part[0] = field[2];
    part[1] = field[3];
    const int month = atoi(part);
    part[0] = field[4];
    part[1] = field[5];
    const int year2 = atoi(part);
    const int year = (year2 >= 80) ? (1900 + year2) : (2000 + year2);

    if (year < 1970 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    static const int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxDay = kDaysInMonth[month - 1];
    if (month == 2 && IsLeapYear(year)) {
        maxDay++;
    }
    if (day > maxDay) {
        return false;
    }

    daysSince1970 = DaysSince1970(year, month, day);
    return true;
}

int
SplitCsv(char* text, char* fields[], int maxFields) {
    int count = 0;
    char* cursor = text;
    while (count < maxFields) {
        fields[count++] = cursor;
        char* comma = strchr(cursor, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

bool
SentenceHasType(const char* sentenceName, const char* suffix) {
    if (sentenceName == nullptr || suffix == nullptr) {
        return false;
    }

    const size_t sentenceLen = strlen(sentenceName);
    const size_t suffixLen = strlen(suffix);
    return sentenceLen >= suffixLen && strcmp(sentenceName + sentenceLen - suffixLen, suffix) == 0;
}

bool
HandleRmcSentence(char* fields[], int count, tGatewayGnssData& gnss) {
    if (count < 10 || fields[2][0] != 'A') {
        return false;
    }

    double latitude = 0.0;
    double longitude = 0.0;
    double seconds = 0.0;
    double sogKnots = 0.0;
    double cogDegrees = 0.0;
    uint16_t days = 0;

    if (!ParseUtcSeconds(fields[1], seconds) || !ParseLatLon(fields[3], fields[4], false, latitude)
        || !ParseLatLon(fields[5], fields[6], true, longitude) || !ParseDoubleField(fields[7], sogKnots)
        || !ParseDoubleField(fields[8], cogDegrees) || !ParseDate(fields[9], days)) {
        return false;
    }

    gnss.HasRmc = true;
    gnss.PositionValid = true;
    gnss.TimeValid = true;
    gnss.DateValid = true;
    gnss.SpeedCourseValid = true;
    gnss.Latitude = latitude;
    gnss.Longitude = longitude;
    gnss.SecondsSinceMidnight = seconds;
    gnss.DaysSince1970 = days;
    gnss.Sog = KnotsToms(sogKnots);
    gnss.Cog = DegToRad(cogDegrees);
    return true;
}

bool
HandleGgaSentence(char* fields[], int count, tGatewayGnssData& gnss) {
    if (count < 12) {
        return false;
    }

    uint8_t fixQuality = 0;
    if (!ParseUint8Field(fields[6], fixQuality) || fixQuality == 0) {
        return false;
    }

    double seconds = 0.0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double geoidalSeparation = 0.0;
    double hdop = 0.0;
    uint8_t satellites = 0;

    if (!ParseUtcSeconds(fields[1], seconds) || !ParseLatLon(fields[2], fields[3], false, latitude)
        || !ParseLatLon(fields[4], fields[5], true, longitude) || !ParseUint8Field(fields[7], satellites)
        || !ParseDoubleField(fields[8], hdop) || !ParseDoubleField(fields[9], altitude)
        || !ParseDoubleField(fields[11], geoidalSeparation)) {
        return false;
    }

    gnss.HasGga = true;
    gnss.FixValid = true;
    gnss.PositionValid = true;
    gnss.TimeValid = true;
    gnss.Latitude = latitude;
    gnss.Longitude = longitude;
    gnss.SecondsSinceMidnight = seconds;
    gnss.Altitude = altitude;
    gnss.GeoidalSeparation = geoidalSeparation;
    gnss.Hdop = hdop;
    gnss.Satellites = satellites;
    gnss.GnssMethod = (fixQuality == 2) ? N2kGNSSm_DGNSS : N2kGNSSm_GNSSfix;
    return true;
}

tNmeaSentenceResult
DispatchNmeaSentence(const char* sentence, const char* sentenceName, tGatewayGnssData& gnss) {
    char work[128] = {};
    snprintf(work, sizeof(work), "%s", sentence == nullptr ? "" : sentence);

    char* asterisk = strchr(work, '*');
    if (asterisk == nullptr) {
        return tNmeaSentenceResult::Invalid;
    }
    *asterisk = '\0';

    char* payload = work;
    if (payload[0] == '$') {
        payload++;
    }

    char* fields[24] = {};
    const int count = SplitCsv(payload, fields, 24);
    if (count == 0) {
        return tNmeaSentenceResult::Invalid;
    }

    if (SentenceHasType(sentenceName, "RMC")) {
        return HandleRmcSentence(fields, count, gnss) ? tNmeaSentenceResult::Handled : tNmeaSentenceResult::Invalid;
    }

    if (SentenceHasType(sentenceName, "GGA")) {
        return HandleGgaSentence(fields, count, gnss) ? tNmeaSentenceResult::Handled : tNmeaSentenceResult::Invalid;
    }

    return tNmeaSentenceResult::Unsupported;
}

void
NmeaEomCallback(SEMP_PARSE_STATE* parse, uint16_t type) {
    if (gActiveParser != nullptr) {
        gActiveParser->HandleEndOfMessage(parse, type);
    }
}

bool
NmeaBadCrcCallback(SEMP_PARSE_STATE* parse) {
    if (gActiveParser != nullptr) {
        return gActiveParser->HandleBadChecksum(parse);
    }
    return true;
}

} // namespace

tGatewayNmea0183Parser::tGatewayNmea0183Parser()
    : Parser(nullptr), HasPendingMessages(false), MessageCallback(nullptr), MessageCallbackContext(nullptr) {
    ResetFix();
    memset(&Stats, 0, sizeof(Stats));
}

tGatewayNmea0183Parser::~tGatewayNmea0183Parser() { End(); }

bool
tGatewayNmea0183Parser::Begin() {
    if (Parser != nullptr) {
        return true;
    }
    if (gActiveParser != nullptr && gActiveParser != this) {
        ESP_LOGE(kTag, "only one NMEA0183 parser instance can be active");
        return false;
    }

    Parser = sempBeginParser(kParsers, sizeof(kParsers) / sizeof(kParsers[0]), kParserNames,
                             sizeof(kParserNames) / sizeof(kParserNames[0]), 0, 128, NmeaEomCallback,
                             "nmea0183-gateway", nullptr, nullptr, NmeaBadCrcCallback);
    if (Parser == nullptr) {
        SetLastError(Stats, "failed to allocate NMEA parser");
        return false;
    }

    gActiveParser = this;
    return true;
}

void
tGatewayNmea0183Parser::End() {
    if (Parser != nullptr) {
        sempStopParser(&Parser);
    }
    if (gActiveParser == this) {
        gActiveParser = nullptr;
    }
}

bool
tGatewayNmea0183Parser::IsStarted() const {
    return Parser != nullptr;
}

void
tGatewayNmea0183Parser::FeedByte(uint8_t byte) {
    if (Parser != nullptr) {
        sempParseNextByte(Parser, byte);
    }
}

void
tGatewayNmea0183Parser::FeedBytes(const uint8_t* bytes, size_t length) {
    if (bytes == nullptr) {
        return;
    }

    for (size_t i = 0; i < length; i++) {
        FeedByte(bytes[i]);
    }
}

void
tGatewayNmea0183Parser::SetMessageCallback(tGatewayN2kMessageCallback callback, void* userContext) {
    MessageCallback = callback;
    MessageCallbackContext = userContext;
}

bool
tGatewayNmea0183Parser::TakeMessages(tGatewayN2kMessages& messages) {
    if (!HasPendingMessages) {
        return false;
    }

    messages = PendingMessages;
    HasPendingMessages = false;
    return true;
}

bool
tGatewayNmea0183Parser::BuildMessages(tGatewayN2kMessages& messages) const {
    return BuildGatewayN2kMessages(Gnss, messages);
}

void
tGatewayNmea0183Parser::ResetFix() {
    Gnss = tGatewayGnssData();
    HasPendingMessages = false;
}

const tGatewayGnssData&
tGatewayNmea0183Parser::GetGnssData() const {
    return Gnss;
}

const tGatewayNmea0183Stats&
tGatewayNmea0183Parser::GetStats() const {
    return Stats;
}

void
tGatewayNmea0183Parser::HandleEndOfMessage(SEMP_PARSE_STATE* parse, uint16_t type) {
    (void)type;
    if (parse == nullptr) {
        return;
    }

    const char* sentenceName = sempNmeaGetSentenceName(parse);
    const char* sentence = reinterpret_cast<const char*>(parse->buffer);

    Stats.ParsedSentences++;
    SetLastSentence(Stats, sentenceName);

    switch (DispatchNmeaSentence(sentence, sentenceName, Gnss)) {
        case tNmeaSentenceResult::Handled:
            if (SentenceHasType(sentenceName, "RMC")) {
                Stats.AcceptedRmc++;
            } else if (SentenceHasType(sentenceName, "GGA")) {
                Stats.AcceptedGga++;
            }
            SetLastError(Stats, "");
            if (BuildGatewayN2kMessages(Gnss, PendingMessages)) {
                HasPendingMessages = true;
                Stats.GeneratedMessages++;
                if (MessageCallback != nullptr) {
                    MessageCallback(PendingMessages, MessageCallbackContext);
                }
            }
            break;

        case tNmeaSentenceResult::Unsupported:
            Stats.UnsupportedSentences++;
            SetLastError(Stats, "unsupported NMEA sentence");
            break;

        case tNmeaSentenceResult::Invalid:
            Stats.InvalidFields++;
            SetLastError(Stats, "invalid NMEA sentence fields");
            break;
    }
}

bool
tGatewayNmea0183Parser::HandleBadChecksum(SEMP_PARSE_STATE* parse) {
    (void)parse;
    Stats.BadChecksum++;
    SetLastError(Stats, "NMEA checksum failed");
    return true;
}

void
ConfigureGatewayNmea2000(tNMEA2000& nmea2000) {
    nmea2000.SetProductInformation("00000001", 100, "NMEA0183 GNSS gateway", "0.1.0", "0.1.0");
    nmea2000.SetDeviceInformation(1, 132, 25, 2046);
    nmea2000.ExtendTransmitMessages(kGatewayTransmitMessages);
    nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
}

bool
BuildGatewayN2kMessages(const tGatewayGnssData& gnss, tGatewayN2kMessages& messages) {
    if (!gnss.HasRmc || !gnss.HasGga || !gnss.PositionValid || !gnss.TimeValid || !gnss.DateValid
        || !gnss.SpeedCourseValid || !gnss.FixValid) {
        return false;
    }

    SetN2kLatLonRapid(messages.LatLonRapid, gnss.Latitude, gnss.Longitude);
    SetN2kCOGSOGRapid(messages.CogSogRapid, 1, N2khr_true, gnss.Cog, gnss.Sog);
    SetN2kGNSS(messages.Gnss, 1, gnss.DaysSince1970, gnss.SecondsSinceMidnight, gnss.Latitude, gnss.Longitude,
               gnss.Altitude, N2kGNSSt_GPS, gnss.GnssMethod, gnss.Satellites, gnss.Hdop, N2kDoubleNA,
               gnss.GeoidalSeparation, 0);
    return true;
}
