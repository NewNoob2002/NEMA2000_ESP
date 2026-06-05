#pragma once

#include "mcu_settings.h"

#ifdef COMPILE_WIFI

#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

#define WIFI_DEFAULT_CHANNEL         1
#define WIFI_IP_ADDRESS_TIMEOUT_MSEC (15 * MILLISECONDS_IN_A_SECOND)

typedef uint8_t WIFI_CHANNEL_t;

bool wifiSettingsChangedAndFree();
bool wifiSettingsChanged(struct settings_t* newSettings);
void wifiSettingsClone();
void wifiUpdateSettings();

bool wifiSoftApOn(const char* fileName, uint32_t lineNumber);
bool wifiSoftApOff(const char* fileName, uint32_t lineNumber);
IPAddress wifiSoftApGetIpAddress();
IPAddress wifiSoftApGetBroadcastIpAddress();
const char* wifiSoftApGetSsid();
bool wifiSoftApOnline();
bool wifiSoftApRunning();
uint8_t wifiSoftApClientCount();

bool wifiStationOn(const char* fileName, uint32_t lineNumber);
bool wifiStationOff(const char* fileName, uint32_t lineNumber);
bool wifiStationOnline();
IPAddress wifiStationGetIpAddress();
const char* wifiStationGetSsid();

void wifiStopAll();
int wifiNetworkCount();
const char* wifiPrintState(wl_status_t wifiStatus);
void wifiDisplayNetworkData();
void wifiDisplaySoftApStatus();
void wifiDisplayState();
void wifiVerifyTables();

class RTK_WIFI {
  private:
    IPAddress _apDnsAddress;
    IPAddress _apFirstDhcpAddress;
    IPAddress _apGatewayAddress;
    IPAddress _apIpAddress;
    IPAddress _apSubnetMask;
    WIFI_CHANNEL_t _apChannel;
    char _apSsid[WIFI_SSID_LENGTH + 1];

    WIFI_CHANNEL_t _stationChannel;
    IPAddress _staIpAddress;
    char _staRemoteApSsid[WIFI_SSID_LENGTH + 1];
    bool _eventRegistered;
    bool _verbose;

    void eventHandler(arduino_event_id_t event, arduino_event_info_t info);
    void registerEventHandler();
    void refreshOnlineFlags();
    bool setMode(bool enableSoftAP, bool enableStation);
    bool softApStart();
    bool softApStop();
    bool stationConnect(uint32_t timeoutMs);
    bool stationDisconnect();
    bool stationSelectNetwork(char* ssid, size_t ssidLength, char* password, size_t passwordLength,
                              WIFI_CHANNEL_t* channel);
    bool stationStart(uint32_t timeoutMs);
    bool stationStop();

  public:
    RTK_WIFI(bool verbose = false);

    bool enable(bool enableSoftAP, bool enableStation, const char* fileName, int lineNumber);
    bool connect(unsigned long timeout, bool startAP);

    WIFI_CHANNEL_t getChannel();
    WIFI_CHANNEL_t softApChannelGet();
    void softApChannelSet(WIFI_CHANNEL_t channel);
    bool softApConfiguration(IPAddress ipAddress, IPAddress subnetMask, IPAddress firstDhcpAddress,
                             IPAddress dnsAddress, IPAddress gateway);
    void softApConfigurationDisplay(Print* display);
    IPAddress softApIpAddress();
    bool softApOnline();
    const char* softApSsid();
    bool startAp(bool forceAP);

    WIFI_CHANNEL_t stationChannelGet();
    void stationChannelSet(WIFI_CHANNEL_t channel);
    IPAddress stationIpAddress();
    bool stationOnline();
    const char* stationSsid();

    bool verbose(bool enable);
    void verifyTables();
};

extern RTK_WIFI wifi;

#endif // COMPILE_WIFI
