#include "myWIFI.h"

#ifdef COMPILE_WIFI

#include <DNSServer.h>
#include <WiFi.h>
#include <string.h>
#include "Support.h"

static const char* wifiSoftApSsid = "S20 Config";
static const char* wifiSoftApPassword = "12345678";
static const char* wifiSoftApName = "Soft AP";

static DNSServer dnsServer;
static struct settings_t* wifiPreviousSettings;
static bool wifiSoftApSsidSet;
static bool wifiStationSsidSet;
static WIFI_CHANNEL_t wifiChannel;

RTK_WIFI wifi(false);

static void
copyCredential(char* destination, size_t destinationLength, const char* source) {
    if (destinationLength == 0) {
        return;
    }
    if (source == nullptr) {
        destination[0] = 0;
        return;
    }
    strncpy(destination, source, destinationLength - 1);
    destination[destinationLength - 1] = 0;
}

void
wifiUpdate() {
    if (settings.enableCaptivePortal && online_devices.wifi.wifiSoftApRunning) {
        dnsServer.processNextRequest();
    }
}

bool
wifiSettingsChangedAndFree() {
    bool changed = false;
    if (wifiPreviousSettings) {
        changed = wifiSettingsChanged(wifiPreviousSettings);
        rtkFree(wifiPreviousSettings, "WiFi previous settings");
        wifiPreviousSettings = nullptr;
    }
    return changed;
}

bool
wifiSettingsChanged(struct settings_t* newSettings) {
    if (newSettings == nullptr) {
        return false;
    }

    for (int index = 0; index < MAX_WIFI_NETWORKS; index++) {
        if (memcmp(newSettings->wifiNetworks[index].ssid, settings.wifiNetworks[index].ssid, WIFI_SSID_LENGTH) != 0) {
            return true;
        }
        if (memcmp(newSettings->wifiNetworks[index].password, settings.wifiNetworks[index].password,
                   WIFI_PASSWORD_LENGTH)
            != 0) {
            return true;
        }
    }
    return false;
}

void
wifiSettingsClone() {
    wifiPreviousSettings = (struct settings_t*)rtkMalloc(sizeof(settings), "WiFi previous settings");
    if (wifiPreviousSettings == nullptr) {
        systemPrintln("ERROR: WiFi failed to allocate previous settings!");
    } else {
        memcpy(wifiPreviousSettings, &settings, sizeof(settings));
    }
}

void
wifiUpdateSettings() {
    wifiStationSsidSet = wifiNetworkCount() > 0;
    wifiSoftApSsidSet = (wifiSoftApSsid != nullptr) && (strlen(wifiSoftApSsid) > 0);

    if (wifiSettingsChangedAndFree() && online_devices.wifi.wifiStationRunning) {
        wifiStationOff(__FILE__, __LINE__);
        if (wifiStationSsidSet) {
            wifiStationOn(__FILE__, __LINE__);
        }
    }
}

void
wifiDisplayNetworkData() {
    IPAddress ipAddress = wifiSoftApGetIpAddress();
    const bool hasIP = static_cast<uint32_t>(ipAddress) != 0;

    systemPrintf("%s: %s\r\n", wifiSoftApName, wifiSoftApOnline() ? "Online" : "Off");
    systemPrintf("    SSID: %s\r\n", wifiSoftApGetSsid());
    systemPrintf("    MAC Address: %s\r\n", WiFi.softAPmacAddress().c_str());
    if (hasIP) {
        systemPrintf("    IPv4 Address: %s\r\n", ipAddress.toString().c_str());
        systemPrintf("    Subnet Mask: %s\r\n", WiFi.softAPSubnetMask().toString().c_str());
    }
}

void
wifiDisplaySoftApStatus() {
    const char* status = "Off";
    if (online_devices.wifi.wifiSoftApOnline) {
        status = "Online";
    } else if (online_devices.wifi.wifiSoftApRunning) {
        status = "Starting";
    }

    systemPrintf("    %-10s %s\r\n", wifiSoftApName, status);
}

void
wifiDisplayState() {
    systemPrintf("WiFi Station: %s\r\n", wifiStationOnline() ? "Online" : "Offline");
    systemPrintf("    MAC Address: %s\r\n", WiFi.macAddress().c_str());

    if (wifiStationOnline()) {
        systemPrintf("    SSID: %s\r\n", WiFi.SSID().c_str());
        systemPrintf("    IP Address: %s\r\n", WiFi.localIP().toString().c_str());
        systemPrintf("    Subnet Mask: %s\r\n", WiFi.subnetMask().toString().c_str());
        systemPrintf("    Gateway Address: %s\r\n", WiFi.gatewayIP().toString().c_str());
        systemPrintf("    DNS Address: %s\r\n", WiFi.dnsIP().toString().c_str());
        systemPrintf("    WiFi Strength: %d dBm\r\n", WiFi.RSSI());
    }
    systemPrintf("    WiFi Status: %d (%s)\r\n", WiFi.status(), wifiPrintState(WiFi.status()));
}

