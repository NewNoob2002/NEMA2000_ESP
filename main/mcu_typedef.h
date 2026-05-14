#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "CompileConfig.h"
#include "FreeRTOSConfig.h"

#define PIN_UNDEFINED                   0xFF
#define INCHES_IN_A_METER               (float)39.37007874
#define FEET_IN_A_METER                 (float)3.280839895

#define HOURS_IN_A_DAY                  24L
#define MINUTES_IN_AN_HOUR              60L
#define SECONDS_IN_A_MINUTE             60L
#define MILLISECONDS_IN_A_SECOND        1000L
#define MILLISECONDS_IN_A_MINUTE        (SECONDS_IN_A_MINUTE * MILLISECONDS_IN_A_SECOND)
#define MILLISECONDS_IN_AN_HOUR         (MINUTES_IN_AN_HOUR * MILLISECONDS_IN_A_MINUTE)
#define MILLISECONDS_IN_A_DAY           (HOURS_IN_A_DAY * MILLISECONDS_IN_AN_HOUR)

#define SECONDS_IN_AN_HOUR              (MINUTES_IN_AN_HOUR * SECONDS_IN_A_MINUTE)
#define SECONDS_IN_A_DAY                (HOURS_IN_A_DAY * SECONDS_IN_AN_HOUR)

// Define the index values into the parserTable
#define RTK_NMEA_PARSER_INDEX           0
#define RTK_UNICORE_HASH_PARSER_INDEX   1
#define RTK_RTCM_PARSER_INDEX           2
// #define RTK_UBLOX_PARSER_INDEX 3
#define RTK_UNICORE_BINARY_PARSER_INDEX 3

#define WIFI_SSID_LENGTH                32
#define WIFI_PASSWORD_LENGTH            32
#define MAX_WIFI_NETWORKS               4

typedef uint16_t RING_BUFFER_OFFSET;

typedef enum RTK_MODE_t {
    RTK_MODE_BASE_UNDECIDED = 0x00, // 0 << 0
    RTK_MODE_BASE_FIXED = 0x01,     // 1 << 0
    RTK_MODE_ROVER = 0x02,          // 1 << 1
    RTK_MODE_BASE_SURVEY_IN = 0x04, // 1 << 2
    RTK_MODE_NTP = 0x08,            // 1 << 3
    RTK_MODE_WEB_CONFIG = 0x10,     // 1 << 4
    RTK_MODE_MAX = 0x80,            // 1 << 7
} RTK_MODE_t;

typedef enum measurementUnits {
    MEASUREMENT_UNITS_METERS = 0,
    MEASUREMENT_UNITS_FEET_INCHES,
    // Add new measurement units above this line
    MEASUREMENT_UNITS_MAX
} measurementUnits;

// System can enter a variety of states
// See statemachine diagram at:
// https://lucid.app/lucidchart/53519501-9fa5-4352-aa40-673f88ca0c9b/edit?invitationId=inv_ebd4b988-513d-4169-93fd-c291851108f8
typedef enum SystemState_t {
    STATE_ROVER_NOT_STARTED = 0, //  0
    STATE_ROVER_CONFIG_WAIT,     //  1
    STATE_ROVER_NO_FIX,          //  2
    STATE_ROVER_FIX,             //  3
    STATE_ROVER_RTK_FLOAT,       //  4
    STATE_ROVER_RTK_FIX,         //  5

    STATE_BASE_CASTER_NOT_STARTED,  //  6, Set override flag
    STATE_BASE_ASSIST_NOT_STARTED,  //  7
    STATE_BASE_NOT_STARTED,         //  8
    STATE_BASE_CONFIG_WAIT,         //  9
    STATE_BASE_TEMP_SETTLE,         // 10, User has indicated base, but current pos accuracy is too low
    STATE_BASE_TEMP_SURVEY_STARTED, // 11
    STATE_BASE_TEMP_TRANSMITTING,   // 12
    STATE_BASE_FIXED_NOT_STARTED,   // 13
    STATE_BASE_FIXED_TRANSMITTING,  // 14

    STATE_WEB_CONFIG_NOT_STARTED,      // 16
    STATE_WEB_CONFIG_WAIT_FOR_NETWORK, // 17
    STATE_WEB_CONFIG,                  // 18
    STATE_PROFILE,                     // 19
#ifdef COMPILE_NTP
    STATE_NTPSERVER_NOT_STARTED, // 23
    STATE_NTPSERVER_NO_SYNC,     // 24
    STATE_NTPSERVER_SYNC,        // 25
#endif
    STATE_SHUTDOWN,

    STATE_NOT_SET,
} SystemState_t;

// RTK mode structure
typedef struct RTK_MODE_ENTRY_T {
    const char* modeName;
    SystemState_t first;
    SystemState_t last;
} RTK_MODE_ENTRY_T;

