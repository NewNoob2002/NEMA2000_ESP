#ifndef UNICORE_STRUCTS_H
#define UNICORE_STRUCTS_H

#include <stdint.h>

#define UnicoreBinarySyncA           ((uint8_t)0xAA)
#define UnicoreBinarySyncB           ((uint8_t)0x44)
#define UnicoreBinarySyncC           ((uint8_t)0xB5)
#define UnicoreASCIISyncEnd          ((uint8_t)'\n')

#define UnicoreHeaderLength          ((uint16_t)24)
#define offsetHeaderSyncA            ((uint16_t)0)
#define offsetHeaderSyncB            ((uint16_t)1)
#define offsetHeaderSyncC            ((uint16_t)2)
#define offsetHeaderCpuIdle          ((uint16_t)3)
#define offsetHeaderMessageId        ((uint16_t)4)
#define offsetHeaderMessageLength    ((uint16_t)6)
#define offsetHeaderReferenceTime    ((uint16_t)8)
#define offsetHeaderTimeStatus       ((uint16_t)9)
#define offsetHeaderWeekNumber       ((uint16_t)10)
#define offsetHeaderSecondsOfWeek    ((uint16_t)12)
#define offsetHeaderReleaseVersion   ((uint16_t)20)
#define offsetHeaderLeapSecond       ((uint16_t)21)
#define offsetHeaderOutputDelay      ((uint16_t)22)

// VERSIONB
#define messageIdVersion             ((uint16_t)37)
#define offsetVersionModuleType      ((uint16_t)0)
#define offsetVersionFirmwareVersion ((uint16_t)4)
#define offsetVersionAuth            ((uint16_t)37)
#define offsetVersionPsn             ((uint16_t)166 + 14)
#define offsetVersionEfuseID         ((uint16_t)232)
#define offsetVersionCompTime        ((uint16_t)265)

// BESTNAVB contains HPA, sats tracked/used, lat/long, RTK status, fix status
#define messageIdBestnav             ((uint16_t)2118)
#define offsetBestnavPsolStatus      ((uint16_t)0)
#define offsetBestnavPosType         ((uint16_t)4)
#define offsetBestnavLat             ((uint16_t)8)
#define offsetBestnavLon             ((uint16_t)16)
#define offsetBestnavHgt             ((uint16_t)24)
#define offsetBestnavLatDeviation    ((uint16_t)40)
#define offsetBestnavLonDeviation    ((uint16_t)44)
#define offsetBestnavHgtDeviation    ((uint16_t)48)
#define offsetBestnavSatsTracked     ((uint16_t)64)
#define offsetBestnavSatsUsed        ((uint16_t)65)
#define offsetBestnavExtSolStat      ((uint16_t)69)
#define offsetBestnavVelType         ((uint16_t)76)
#define offsetBestnavHorSpd          ((uint16_t)88)
#define offsetBestnavTrkGnd          ((uint16_t)96)
#define offsetBestnavVertSpd         ((uint16_t)104)
#define offsetBestnavVerspdStd       ((uint16_t)112)
#define offsetBestnavHorspdStd       ((uint16_t)116)

// BESTNAVXYZB
#define messageIdBestnavXyz          ((uint16_t)240)
#define offsetBestnavXyzPsolStatus   ((uint16_t)0)
#define offsetBestnavXyzPosType      ((uint16_t)4)
#define offsetBestnavXyzPX           ((uint16_t)8)
#define offsetBestnavXyzPY           ((uint16_t)16)
#define offsetBestnavXyzPZ           ((uint16_t)24)
#define offsetBestnavXyzPXDeviation  ((uint16_t)32)
#define offsetBestnavXyzPYDeviation  ((uint16_t)36)
#define offsetBestnavXyzPZDeviation  ((uint16_t)40)
#define offsetBestnavXyzSatsTracked  ((uint16_t)104)
#define offsetBestnavXyzSatsUsed     ((uint16_t)105)
#define offsetBestnavXyzExtSolStat   ((uint16_t)109)

// RECTIMEB for time/date
#define messageIdRectime             ((uint16_t)102)
#define offsetRectimeClockStatus     ((uint16_t)0)
#define offsetRectimeOffset          ((uint16_t)4)
#define offsetRectimeOffsetStd       ((uint16_t)12)
#define offsetRectimeUtcYear         ((uint16_t)28)
#define offsetRectimeUtcMonth        ((uint16_t)32)
#define offsetRectimeUtcDay          ((uint16_t)33)
#define offsetRectimeUtcHour         ((uint16_t)34)
#define offsetRectimeUtcMinute       ((uint16_t)35)
#define offsetRectimeUtcMillisecond  ((uint16_t)36)
#define offsetRectimeUtcStatus       ((uint16_t)40)