int
wifiNetworkCount() {
    int networkCount = 0;
    for (int index = 0; index < MAX_WIFI_NETWORKS; index++) {
        if (strlen(settings.wifiNetworks[index].ssid) > 0) {
            networkCount++;
        }
    }
    return networkCount;
}

const char*
wifiPrintState(wl_status_t wifiStatus) {
    switch (wifiStatus) {
        case WL_NO_SHIELD: return "WL_NO_SHIELD";
        case WL_STOPPED: return "WL_STOPPED";
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
    }
    return "WiFi Status Unknown";
}

IPAddress
wifiSoftApGetBroadcastIpAddress() {
    return wifi.softApOnline() ? WiFi.softAPBroadcastIP() : IPAddress((uint32_t)0);
}

IPAddress
wifiSoftApGetIpAddress() {
    return wifi.softApIpAddress();
}

const char*
wifiSoftApGetSsid() {
    return wifi.softApOnline() ? wifi.softApSsid() : "";
}

bool
wifiSoftApOn(const char* fileName, uint32_t lineNumber) {
    if (settings.debugWifiState) {
        systemPrintf("wifiSoftApOn called in %s at line %lu\r\n", fileName, static_cast<unsigned long>(lineNumber));
    }
    return wifi.enable(true, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__);
}

bool
wifiSoftApOff(const char* fileName, uint32_t lineNumber) {
    if (settings.debugWifiState) {
        systemPrintf("wifiSoftApOff called in %s at line %lu\r\n", fileName, static_cast<unsigned long>(lineNumber));
    }
    return wifi.enable(false, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__);
}

bool
wifiSoftApOnline() {
    return wifi.softApOnline();
}

bool
wifiSoftApRunning() {
    return online_devices.wifi.wifiSoftApRunning;
}

uint8_t
wifiSoftApClientCount() {
    return online_devices.wifi.wifiSoftApRunning ? WiFi.softAPgetStationNum() : 0;
}

bool
wifiStationOn(const char* fileName, uint32_t lineNumber) {
    if (settings.debugWifiState) {
        systemPrintf("wifiStationOn called in %s at line %lu\r\n", fileName, static_cast<unsigned long>(lineNumber));
    }
    return wifi.enable(online_devices.wifi.wifiSoftApRunning, true, __FILE__, __LINE__);
}

bool
wifiStationOff(const char* fileName, uint32_t lineNumber) {
    if (settings.debugWifiState) {
        systemPrintf("wifiStationOff called in %s at line %lu\r\n", fileName, static_cast<unsigned long>(lineNumber));
    }
    return wifi.enable(online_devices.wifi.wifiSoftApRunning, false, __FILE__, __LINE__);
}

bool
wifiStationOnline() {
    return wifi.stationOnline();
}

IPAddress
wifiStationGetIpAddress() {
    return wifi.stationIpAddress();
}

const char*
wifiStationGetSsid() {
    return wifi.stationSsid();
}

void
wifiStopAll() {
    wifi.enable(false, false, __FILE__, __LINE__);
}

void
wifiVerifyTables() {
    wifi.verifyTables();
}

RTK_WIFI::RTK_WIFI(bool verbose)
    : _apDnsAddress{IPAddress((uint32_t)0)}, _apFirstDhcpAddress{IPAddress("192.168.10.32")},
      _apGatewayAddress{IPAddress("192.168.10.12")}, _apIpAddress{IPAddress("192.168.10.12")},
      _apSubnetMask{IPAddress("255.255.255.0")}, _apChannel{WIFI_DEFAULT_CHANNEL}, _apSsid{0}, _stationChannel{0},
      _staIpAddress{IPAddress((uint32_t)0)}, _staRemoteApSsid{0}, _eventRegistered{false}, _verbose{verbose} {
    wifiChannel = 0;
}