// We may receive a command or the user may change a setting that needs to modify the configuration of the GNSS receiver
// Because this can take time, we group all the changes together and re-configure the receiver once the user has exited
// the menu system, closed the Web Config, or the CLI is closed.
typedef enum GNSS_CONFIG_ACTIONS_T {
    GNSS_CONFIG_ONCE, // Settings specific to a receiver that don't fit into other setting categories
    GNSS_CONFIG_ROVER,
    GNSS_CONFIG_BASE,        // Apply any settings before the start of survey-in or fixed base
    GNSS_CONFIG_BASE_SURVEY, // Start survey in base
    GNSS_CONFIG_BASE_FIXED,  // Start fixed base
    GNSS_CONFIG_BAUD_RATE_RADIO,
    GNSS_CONFIG_BAUD_RATE_DATA,
    GNSS_CONFIG_FIX_RATE,
    GNSS_CONFIG_CONSTELLATION, // Turn on/off a constellation
    GNSS_CONFIG_ELEVATION,
    GNSS_CONFIG_CN0,
    GNSS_CONFIG_PPS,
    GNSS_CONFIG_MODEL,
    GNSS_CONFIG_MESSAGE_RATE_NMEA,       // Update NMEA message rates
    GNSS_CONFIG_MESSAGE_RATE_RTCM_ROVER, // Update RTCM Rover message rates
    GNSS_CONFIG_MESSAGE_RATE_RTCM_BASE,  // Update RTCM Base message rates
    GNSS_CONFIG_MESSAGE_RATE_OTHER,      // Update any other messages (UBX, PQTM, etc)
    GNSS_CONFIG_PPP,                     // Enable/disable HAS E6 capabilities
    GNSS_CONFIG_MULTIPATH,
    GNSS_CONFIG_TILT,            // Enable/disable any output needed for tilt compensation
    GNSS_CONFIG_EXT_CORRECTIONS, // Enable / disable corrections protocol(s) on the Radio External port
    GNSS_CONFIG_LOGGING,         // Enable / disable logging
    GNSS_CONFIG_SAVE,            // Indicates current settings be saved to GNSS receiver NVM
    GNSS_CONFIG_RESET,           // Indicates receiver needs resetting

    // Add new entries above here
    GNSS_CONFIG_MAX,
} GNSS_CONFIG_ACTIONS_T;

// Product Variant used as part of device ID and whitelists. Do not reorder.
typedef enum {
    RTK_S20 = 0, // 0x00
    RTK_UNKNOWN
} ProductVariant;

// Branding support
typedef enum {
    BRAND_SINGULARXYZ = 0,
    // Add new brands above this line
    BRAND_NUM
} RTKBrands_e;

typedef struct productProperties_t {
    ProductVariant productVariant;
    const RTKBrands_e brand;
    const char name[16];
    char displayName[32];
    const char filePrefix[32];
    const bool platformProvision;
    const char rtkPrefix[16];
    const char productPlanUID[16];
    const SystemState_t defaultSystemState;
} productProperties_t;

// Corrections Priority
typedef enum correctionsSource_e {
    // Change the order of these to set the default priority. First (0) is highest
    // 0, 100 m Baseline, Data goes direct from RADIO connector to ZED - or X5. How to disable / enable it? Via port protocol?
    CORR_RADIO_EXT = 0,
    CORR_ESPNOW,      // 1, 100 m Baseline, ESPNOW.ino
    CORR_RADIO_LORA,  // 2,   1 km Baseline, Torch goes via ESP32, Facet FP goes via SW4 to GNSS
    CORR_BLUETOOTH,   // 3,  10+km Baseline, Tasks.ino (sendGnssBuffer)
    CORR_USB,         // 4,                  menuMain.ino (terminalUpdate)
    CORR_TCP,         // 5,  10+km Baseline, NtripClient.ino
    CORR_PPP_HAS_B2B, // 6, 100+km Baseline
    CORR_LBAND,       // 7, 100 km Baseline, menuPP.ino for PMP - PointPerfectLibrary.ino for PPL
    CORR_IP,          // 8, 100+km Baseline, MQTT_Client.ino
    // Add new correction sources just above this line
    CORR_NUM
} correctionsSource_e;

// User can enter fixed base coordinates in ECEF or degrees
typedef enum coordinateType_e {
    COORD_TYPE_ECEF = 0,
    COORD_TYPE_GEODETIC,
} coordinateType_e;

typedef enum PPP_MODE_e {
    PPP_MODE_DISABLE = 0,
    PPP_MODE_B2B, // 1
    PPP_MODE_HAS, // 2
    PPP_MODE_AUTO = 255
} PPP_MODE_e;

typedef enum BTState_e {
    BT_OFF = 0,
    BT_NOTCONNECTED,
    BT_CONNECTED,
} BTState_e;

typedef enum BluetoothRadioType_e {
    BLUETOOTH_RADIO_SPP = 0,
    BLUETOOTH_RADIO_BLE,
    BLUETOOTH_RADIO_SPP_AND_BLE,
    BLUETOOTH_RADIO_OFF,
} BluetoothRadioType_e;

