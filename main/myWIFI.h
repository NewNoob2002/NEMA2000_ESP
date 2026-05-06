#pragma once

#ifndef COMPILE_WIFI
#define COMPILE_WIFI
#endif

//****************************************
// WiFi class
//****************************************

#ifdef COMPILE_WIFI

#include <stdint.h>
typedef uint8_t WIFI_CHANNEL_t;
typedef uint32_t WIFI_ACTION_t;
bool wifiSettingsChangedAndFree();
bool wifiSettingsChanged(struct settings_t* newSettings);

#if defined(__cplusplus)
#include <Arduino.h>
#include <Network.h>
#include <esp_wifi_types_generic.h>

// Class to simplify WiFi handling
class RTK_WIFI {
  private:
    WIFI_CHANNEL_t _apChannel;          // Channel required for soft AP, zero (0) use wifiChannel
    int16_t _apCount;                   // The number or remote APs detected in the WiFi network
    IPAddress _apDnsAddress;            // DNS IP address to use while translating names into IP addresses
    IPAddress _apFirstDhcpAddress;      // First IP address to use for DHCP
    IPAddress _apGatewayAddress;        // IP address of the gateway to the larger network (internet?)
    IPAddress _apIpAddress;             // IP address of the soft AP
    uint8_t _apMacAddress[6];           // MAC address of the soft AP
    IPAddress _apSubnetMask;            // Subnet mask for soft AP
    WIFI_CHANNEL_t _espNowChannel;      // Channel required for ESPNow, zero (0) use wifiChannel
    volatile bool _scanRunning;         // Scan running
    int _staAuthType;                   // Authorization type for the remote AP
    bool _staConnected;                 // True when station is connected
    bool _staHasIp;                     // True when station has IP address
    IPAddress _staIpAddress;            // IP address of the station
    uint8_t _staIpType;                 // 4 or 6 when IP address is assigned
    volatile uint8_t _staMacAddress[6]; // MAC address of the station
    const char* _staRemoteApSsid;       // SSID of remote AP
    const char* _staRemoteApPassword;   // Password of remote AP
    volatile WIFI_ACTION_t _started;    // Components that are started and running
    WIFI_CHANNEL_t _stationChannel;     // Channel required for station, zero (0) use wifiChannel
    bool _usingDefaultChannel;          // Using default WiFi channel
    bool _verbose;                      // True causes more debug output to be displayed

    // Display components begin started or stopped
    // Inputs:
    //   text: Text describing the component list
    //   components: A bit mask of the components
    void displayComponents(const char* text, WIFI_ACTION_t components);

    // Set the WiFi mode
    // Inputs:
    //   setMode: Modes to set
    //   xorMode: Modes to toggle
    //
    // Math: result = (mode | setMode) ^ xorMode
    //
    //                              setMode
    //                      0                   1
    //  xorMode 0       No change           Set bit
    //          1       Toggle bit          Clear bit
    //
    // Outputs:
    //   Returns true if successful and false upon failure
    bool setWiFiMode(uint8_t setMode, uint8_t xorMode);

    // Set the WiFi radio protocols
    // Inputs:
    //   interface: Interface on which to set the protocols
    //   enableWiFiProtocols: When true, enable the WiFi protocols
    //   enableLongRangeProtocol: When true, enable the long range protocol
    // Outputs:
    //   Returns true if successful and false upon failure
    bool setWiFiProtocols(wifi_interface_t interface, bool enableWiFiProtocols, bool enableLongRangeProtocol);

    // Handle the soft AP events
    // Inputs:
    //   event: Arduino ESP32 event number found on
    //          https://github.com/espressif/arduino-esp32
    //          in libraries/Network/src/NetworkEvents.h
    //   info: Additional data about the event
    void softApEventHandler(arduino_event_id_t event, arduino_event_info_t info);

    // Set the soft AP host name
    // Inputs:
    //   hostName: Zero terminated host name character string
    // Outputs:
    //   Returns true if successful and false upon failure
    bool softApSetHostName(const char* hostName);