bool
RTK_WIFI::connect(unsigned long timeout, bool startAP) {
    if (startAP && !online_devices.wifi.wifiSoftApRunning) {
        if (!enable(true, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__)) {
            return false;
        }
    }

    if (!online_devices.wifi.wifiStationRunning) {
        return enable(online_devices.wifi.wifiSoftApRunning, true, __FILE__, __LINE__);
    }

    if (stationOnline()) {
        return true;
    }

    return stationStart(timeout);
}

bool
RTK_WIFI::enable(bool enableSoftAP, bool enableStation, const char* fileName, int lineNumber) {
    if (settings.debugWifiState && _verbose) {
        systemPrintf("WiFi enable from %s:%d AP=%s STA=%s\r\n", fileName, lineNumber, enableSoftAP ? "on" : "off",
                     enableStation ? "on" : "off");
    }

    bool success = true;

    if (!enableStation && online_devices.wifi.wifiStationRunning) {
        success = stationStop() && success;
    }
    if (!enableSoftAP && online_devices.wifi.wifiSoftApRunning) {
        success = softApStop() && success;
    }

    if (!setMode(enableSoftAP, enableStation)) {
        refreshOnlineFlags();
        return false;
    }

    if (enableSoftAP && !online_devices.wifi.wifiSoftApRunning) {
        if (!softApStart()) {
            success = false;
            setMode(false, enableStation);
        }
    }
    if (enableStation && !online_devices.wifi.wifiStationRunning) {
        if (!stationStart(settings.wifiConnectTimeoutMs)) {
            success = false;
            setMode(enableSoftAP && online_devices.wifi.wifiSoftApRunning, false);
        }
    }

    refreshOnlineFlags();
    return success;
}

void
RTK_WIFI::eventHandler(arduino_event_id_t event, arduino_event_info_t info) {
    (void)info;

    if (settings.debugWifiState && _verbose) {
        systemPrintf("WiFi event: %d (%s)\r\n", event, WiFi.eventName(event));
    }

    switch (event) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            online_devices.wifi.wifiSoftApConnected = wifiSoftApClientCount() > 0;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            _staIpAddress = WiFi.localIP();
            online_devices.wifi.wifiStationOnline = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_STA_STOP:
            _staIpAddress = IPAddress((uint32_t)0);
            online_devices.wifi.wifiStationOnline = false;
            break;
        default: break;
    }
}

void
RTK_WIFI::registerEventHandler() {
    if (_eventRegistered) {
        return;
    }

    WiFi.onEvent([this](arduino_event_id_t event, arduino_event_info_t info) { eventHandler(event, info); });
    _eventRegistered = true;
}

WIFI_CHANNEL_t
RTK_WIFI::getChannel() {
    return wifiChannel;
}

void
RTK_WIFI::refreshOnlineFlags() {
    online_devices.wifi.wifiSoftApOnline = online_devices.wifi.wifiSoftApRunning && (WiFi.getMode() & WIFI_AP);
    online_devices.wifi.wifiStationOnline = online_devices.wifi.wifiStationRunning && (WiFi.status() == WL_CONNECTED);
    if (online_devices.wifi.wifiStationOnline) {
        _staIpAddress = WiFi.localIP();
    }
}

bool
RTK_WIFI::setMode(bool enableSoftAP, bool enableStation) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (enableSoftAP && enableStation) {
        mode = WIFI_MODE_APSTA;
    } else if (enableSoftAP) {
        mode = WIFI_MODE_AP;
    } else if (enableStation) {
        mode = WIFI_MODE_STA;
    }

    if (WiFi.getMode() == mode) {
        return true;
    }

    if (!WiFi.mode(mode)) {
        systemPrintf("ERROR: Failed to set WiFi mode %d\r\n", mode);
        return false;
    }

    if (mode == WIFI_MODE_NULL) {
        wifiChannel = 0;
    }
    return true;
}

WIFI_CHANNEL_t
RTK_WIFI::softApChannelGet() {
    return _apChannel;
}

void
RTK_WIFI::softApChannelSet(WIFI_CHANNEL_t channel) {
    _apChannel = channel ? channel : WIFI_DEFAULT_CHANNEL;
}

bool
RTK_WIFI::softApConfiguration(IPAddress ipAddress, IPAddress subnetMask, IPAddress firstDhcpAddress,
                              IPAddress dnsAddress, IPAddress gatewayAddress) {
    _apIpAddress = ipAddress;
    _apSubnetMask = subnetMask;
    _apFirstDhcpAddress = firstDhcpAddress;
    _apDnsAddress = dnsAddress;
    _apGatewayAddress = gatewayAddress;

    if (online_devices.wifi.wifiSoftApRunning) {
        return enable(false, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__)
               && enable(true, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__);
    }

    return true;
}