typedef struct WiFiNetwork_t {
    char ssid[WIFI_SSID_LENGTH];
    char password[WIFI_PASSWORD_LENGTH];
} WiFiNetwork_t;

typedef enum SemaphoreFunction_e {
    FUNCTION_NOT_SET = 0,
    FUNCTION_SYNC,
    FUNCTION_WRITESD,
    FUNCTION_FILESIZE,
    FUNCTION_EVENT,
    FUNCTION_BEGINSD,
    FUNCTION_RECORDSETTINGS,
    FUNCTION_LOADSETTINGS,
    FUNCTION_MARKEVENT,
    FUNCTION_GETLINE,
    FUNCTION_REMOVEFILE,
    FUNCTION_RECORDLINE,
    FUNCTION_CREATEFILE,
    FUNCTION_ENDLOGGING,
    FUNCTION_FINDLOG,
    FUNCTION_FILELIST,
    FUNCTION_FILEMANAGER_OPEN1,
    FUNCTION_FILEMANAGER_OPEN2,
    FUNCTION_FILEMANAGER_OPEN3,
    FUNCTION_FILEMANAGER_UPLOAD1,
    FUNCTION_FILEMANAGER_UPLOAD2,
    FUNCTION_FILEMANAGER_UPLOAD3,
    FUNCTION_FILEMANAGER_DOWNLOAD1,
    FUNCTION_FILEMANAGER_DOWNLOAD2,
    FUNCTION_SDSIZECHECK,
    FUNCTION_LOG_CLOSURE,
    FUNCTION_PRINT_FILE_LIST,
    FUNCTION_NTPEVENT,
    FUNCTION_ARPWRITE,
    FUNCTION_FILE_EXISTS,
    FUNCTION_FILE_DUMP,
} SemaphoreFunction_e;

#if defined(__cplusplus)

typedef struct TaskManager_t {
    //Running flags
    volatile bool bluetoothCommandTaskRunning = false;
    volatile bool bluetoothReadTaskRunning = false;
    //Stop Requests
    volatile bool bluetoothCommandTaskStopRequest = false;
    volatile bool bluetoothReadTaskStopRequest = false;
} TaskManager_t;

#endif

typedef struct online_wifi_t {
    bool wifiEspNowOnline;    // ESP-NOW started successfully
    bool wifiEspNowRunning;   // False: stopped, True: starting, running, stopping
    bool wifiSoftApOnline;    // WiFi soft AP started successfully
    bool wifiSoftApRunning;   // False: stopped, True: starting, running, stopping
    bool wifiSoftApConnected; // False: no client connected, True: client connected
    bool wifiStationOnline;   // WiFi station started successfully
    bool wifiStationRunning;  // False: stopped, True: starting, running, stopping
} online_wifi_t;

typedef struct online_devices_t {
    bool i2c = false;
    bool littlefs = false;
    bool gnss = false;
    bool bluetooth = false;
    bool psram = false;
    bool bq40z50 = false;
    bool mp2762a = false;
    bool rtc = false;
    online_wifi_t wifi;
} online_devices_t;