    // Set the soft AP configuration
    // Inputs:
    //   ipAddress: IP address of the server, nullptr or empty string causes
    //              default 192.168.4.1 to be used
    //   subnetMask: Subnet mask for local network segment, nullptr or empty
    //              string causes default 0.0.0.0 to be used, unless ipAddress
    //              is not specified, in which case 255.255.255.0 is used
    //   gatewayAddress: Gateway to internet IP address, nullptr or empty string
    //            causes default 0.0.0.0 to be used (no access to internet)
    //   dnsAddress: Domain name server (name to IP address translation) IP address,
    //              nullptr or empty string causes 0.0.0.0 to be used (only
    //              mDNS name translation, if started)
    //   dhcpStartAddress: Start of DHCP IP address assignments for the local
    //              network segment, nullptr or empty string causes default
    //              0.0.0.0 to be used (disable DHCP server)  unless ipAddress
    //              was not specified in which case 192.168.4.2
    // Outputs:
    //   Returns true if successful and false upon failure
    bool softApSetIpAddress(const char* ipAddress, const char* subnetMask, const char* gatewayAddress,
                            const char* dnsAddress, const char* dhcpFirstAddress);

    // Set the soft AP SSID and password
    // Outputs:
    //   Returns true if successful and false upon failure
    bool softApSetSsidPassword(const char* ssid, const char* password);

    // Connect to an access point
    // Outputs:
    //   Return true if the connection was successful and false upon failure.
    bool stationConnectAP();

    // Disconnect the station from an AP
    // Outputs:
    //   Returns true if successful and false upon failure
    bool stationDisconnect();

    // Handle the WiFi station events
    // Inputs:
    //   event: Arduino ESP32 event number found on
    //          https://github.com/espressif/arduino-esp32
    //          in libraries/Network/src/NetworkEvents.h
    //   info: Additional data about the event
    void stationEventHandler(arduino_event_id_t event, arduino_event_info_t info);

    // Set the station's host name
    // Inputs:
    //   hostName: Zero terminated host name character string
    // Outputs:
    //   Returns true if successful and false upon failure
    bool stationHostName(const char* hostName);

    // Start the WiFi scan
    // Inputs:
    //   channel: Channel number for the scan, zero (0) scan all channels
    // Outputs:
    //   Returns the number of access points
    int16_t stationScanForAPs(WIFI_CHANNEL_t channel);

    // Select the AP and channel to use for WiFi station
    // Inputs:
    //   apCount: Number to APs detected by the WiFi scan
    //   list: Determine if the APs should be listed
    // Outputs:
    //   Returns the channel number of the AP
    WIFI_CHANNEL_t stationSelectAP(uint8_t apCount, bool list);

    // Handle the WiFi event
    // Inputs:
    //   event: Arduino ESP32 event number found on
    //          https://github.com/espressif/arduino-esp32
    //          in libraries/Network/src/NetworkEvents.h
    //   info: Additional data about the event
    void wifiEvent(arduino_event_id_t event, arduino_event_info_t info);

  public:
    char* _apSsid; // SSID for the soft AP

    // Constructor
    // Inputs:
    //   verbose: Set to true to display additional WiFi debug data
    RTK_WIFI(bool verbose = false);

    // Clear some of the started components
    // Inputs:
    //   components: Bitmask of components to clear
    // Outputs:
    //   Returns the bitmask of started components
    WIFI_ACTION_t clearStarted(WIFI_ACTION_t components);

    // Attempts a connection to all provided SSIDs
    // Inputs:
    //    timeout: Number of milliseconds to wait for the connection
    //    startAP: Set true to start AP mode, false does not change soft AP
    //             status
    // Outputs:
    //    Returns true if successful and false upon timeout, no matching
    //    SSID or other failure
    bool connect(unsigned long timeout, bool startAP);

    // Enable or disable the WiFi modes
    // Inputs:
    //   enableESPNow: Enable ESP-NOW mode
    //   enableSoftAP: Enable soft AP mode
    //   enableStataion: Enable station mode
    //   fileName: Name of file calling the enable routine
    //   lineNumber: Line number in the file calling the enable routine
    // Outputs:
    //   Returns true if the modes were successfully configured
    bool enable(bool enableESPNow, bool enableSoftAP, bool enableStation, const char* fileName, int lineNumber);