void
RTK_WIFI::softApConfigurationDisplay(Print* display) {
    display->printf("Soft AP configuration:\r\n");
    display->printf("    %s: IP Address\r\n", _apIpAddress.toString().c_str());
    display->printf("    %s: Subnet mask\r\n", _apSubnetMask.toString().c_str());
    display->printf("    %s: First DHCP address\r\n", _apFirstDhcpAddress.toString().c_str());
    display->printf("    %s: DNS address\r\n", _apDnsAddress.toString().c_str());
    display->printf("    %s: Gateway address\r\n", _apGatewayAddress.toString().c_str());
}

IPAddress
RTK_WIFI::softApIpAddress() {
    return softApOnline() ? WiFi.softAPIP() : IPAddress((uint32_t)0);
}

bool
RTK_WIFI::softApOnline() {
    return online_devices.wifi.wifiSoftApOnline;
}

const char*
RTK_WIFI::softApSsid() {
    return _apSsid;
}

bool
RTK_WIFI::softApStart() {
    wifiSoftApSsidSet = (wifiSoftApSsid != nullptr) && (strlen(wifiSoftApSsid) > 0);
    if (!wifiSoftApSsidSet) {
        systemPrintln("ERROR: AP SSID is missing");
        return false;
    }

    registerEventHandler();

    if (!WiFi.softAPConfig(_apIpAddress, _apGatewayAddress, _apSubnetMask, _apFirstDhcpAddress, _apDnsAddress)) {
        systemPrintln("ERROR: Failed to configure WiFi soft AP");
        return false;
    }

    snprintf(_apSsid, sizeof(_apSsid), "%s %s", wifiSoftApSsid, productPropertiesTable[productType].productPlanUID);
    _apSsid[sizeof(_apSsid) - 1] = 0;

    WIFI_CHANNEL_t channel = _apChannel ? _apChannel : settings.wifiChannel;
    if (channel < 1 || channel > 14) {
        channel = WIFI_DEFAULT_CHANNEL;
    }
    if (!WiFi.softAP(_apSsid, wifiSoftApPassword, channel)) {
        systemPrintln("ERROR: Failed to start WiFi soft AP");
        return false;
    }

    wifiChannel = WiFi.channel();
    online_devices.wifi.wifiSoftApRunning = true;
    online_devices.wifi.wifiSoftApOnline = true;

    if (settings.enableCaptivePortal) {
        if (!dnsServer.start(53, "*", WiFi.softAPIP())) {
            systemPrintln("ERROR: Failed to start DNS server for captive portal");
            softApStop();
            return false;
        }
    }

    if (settings.debugWifiState) {
        systemPrintf("WiFi: Soft AP online, SSID: %s, IP: %s, Password: %s\r\n", _apSsid,
                     WiFi.softAPIP().toString().c_str(), wifiSoftApPassword);
    }

    return true;
}

bool
RTK_WIFI::softApStop() {
    dnsServer.stop();
    const bool stopped = WiFi.softAPdisconnect(false);
    online_devices.wifi.wifiSoftApRunning = false;
    online_devices.wifi.wifiSoftApOnline = false;
    online_devices.wifi.wifiSoftApConnected = false;
    _apSsid[0] = 0;

    if (!stopped) {
        systemPrintln("ERROR: Failed to stop WiFi soft AP");
    }

    return stopped;
}

bool
RTK_WIFI::startAp(bool forceAP) {
    return enable(forceAP || settings.wifiConfigOverAP, online_devices.wifi.wifiStationRunning, __FILE__, __LINE__);
}

WIFI_CHANNEL_t
RTK_WIFI::stationChannelGet() {
    return _stationChannel;
}

void
RTK_WIFI::stationChannelSet(WIFI_CHANNEL_t channel) {
    _stationChannel = channel;
}

IPAddress
RTK_WIFI::stationIpAddress() {
    return stationOnline() ? _staIpAddress : IPAddress((uint32_t)0);
}

bool
RTK_WIFI::stationOnline() {
    return online_devices.wifi.wifiStationOnline;
}

const char*
RTK_WIFI::stationSsid() {
    return stationOnline() ? _staRemoteApSsid : "";
}

