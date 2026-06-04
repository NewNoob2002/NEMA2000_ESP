#ifndef NMEA0183_TO_N2K_H_
#define NMEA0183_TO_N2K_H_

#include <stdint.h>

#include "N2kMsg.h"
#include "N2kTypes.h"
#include "NMEA2000.h"

class UnicoreUM980;

struct tGatewayGnssData {
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

struct tGatewayN2kMessages {
    tN2kMsg LatLonRapid;
    tN2kMsg CogSogRapid;
    tN2kMsg Gnss;
};

void ConfigureGatewayNmea2000(tNMEA2000& nmea2000);
bool ReadGatewayGnssData(const UnicoreUM980& gnss, tGatewayGnssData& data);
bool BuildGatewayN2kMessages(const tGatewayGnssData& gnss, tGatewayN2kMessages& messages);

#endif // NMEA0183_TO_N2K_H_