    // Get the ESP-NOW channel
    // Outputs:
    //   Returns the requested ESP-NOW channel
    WIFI_CHANNEL_t espNowChannelGet();

    // Set the ESP-NOW channel
    // Inputs:
    //   channel: New ESP-NOW channel number
    void espNowChannelSet(WIFI_CHANNEL_t channel);

    // Get the ESP-NOW status
    // Outputs:
    //   Returns true when ESP-NOW is online and ready for use
    bool espNowOnline();

    // Handle the WiFi event
    // Inputs:
    //   event: Arduino ESP32 event number found on
    //          https://github.com/espressif/arduino-esp32
    //          in libraries/Network/src/NetworkEvents.h
    //   info: Additional data about the event
    void eventHandler(arduino_event_id_t event, arduino_event_info_t info);

    // Get the current WiFi channel
    // Outputs:
    //   Returns the current WiFi channel number
    WIFI_CHANNEL_t getChannel();

    // Get the soft AP channel
    // Outputs:
    //   Returns the requested soft AP channel
    WIFI_CHANNEL_t softApChannelGet();

    // Set the soft AP channel
    // Inputs:
    //   channel: Request the channel for WiFi soft AP
    void softApChannelSet(WIFI_CHANNEL_t channel);

    // Configure the soft AP
    // Inputs:
    //   ipAddress: IP address of the soft AP
    //   subnetMask: Subnet mask for the soft AP network
    //   firstDhcpAddress: First IP address to use in the DHCP range
    //   dnsAddress: IP address to use for DNS lookup (translate name to IP address)
    //   gatewayAddress: IP address of the gateway to a larger network (internet?)
    // Outputs:
    //   Returns true if the soft AP was successfully configured.
    bool softApConfiguration(IPAddress ipAddress, IPAddress subnetMask, IPAddress firstDhcpAddress,
                             IPAddress dnsAddress, IPAddress gateway);

    // Display the soft AP configuration
    // Inputs:
    //   display: Address of a Print object
    void softApConfigurationDisplay(Print* display);

    // Get the soft AP IP address
    // Returns the soft IP address
    IPAddress softApIpAddress();

    // Get the soft AP status
    // Outputs:
    //   Returns true when the soft AP is ready for use
    bool softApOnline();

    // Attempt to start the soft AP mode
    // Inputs:
    //    forceAP: Set to true to force AP to start, false will only start
    //             soft AP if settings.wifiConfigOverAP is true
    // Outputs:
    //    Returns true if the soft AP was started successfully and false
    //    otherwise
    bool startAp(bool forceAP);

    // Get the station channel
    // Outputs:
    //   Returns the requested station channel
    WIFI_CHANNEL_t stationChannelGet();

    // Set the station channel
    // Inputs:
    //   channel: Request the channel for WiFi station
    void stationChannelSet(WIFI_CHANNEL_t channel);

    // Get the WiFi station IP address
    // Returns the IP address of the WiFi station
    IPAddress stationIpAddress();

    // Get the station status
    // Outputs:
    //   Returns true when the WiFi station is online and ready for use
    bool stationOnline();

    // Get the SSID of the remote AP
    const char* stationSsid();

    // Stop and start WiFi components
    // Inputs:
    //   stopping: WiFi components that need to be stopped
    //   starting: WiFi components that neet to be started
    // Outputs:
    //   Returns true if the modes were successfully configured
    bool stopStart(WIFI_ACTION_t stopping, WIFI_ACTION_t starting);

    // Test the WiFi modes
    // Inputs:
    //   testDurationMsec: Milliseconds to run each test
    void test(uint32_t testDurationMsec);

    // Enable or disable verbose debug output
    // Inputs:
    //   enable: Set true to enable verbose debug output
    // Outputs:
    //   Return the previous enable value
    bool verbose(bool enable);

    // Verify the WiFi tables
    void verifyTables();
};

#endif //__cplusplus

#endif // COMPILE_WIFI