bool
RTK_WIFI::stationConnect(uint32_t timeoutMs) {
    char ssid[WIFI_SSID_LENGTH + 1] = {};
    char password[WIFI_PASSWORD_LENGTH + 1] = {};
    WIFI_CHANNEL_t channel = 0;

    if (!stationSelectNetwork(ssid, sizeof(ssid), password, sizeof(password), &channel)) {
        systemPrintln("ERROR: No configured WiFi network was found");
        return false;
    }

    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.setHostname(settings.mdnsHostName);

    wl_status_t beginStatus = WiFi.begin(ssid, password, channel);
    if (beginStatus == WL_CONNECT_FAILED) {
        systemPrintf("ERROR: WiFi failed to begin connection to %s\r\n", ssid);
        return false;
    }

    const uint32_t startMsec = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if ((millis() - startMsec) >= timeoutMs) {
            systemPrintf("ERROR: WiFi timed out connecting to %s (%s)\r\n", ssid, wifiPrintState(WiFi.status()));
            return false;
        }
        delay(50);
    }

    copyCredential(_staRemoteApSsid, sizeof(_staRemoteApSsid), ssid);
    _staIpAddress = WiFi.localIP();
    wifiChannel = WiFi.channel();

    if (settings.debugWifiState) {
        systemPrintf("WiFi: Station online, SSID: %s, IP: %s, RSSI: %d dBm\r\n", _staRemoteApSsid,
                     _staIpAddress.toString().c_str(), WiFi.RSSI());
    }

    return true;
}

bool
RTK_WIFI::stationDisconnect() {
    if (!online_devices.wifi.wifiStationRunning) {
        return true;
    }

    const bool disconnected = WiFi.disconnect(false, false);
    _staIpAddress = IPAddress((uint32_t)0);
    _staRemoteApSsid[0] = 0;
    online_devices.wifi.wifiStationOnline = false;

    if (!disconnected) {
        systemPrintln("ERROR: Failed to disconnect WiFi station");
    }

    return disconnected;
}

bool
RTK_WIFI::stationSelectNetwork(char* ssid, size_t ssidLength, char* password, size_t passwordLength,
                               WIFI_CHANNEL_t* channel) {
    const WIFI_CHANNEL_t requestedChannel = _stationChannel;
    int networkCount = WiFi.scanNetworks(false, true, false, 300, requestedChannel);

    if (networkCount < 0) {
        systemPrintf("ERROR: WiFi scan failed, status: %d\r\n", networkCount);
        return false;
    }

    for (int scanIndex = 0; scanIndex < networkCount; scanIndex++) {
        const String scannedSsid = WiFi.SSID(scanIndex);
        const wifi_auth_mode_t authMode = WiFi.encryptionType(scanIndex);

        for (int configIndex = 0; configIndex < MAX_WIFI_NETWORKS; configIndex++) {
            const WiFiNetwork_t* network = &settings.wifiNetworks[configIndex];
            if (strlen(network->ssid) == 0) {
                continue;
            }
            if (scannedSsid != network->ssid) {
                continue;
            }
            if ((authMode != WIFI_AUTH_OPEN) && (strlen(network->password) == 0)) {
                continue;
            }

            copyCredential(ssid, ssidLength, network->ssid);
            copyCredential(password, passwordLength, network->password);
            *channel = WiFi.channel(scanIndex);
            WiFi.scanDelete();
            return true;
        }
    }

    WiFi.scanDelete();
    return false;
}

bool
RTK_WIFI::stationStart(uint32_t timeoutMs) {
    wifiStationSsidSet = wifiNetworkCount() > 0;
    if (!wifiStationSsidSet) {
        systemPrintln("ERROR: No valid SSID in settings to start WiFi station");
        return false;
    }

    registerEventHandler();

    if (!stationConnect(timeoutMs ? timeoutMs : settings.wifiConnectTimeoutMs)) {
        online_devices.wifi.wifiStationRunning = false;
        online_devices.wifi.wifiStationOnline = false;
        return false;
    }

    online_devices.wifi.wifiStationRunning = true;
    online_devices.wifi.wifiStationOnline = true;
    return true;
}

bool
RTK_WIFI::stationStop() {
    const bool stopped = stationDisconnect();
    online_devices.wifi.wifiStationRunning = false;
    online_devices.wifi.wifiStationOnline = false;
    return stopped;
}

bool
RTK_WIFI::verbose(bool enable) {
    const bool oldVerbose = _verbose;
    _verbose = enable;
    return oldVerbose;
}

void
RTK_WIFI::verifyTables() {
    if (settings.wifiChannel < 1 || settings.wifiChannel > 14) {
        systemPrintf("ERROR: WiFi channel must be 1-14, found %d\r\n", settings.wifiChannel);
        reportFatalError("Invalid WiFi channel");
    }
}

#endif // COMPILE_WIFI