typedef enum {
    Unicore_RESULT_SEND_COMMAND_OK = 0,
    Unicore_RESULT_TIMEOUT_START_BYTE,
    Unicore_RESULT_TIMEOUT_DATA_BYTE,
    Unicore_RESULT_TIMEOUT_END_BYTE,
    Unicore_RESULT_TIMEOUT_RESPONSE,
    Unicore_RESULT_WRONG_COMMAND,
    Unicore_RESULT_WRONG_MESSAGE_ID,
    Unicore_RESULT_BAD_START_BYTE,
    Unicore_RESULT_BAD_CHECKSUM,
    Unicore_RESULT_BAD_CRC,
    Unicore_RESULT_MISSING_CRC,
    Unicore_RESULT_TIMEOUT,
    Unicore_RESULT_RESPONSE_OVERFLOW,
    Unicore_RESULT_RESPONSE_COMMAND_OK,
    Unicore_RESULT_RESPONSE_COMMAND_ERROR,
    Unicore_RESULT_RESPONSE_COMMAND_WAITING,
    Unicore_RESULT_RESPONSE_COMMAND_CONFIG,
    Unicore_RESULT_CONFIG_PRESENT,
} UnicoreResult_t;

typedef struct {

    double latitude;
    double longitude;
    double altitude;
    double horizontalSpeed;
    double verticalSpeed;
    double trackGround;

    float latitudeDeviation;
    float longitudeDeviation;
    float heightDeviation;
    float horizontalSpeedDeviation;
    float verticalSpeedDeviation;
    // 0 = None, 1 = FixedPos, 8 = DopplerVelocity, 16 = Single, ...
    uint8_t positionType;
    // 0 = None, 1 = FixedPos, 8 = DopplerVelocity, 16 = Single, ...
    uint8_t velocityType;
    // 0 = Solution computed, 1 = Insufficient observation, 3 = No convergence, 4 = Covariance trace
    uint8_t solutionStatus;

    uint8_t satellitesTracked;
    uint8_t satellitesUsed;

    uint8_t rtkSolution;
    uint8_t pseudorangeCorrection;
} UNICORE_BESTNAV_data_t;

typedef struct {
    // ubxAutomaticFlags automaticFlags;
    UNICORE_BESTNAV_data_t data;
    void (*callbackPointerPtr)(UNICORE_BESTNAV_data_t*);
    UNICORE_BESTNAV_data_t* callbackData;
} UNICORE_BESTNAV_t;

typedef struct {
    double ecefX;
    double ecefY;
    double ecefZ;
    float ecefXDeviation;
    float ecefYDeviation;
    float ecefZDeviation;
} UNICORE_BESTNAVXYZ_data_t;

typedef struct {
    // ubxAutomaticFlags automaticFlags;
    UNICORE_BESTNAVXYZ_data_t data;
    void (*callbackPointerPtr)(UNICORE_BESTNAVXYZ_data_t*);
    UNICORE_BESTNAVXYZ_data_t* callbackData;
} UNICORE_BESTNAVXYZ_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint16_t millisecond;
    uint8_t timeStatus; // 0 = valid, 3 = invalid
    uint8_t dateStatus; // 0 = Invalid, 1 = valid, 2 = leap second warning
    double timeOffset;
    double timeDeviation;
} UNICORE_RECTIME_data_t;

typedef struct {
    // ubxAutomaticFlags automaticFlags;
    UNICORE_RECTIME_data_t data;
    void (*callbackPointerPtr)(UNICORE_RECTIME_data_t*);
    UNICORE_RECTIME_data_t* callbackData;
} UNICORE_RECTIME_t;

// #VERSION,98,GPS,UNKNOWN,1,711000,0,0,18,144;UM980,R4.10Build7923,HRPT00-S10C-P,2310415000001-MD22B1224961040,ff3bd496fd7ca68b,2022/09/28*55f61e51
typedef struct {
    uint8_t modelType;
    char swVersion[33 + 1];    // Add terminator
    char serialNumber[15 + 1]; // Add terminator
    char efuseID[33 + 1];      // Add terminator
    char compileTime[43 + 1];  // Add terminator
} UNICORE_VERSION_data_t;

typedef struct {
    // ubxAutomaticFlags automaticFlags;
    UNICORE_VERSION_data_t data;
} UNICORE_VERSION_t;

#endif