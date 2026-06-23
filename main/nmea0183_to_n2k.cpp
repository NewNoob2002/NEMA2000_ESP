#include "nmea0183_to_n2k.h"

#include "N2kMessages.h"
#include "Unicore_UM980.h"
#include "esp_log.h"

namespace {

const char* kTag = "[nmea0183_to_n2k]";

const unsigned long kGatewayTransmitMessages[] = {
    129025L,
    129026L,
    129029L,
    0,
};

constexpr uint16_t kMaxGnssFixAgeMs = 3000;

bool
IsLeapYear(int year) {
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

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
IsValidDate(int year, int month, int day) {
    if (year < 1970 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    static const int kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxDay = kDaysInMonth[month - 1];
    if (month == 2 && IsLeapYear(year)) {
        maxDay++;
    }
    return day <= maxDay;
}

tN2kGNSSmethod
GetN2kGnssMethod(const UnicoreUM980& gnss) {
    if (gnss.isRTKFix()) {
        return N2kGNSSm_RTKFixed;
    }
    if (gnss.isRTKFloat()) {
        return N2kGNSSm_RTKFloat;
    }
    if (gnss.isDgpsFixed()) {
        return N2kGNSSm_DGNSS;
    }
    if (gnss.isFixed()) {
        return N2kGNSSm_GNSSfix;
    }
    return N2kGNSSm_noGNSS;
}

} // namespace

void
ConfigureGatewayNmea2000(tNMEA2000& nmea2000) {
    nmea2000.SetProductInformation("00000001", 100, "GNSS NMEA2000 gateway", "0.1.0", "0.1.0");
    nmea2000.SetDeviceInformation(1, 132, 25, 2046);
    nmea2000.ExtendTransmitMessages(kGatewayTransmitMessages);
    nmea2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
    ESP_LOGI(kTag, "NMEA2000 gateway configured");
}

bool
ReadGatewayGnssData(const UnicoreUM980& gnss, tGatewayGnssData& data) {
    data = tGatewayGnssData();

    data.GnssMethod = GetN2kGnssMethod(gnss);
    data.Satellites = gnss.getSatellitesUsed();
    data.FixValid = gnss.isFixed() && gnss.getFixAgeMilliseconds() <= kMaxGnssFixAgeMs;
    data.Hdop = N2kDoubleNA;
    data.GeoidalSeparation = N2kDoubleNA;

    data.TimeValid = gnss.isValidTime();
    data.DateValid = gnss.isValidDate();

    if (data.FixValid) {
        data.Latitude = gnss.getLatitude();
        data.Longitude = gnss.getLongitude();
        data.Altitude = gnss.getAltitude();
        data.Sog = gnss.getHorizontalSpeed();
        data.Cog = DegToRad(gnss.getTrackGround());
        data.PositionValid = true;
        data.SpeedCourseValid = true;
    }

    if (data.TimeValid) {
        data.SecondsSinceMidnight =
            static_cast<double>(gnss.getHour()) * 3600.0 + static_cast<double>(gnss.getMinute()) * 60.0
            + static_cast<double>(gnss.getSecond()) + static_cast<double>(gnss.getMillisecond()) / 1000.0;
    }

    if (data.DateValid) {
        const int year = gnss.getYear();
        const int month = gnss.getMonth();
        const int day = gnss.getDay();
        if (IsValidDate(year, month, day)) {
            data.DaysSince1970 = DaysSince1970(year, month, day);
        } else {
            data.DateValid = false;
        }
    }

    return data.PositionValid && data.TimeValid && data.DateValid && data.SpeedCourseValid && data.FixValid;
}

bool
BuildGatewayN2kMessages(const tGatewayGnssData& gnss, tGatewayN2kMessages& messages) {
    SetN2kLatLonRapid(messages.LatLonRapid, gnss.Latitude, gnss.Longitude);
    SetN2kCOGSOGRapid(messages.CogSogRapid, 1, N2khr_true, gnss.Cog, gnss.Sog);
    SetN2kGNSS(messages.Gnss, 1, gnss.DaysSince1970, gnss.SecondsSinceMidnight, gnss.Latitude, gnss.Longitude,
               gnss.Altitude, N2kGNSSt_GPS, gnss.GnssMethod, gnss.Satellites, gnss.Hdop, N2kDoubleNA,
               gnss.GeoidalSeparation, 0);
    return true;
}