// This is all the settings that can be set on RTK Product. It's recorded to NVM and the config file.
// Avoid reordering. The order of these variables is mimicked in NVM/record/parse/create/update/get
typedef struct settings_t {

    int sizeOfSettings = 0; // sizeOfSettings **must** be the first entry and must be int
    // int rtkIdentifier = RTK_IDENTIFIER; // rtkIdentifier **must** be the second entry

    //Once we detect the platform or receiver, no need to re-detect
    //ProductVariant previouslyDetectedPlatform = RTK_UNKNOWN; //Because LFS is started after deviceID, this is mute
    // gnssReceiverType_e detectedGnssReceiver = GNSS_RECEIVER_UNKNOWN;

    // Antenna
    int16_t antennaHeight_mm = 1800;    // Aka Pole length
    float antennaPhaseCenter_mm = 0.0;  // Aka ARP
    uint16_t ARPLoggingInterval_s = 10; // Log the ARP every 10 seconds - if available
    bool enableARPLogging = false;      // Log the Antenna Reference Position from RTCM 1005/1006 - if available

    // Base operation
    // CoordinateInputType coordinateInputType = COORDINATE_INPUT_TYPE_DD; // Default DD.ddddddddd
    char baseId[8] = "1234";

    double fixedAltitude = 1560.089; // m
    bool fixedBase = false;          // Use survey-in by default
    bool fixedBaseCoordinateType = COORD_TYPE_ECEF;
    double fixedEcefX = -1280206.568;
    double fixedEcefY = -4716804.403;
    double fixedEcefZ = 4086665.484;
    double fixedLat = 40.09029479;
    double fixedLong = -105.18505761;
    int observationSeconds = 60;             // Default survey in time of 60 seconds
    float observationPositionAccuracy = 5.0; // Default survey in pos accy of 5m
    float surveyInStartingAccuracy =
        1.0; // Wait for this horizontal positional accuracy in meters before starting survey in
    // Use MSM7 over MSM4: on platforms where that is possible and where it requires parameter selection
    // Needed on:
    //   LG290P (PQTMCFGRTCM)
    // Not needed on:
    //   mosaic-X5 (it has MSM4 and MSM7 message groups)
    //   ZED (it has separate messages for MSM4 vs. MSM7)
    //   UM980 (it has separate messages for MSM4 vs. MSM7)
    bool useMSM7 = false;
    int rtcmMinElev = -90; // LG290P - minimum elevation for RTCM (PQTMCFGRTCM)

    // Battery
    bool enablePrintBatteryMessages = true;
    uint32_t shutdownNoChargeTimeoutMinutes = 0; // If > 0, shut down unit after timeout if not charging

    // Beeper
    bool enableBeeper = true; // Some platforms have an audible notification

    // Bluetooth
    double accessoryTimeOffset_s = -1.0; // Apply this offset to EA NMEA data via utcAdjust
    BluetoothRadioType_e bluetoothRadioType = BLUETOOTH_RADIO_SPP;
    bool clearBtPairings = true;              // Clear MFi Accessory SSP pairings
    char eaProtocol[50] = "com.sparkfun.rtk"; // MFi External Accessory protocol name
    uint16_t sppRxQueueSize = 512 * 4;
    uint16_t sppTxQueueSize = 32;

    // Corrections
    int correctionsSourcesLifetime_s = 30; // Expire a corrections source if no data is seen for this many seconds
    // CORRECTION_ID_T correctionsSourcesPriority[CORR_NUM] = {
    //     (CORRECTION_ID_T)-1}; // -1 indicates array is uninitialized, indexed by correction source ID
    bool debugCorrections = false;
    uint8_t enableExtCorrRadio = 254; // Will be initialized to true or false depending on model

    // Display
    bool enableResetDisplay = false;

    // ESP Now
    bool debugEspNow = false;
    bool enableEspNow = false;
    uint8_t espnowPeerCount = 0;
    // uint8_t espnowPeers[ESPNOW_MAX_PEERS][6] = {0}; // Contains the MAC addresses (6 bytes) of paired units

    // Ethernet
    bool enablePrintEthernetDiag = false;
    bool ethernetDHCP = true;
    // IPAddress ethernetDNS = {194, 168, 4, 100};
    // IPAddress ethernetGateway = {192, 168, 0, 1};
    // IPAddress ethernetIP = {192, 168, 0, 123};
    // IPAddress ethernetSubnet = {255, 255, 255, 0};

    // Firmware
    uint32_t autoFirmwareCheckMinutes = 24 * 60;
    bool debugFirmwareUpdate = false;
    bool enableAutoFirmwareUpdate = false;

    // GNSS
    // muxConnectionType_e dataPortChannel = MUX_GNSS_UART; // Mux default to GNSS UART
    bool debugGnss = false; // Turn on to display GNSS library debug messages
    bool enablePrintPosition = false;
    uint16_t measurementRateMs = 250; // Elapsed ms between GNSS measurements. 25ms to 65535ms. Default 4Hz.

    // GNSS UART
    uint16_t serialGNSSRxFullThreshold = 50; // RX FIFO full interrupt. Max of ~128. See pinUART2Task().
    int uartReceiveBufferSize = 1024 * 2;    // This buffer is filled automatically as the UART receives characters

    // Hardware
    uint32_t defaultDoubleTapInterval_ms = 250;
    bool enableExternalHardwareEventLogging = false; // Log when INT/TM2 pin goes low
    uint16_t spiFrequency = 16;                      // By default, use 16MHz SPI

    // HTTP
    bool debugHttpClientData = false;  // Debug the HTTP Client (ZTP) data flow
    bool debugHttpClientState = false; // Debug the HTTP Client state machine

    // IMU
    bool detectedTilt = false;
    bool testedTilt = false;

    // Log file
    bool alignedLogFiles = false; // If true, align log files as per #630
    bool enableLogging = true;    // If an SD card is present, log default sentences
    bool enablePrintLogFileMessages = false;
    bool enablePrintLogFileStatus = true;
    int maxLogLength_minutes = 60 * 24; // Default to 24 hours
    int maxLogTime_minutes = 60 * 24;   // Default to 24 hours

    // MQTT
    bool debugMqttClientData = false;  // Debug the MQTT SPARTAN data flow
    bool debugMqttClientState = false; // Debug the MQTT state machine

    // Multicast DNS
    bool mdnsEnable = true; // Allows locating of device from browser address 'rtk.local'
    char mdnsHostName[50] = "rtk";

    // Network layer
    bool debugAppleAccessory = false; // Enable debugging of the AppleAccessory
    bool debugNetworkLayer = false;   // Enable debugging of the network layer
    bool printNetworkStatus = true;   // Print network status (delays, failovers, IP address)
    // networkClient _timeout in ms (lib default is 3000). This limits write glitches to about 3.4s
    uint32_t networkClientWriteTimeout_ms = 250;

    // NTP
    bool debugNtp = false;
    bool enableNTPFile = false; // Log NTP requests to file
    uint16_t ethernetNtpPort = 123;
    uint8_t ntpPollExponent = 6; // NTPpacket::defaultPollExponent 2^6 = 64 seconds
    int8_t ntpPrecision = -20;   // NTPpacket::defaultPrecision 2^-20 = 0.95us
    char ntpReferenceId[5] = {'G', 'P', 'S', 0,
                              0}; // NTPpacket::defaultReferenceId. Ref ID is 4 chars. Add one extra for a NULL.
    uint32_t ntpRootDelay = 0;    // NTPpacket::defaultRootDelay = 0. ntpRootDelay is defined in microseconds.
                                  // ntpProcessOneRequest will convert it to seconds and fraction.
    uint32_t ntpRootDispersion =
        1000; // NTPpacket::defaultRootDispersion 1007us = 2^-16 * 66. ntpRootDispersion is defined in microseconds.
              // ntpProcessOneRequest will convert it to seconds and fraction.

    // NTRIP Client
    bool debugNtripClientRtcm = false;
    bool debugNtripClientState = false;
    bool enableNtripClient = false;
    char ntripClient_CasterHost[50] = "rtk2go.com"; // It's free...
    uint16_t ntripClient_CasterPort = 2101;
    char ntripClient_CasterUser[50] =
        "test@test.com"; // Some free casters require auth. User must provide their own email address to use RTK2Go
    char ntripClient_CasterUserPW[50] = "";
    char ntripClient_MountPoint[50] = "bldr_SparkFun1";
    char ntripClient_MountPointPW[50] = "";
    bool ntripClient_TransmitGGA = true;

    // NTRIP Server
    bool debugNtripServerRtcm = false;
    bool debugNtripServerState = false;
    bool enableNtripServer = false;
    bool enableRtcmMessageChecking = false;
    /*
    bool ntripServer_CasterEnabled[NTRIP_SERVER_MAX] = {
        false,
        false,
        false,
        false,
    };
    char ntripServer_CasterHost[NTRIP_SERVER_MAX][NTRIP_SERVER_STRING_SIZE] = // It's free...
        {
            "rtk2go.com",
            "",
            "",
            "",
    };
    uint16_t ntripServer_CasterPort[NTRIP_SERVER_MAX] = {
        2101,
        2101,
        2101,
        2101,
    };
    char ntripServer_CasterUser[NTRIP_SERVER_MAX][NTRIP_SERVER_STRING_SIZE] = {
        ""
        "",
        "",
        "",
    };
    char ntripServer_CasterUserPW[NTRIP_SERVER_MAX][NTRIP_SERVER_STRING_SIZE] = {
        "",
        "",
        "",
        "",
    };
    char ntripServer_MountPoint[NTRIP_SERVER_MAX][NTRIP_SERVER_STRING_SIZE] = {
        "bldr_dwntwn2", // NTRIP Server
        "",
        "",
        "",
    };
    char ntripServer_MountPointPW[NTRIP_SERVER_MAX][NTRIP_SERVER_STRING_SIZE] = {
        "WR5wRo4H",
        "",
        "",
        "",
    };
*/
    // OS
    // Core where hardware is started and interrupts are assigned to, 0=core, 1=Arduino
    uint8_t bluetoothInterruptsCore = 1;
    uint8_t btReadTaskCore = 1; // Core where task should run, 0=core, 1=Arduino
    // Read from BT SPP and Write to GNSS
    uint8_t btReadTaskPriority = configMAX_PRIORITIES - 20;
    bool debugMalloc = false;
    bool enableHeapReport = false; // Turn on to display free heap
    bool enablePrintIdleTime = false;
    bool enablePsram = true; // Control the use on onboard PSRAM. Used for testing behavior when PSRAM is not available.
    bool enableTaskReports = false; // Turn on to display task high water marks
    uint8_t gnssReadTaskCore = 1;   // Core where task should run, 0=core, 1=Arduino
    uint8_t gnssReadTaskPriority =
        1; // Read from GNSS and Write to circular buffer (SD, TCP, BT). 3 being the highest, and 0 being the lowest
    uint8_t gnssUartInterruptsCore =
        1;                    // Core where hardware is started and interrupts are assigned to, 0=core, 1=Arduino
    bool haltOnPanic = false; // Halt after beginVersion if the reset reason was panic
    uint8_t handleGnssDataTaskCore = 1;     // Core where task should run, 0=core, 1=Arduino
    uint8_t handleGnssDataTaskPriority = 1; // Read from the circular buffer and dole out to end points (SD, TCP, BT).
    uint8_t i2cInterruptsCore = 1; // Core where hardware is started and interrupts are assigned to, 0=core, 1=Arduino
    uint8_t measurementScale = MEASUREMENT_UNITS_METERS;
    bool printBootTimes = false; // Print times and deltas during boot
    bool printPartitionTable = false;
    bool printTaskStartStop = false;
    uint16_t psramMallocLevel =
        40; // By default, push as much as possible to PSRAM. Needed to do secure WiFi (MQTT) + BT + PPL
    uint32_t rebootMinutes = 0; // Disabled, reboots after uptime reaches this number of minutes
    int resetCount = 0;

    // Periodic Display
    // PeriodicDisplay_t periodicDisplay = (PeriodicDisplay_t)0; //Turn off all periodic debug displays by default.
    uint32_t periodicDisplayInterval = 15 * 1000;

    // Point Perfect
    bool autoKeyRenewal = true;           // Attempt to get keys if we get under 28 days from the expiration date
    bool debugPpCertificate = false;      // Debug Point Perfect certificate management
    int geographicRegion = 0;             // Default to US - first entry in Regional_Information_Table
    uint64_t lastKeyAttempt = 0;          // Epoch time of last attempt at obtaining keys
    char pointPerfectBrokerHost[50] = ""; // pp.services.u-blox.com
    char pointPerfectClientID[50] = "";   // Obtained during ZTP
    char pointPerfectCurrentKey[33] = ""; // 32 hexadecimal digits = 128 bits = 16 Bytes
    uint64_t pointPerfectCurrentKeyDuration = 0;
    uint64_t pointPerfectCurrentKeyStart = 0;
    char pointPerfectDeviceProfileToken[40] = "";
    char pointPerfectKeyDistributionTopic[20] = ""; // /pp/ubx/0236/ip or /pp/ubx/0236/Lb - from ZTP
    char pointPerfectNextKey[33] = "";
    uint64_t pointPerfectNextKeyDuration = 0;
    uint64_t pointPerfectNextKeyStart = 0;
    uint16_t pplFixTimeoutS = 180; // Number of seconds of no RTK fix when using PPL before resetting GNSS
    // The correction topics are provided during ZTP (pointperfectTryZtpToken)
    // For IP-only plans, these will be /pp/ip/us, /pp/ip/eu, etc.
    // For L-Band+IP plans, these will be /pp/Lb/us, /pp/Lb/eu, etc.
    // L-Band-only plans have no correction topics
    // char regionalCorrectionTopics[numRegionalAreas][10] = {
    //     "", "", "", "", "",
    // };
    // uint8_t pointPerfectService =
    //     PP_NICKNAME_DISABLED; // See ppServices[]. Records which PointPerfect service the user has chosen.

    // Profiles
    char profileName[50] = "";

    // Pulse
    bool enableExternalPulse = true;          // Send pulse once lock is achieved
    uint64_t externalPulseLength_us = 200000; // us length of pulse, max of 60s = 60 * 1000 * 1000
    // pulseEdgeType_e externalPulsePolarity = PULSE_RISING_EDGE; // Pulse rises for pulse length, then falls
    uint64_t externalPulseTimeBetweenPulse_us = 1000000; // us between pulses, max of 60s = 60 * 1000 * 1000

    // Ring Buffer
    bool enablePrintRingBufferOffsets = false;
    int gnssHandlerBufferSize =
        1024 * 4; // This buffer is filled from the UART receive buffer, and is then written to SD

    // Rover operation
    uint8_t dynamicModel = 254; // Default will be applied by checkGNSSArrayDefaults
    bool enablePrintRoverAccuracy = true;
    int16_t minCN0 = 6; // Minimum satellite signal level for navigation. ZED-F9P default is 6 dBHz
    // Minimum elevation (in deg) for a GNSS satellite to be used in NAV
    // Note: we use 8-bit unsigned here, but some platforms (ZED, mosaic-X5) support negative elevation limits
    uint8_t minElev = 10;

    // RTC (Real Time Clock)
    bool enablePrintRtcSync = false;

    // RTCM buffers
    bool debugRtcmBuffers = false;

    // SD Card
    bool enablePrintBufferOverrun = false;
    bool enablePrintSDBuffers = false;
    bool enableSD = true;
    bool forceResetOnSDFail = false; // Set to true to reset system if SD is detected but fails to start.

    // Serial
    // Default to 115200bps. This interface can be a bottleneck at high fix rates but allows the SD buffer to be reduced to 6k.
    uint32_t dataPortBaud = (115200);
    bool echoUserInput = true;
    bool enableGnssToUsbSerial = false;
    uint32_t radioPortBaud = 57600; // Default to 57600bps to support connection to SiK1000 type telemetry radios
    int16_t serialTimeoutGNSS = 1;  // In ms - used during serialGNSS->begin. Number of ms to pass of no data before
                                    // hardware serial reports data available.
    bool enableNmeaOnRadio = true;  // Depends on the platform and GNSS

    // Setup Button
    bool disableSetupButton = false; // By default, allow setup through the overlay button(s)

    // State
    bool enablePrintDuplicateStates = false;
    bool enablePrintStates = true;
    SystemState_t lastState = STATE_NOT_SET; // Start unit in last known state

    // TCP Client
    bool debugTcpClient = false;
    bool enableTcpClient = false;
    char tcpClientHost[50] = "";
    uint16_t tcpClientPort = 2948; // TCP client port. 2948 is GPS Daemon: http://tcp-udp-ports.com/port-2948.htm

    // TCP Server
    bool debugTcpServer = false;
    bool enableTcpServer = false;
    uint16_t tcpServerPort = 2948;  // TCP server port, 2948 is GPS Daemon: http://tcp-udp-ports.com/port-2948.htm
    bool tcpOverWiFiStation = true; // Should TCP server use Station (true) or AP (false)
    bool udpOverWiFiStation = true; // Should UDP server use Station (true) or AP (false)

    // Time Zone - Default to UTC
    int8_t timeZoneHours = 0;
    int8_t timeZoneMinutes = 0;
    int8_t timeZoneSeconds = 0;

    // UBX
#ifdef COMPILE_ZED
    uint8_t ubxConstellationsEnabled[MAX_UBX_CONSTELLATIONS] = {
        254};                                     // Mark first record with key so defaults will be applied.
    uint8_t ubxMessageRates[MAX_UBX_MSG] = {254}; // Mark first record with key so defaults will be applied.
    uint8_t ubxMessageRatesBase[MAX_UBX_MSG_RTCM] = {
        254}; // Mark first record with key so defaults will be applied. Int value for each supported message - Report
              // rates for RTCM Base. Default to u-blox recommended rates.
#endif        // COMPILE_ZED

    // UDP Server
    bool debugUdpServer = false;
    bool enableUdpServer = false;
    uint16_t udpServerPort = 10110; // NMEA-0183 Navigational Data: https://tcp-udp-ports.com/port-10110.htm

    // UM980
    bool enableImuCompensationDebug = false;
    bool enableImuDebug = false;         // Turn on to display IMU library debug messages
    bool enableTiltCompensation = false; // Allow user to disable tilt compensation on the models that have an IMU
#ifdef COMPILE_UM980
    uint8_t um980Constellations[MAX_UM980_CONSTELLATIONS] = {
        254};                                                // Mark first record with key so defaults will be applied.
    float um980MessageRatesNMEA[MAX_UM980_NMEA_MSG] = {254}; // Mark first record with key so defaults will be applied.
    float um980MessageRatesRTCMBase[MAX_UM980_RTCM_MSG] = {
        254}; // Mark first record with key so defaults will be applied. Int value for each supported message - Report
              // rates for RTCM Base. Default to Unicore recommended rates.
    float um980MessageRatesRTCMRover[MAX_UM980_RTCM_MSG] = {
        254}; // Mark first record with key so defaults will be applied. Int value for each supported message - Report
              // rates for RTCM Base. Default to Unicore recommended rates.
#endif        // COMPILE_UM980

    // mosaic
#ifdef COMPILE_MOSAICX5
    uint8_t mosaicConstellations[MAX_MOSAIC_CONSTELLATIONS] = {
        254}; // Mark first record with key so defaults will be applied.
    // Each Stream has one connection descriptor and one interval.
    // If a NMEA message is disabled, its entry in mosaicMessageStreamNMEA is 0.
    // If a NMEA message is allocated to Stream1, its entry in mosaicMessageStreamNMEA is 1.
    // Etc..
    uint8_t mosaicMessageStreamNMEA[MAX_MOSAIC_NMEA_MSG] = {
        254}; // Mark first record with key so defaults will be applied.
    // mosaicStreamIntervalsNMEA contains the interval for each of the MOSAIC_NUM_NMEA_STREAMS NMEA Streams
    // It should be an array of mosaicMessageRates (enum). But we'll make life easy for ourselves and use uint8_t
    // The interval will never be "off". To disable a message, set the stream to 0.
    uint8_t mosaicStreamIntervalsNMEA[MOSAIC_NUM_NMEA_STREAMS] = MOSAIC_DEFAULT_NMEA_STREAM_INTERVALS;
    //mosaicRTCMv2MsgRate mosaicMessageRatesRTCMv2Rover[MAX_MOSAIC_RTCM_V2_MSG] = {
    //    { 65534, false } }; // Mark first record with key so defaults will be applied
    //mosaicRTCMv2MsgRate mosaicMessageRatesRTCMv2Base[MAX_MOSAIC_RTCM_V2_MSG] = {
    //    { 65534, false } }; // Mark first record with key so defaults will be applied
    float mosaicMessageIntervalsRTCMv3Rover[MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS] = {
        0.0}; // Mark first record with illegal value so defaults will be applied
    float mosaicMessageIntervalsRTCMv3Base[MAX_MOSAIC_RTCM_V3_INTERVAL_GROUPS] = {
        0.0}; // Mark first record with illegal value so defaults will be applied
    uint8_t mosaicMessageEnabledRTCMv3Rover[MAX_MOSAIC_RTCM_V3_MSG] = {
        254}; // Mark first record with key so defaults will be applied
    uint8_t mosaicMessageEnabledRTCMv3Base[MAX_MOSAIC_RTCM_V3_MSG] = {
        254}; // Mark first record with key so defaults will be applied
#endif        // COMPILE_MOSAICX5

    // We use enableLogging to control the logging of NMEA streams
    // RINEX logging needs its own enable
    bool enableLoggingRINEX = false;
    // uint8_t RINEXFileDuration = MOSAIC_FILE_DURATION_HOUR24;
    // uint8_t RINEXObsInterval = MOSAIC_OBS_INTERVAL_SEC30;
    bool externalEventPolarity = false; // false == Low2High; true == High2Low

    // Web Server
    uint16_t httpPort = 80;

    // WiFi
    bool debugWebServer = true;
    bool debugWifiState = true;
    bool enableCaptivePortal = true;
    uint8_t wifiChannel = 1;      //Valid channels are 1 to 14
    bool wifiConfigOverAP = true; // Configure device over Access Point or have it connect to WiFi
    WiFiNetwork_t wifiNetworks[MAX_WIFI_NETWORKS] = {
        {"", ""},
        {"", ""},
        {"", ""},
        {"", ""},
    };
    uint32_t wifiConnectTimeoutMs = 10000; // Wait this long for a WiFiMulti connection

    bool outputTipAltitude =
        false; // If enabled, subtract the pole length and APC from the GNSS receiver's reported altitude

    // Localized distribution
    bool useLocalizedDistribution = false;
    uint8_t localizedDistributionTileLevel = 5;
    bool useAssistNow = false;

    bool requestKeyUpdate = false; // Set to true to force a key provisioning attempt

    bool debugLora = false;
    bool enableLora = false;
    float loraCoordinationFrequency = 910.000;
    int loraSerialInteractionTimeout_s =
        30; // Seconds without user serial that must elapse before LoRa radio goes into dedicated listening mode
    bool loraSaveSettingsToFlash =
        false; // Passed to LoRa (>= 3.0.1) as AT+SAVE= . When true, updated settings are saved at each AT+TRANS
    int loraTransmitGain_dB = 10;          // Passed to LoRa as AT+PWR=
    bool enableMultipathMitigation = true; // Multipath mitigation. UM980 specific.

#ifdef COMPILE_LG290P
    uint8_t lg290pConstellations[MAX_LG290P_CONSTELLATIONS] = {
        254};                                                // Mark first record with key so defaults will be applied.
    int lg290pMessageRatesNMEA[MAX_LG290P_NMEA_MSG] = {254}; // Mark first record with key so defaults will be applied.
    int lg290pMessageRatesRTCMBase[MAX_LG290P_RTCM_MSG] = {
        254}; // Mark first record with key so defaults will be applied. Int value for each supported message - Report
              // rates for RTCM Base. Default to Quectel recommended rates.
    int lg290pMessageRatesRTCMRover[MAX_LG290P_RTCM_MSG] = {
        254}; // Mark first record with key so defaults will be applied. Int value for each supported message - Report
              // rates for RTCM Base. Default to Quectel recommended rates.
    int lg290pMessageRatesPQTM[MAX_LG290P_PQTM_MSG] = {254}; // Mark first record with key so defaults will be applied.
#endif                                                       // COMPILE_LG290P

    bool debugSettings = false;
    bool enableNtripCaster = false; //When true, respond as a faux NTRIP Caster to incoming TCP connections
    bool baseCasterOverride =
        false; //When true, user has put device into 'BaseCast' mode. Change settings, but don't save to NVM.
    bool debugCLI = false; //When true, output BLE CLI interactions over serial
    uint16_t cliBlePrintDelay_ms =
        50; // Time delayed between prints during a LIST command to avoid overwhelming the BLE connection
    uint32_t gnssConfigureRequest =
        0;                        // Bitfield containing the change requests for various settings on the GNSS receiver
    bool debugGnssConfig = false; // Enable to print output during gnssUpdate

    int pppMode = PPP_MODE_HAS;            // 0 = Disable, 1 = B2b PPP, 2 = HAS, 0xFF = Auto
    int pppDatum = 1;                      // 1 = WGS84, 2 = PPP Original, 3 = CGCS2000
    int pppTimeout = 120;                  // Seconds without PPP corrections before fallback
    float pppHorizontalConvergence = 0.10; // Meters, required horizontal convergence for PPP fix
    float pppVerticalConvergence = 0.15;   // Meters, required vertical convergence for PPP fix

    // Add new settings to appropriate group above or create new group
    // Then also add to the same group in rtkSettingsEntries below
} settings_t;
