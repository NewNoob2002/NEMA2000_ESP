#pragma once

#include <stdint.h>

typedef uint8_t NETCONSUMER_t;
typedef uint16_t NETCONSUMER_MASK_t;
typedef uint8_t NetIndex_t;   // Index into the networkInterfaceTable
typedef uint32_t NetMask_t;   // One bit for each network interface
typedef int8_t NetPriority_t; // Index into networkPriorityTable
                              // Value 0 (highest) - 255 (lowest) priority
#define NTRIP_SERVER_MAX 4

// Bitfield for describing the network consumers
typedef enum NETCONSUMER_MASK_ENUM_t {
    NETCONSUMER_HTTP_CLIENT = 0,
    NETCONSUMER_NTP_SERVER,
    NETCONSUMER_NTRIP_CLIENT,
    NETCONSUMER_NTRIP_SERVER,
    NETCONSUMER_NTRIP_SERVER_0 = NETCONSUMER_NTRIP_SERVER,
    NETCONSUMER_NTRIP_SERVER_1,
    NETCONSUMER_NTRIP_SERVER_2,
    NETCONSUMER_NTRIP_SERVER_3,
    NETCONSUMER_NTRIP_SERVER_MAX = NETCONSUMER_NTRIP_SERVER + NTRIP_SERVER_MAX,
    NETCONSUMER_OTA_CLIENT = NETCONSUMER_NTRIP_SERVER_MAX,
    NETCONSUMER_POINTPERFECT_KEY_UPDATE,
    NETCONSUMER_POINTPERFECT_MQTT_CLIENT,
    NETCONSUMER_TCP_CLIENT,
    NETCONSUMER_TCP_SERVER,
    NETCONSUMER_UDP_SERVER,
    NETCONSUMER_WEB_CONFIG,
    // Add new consumers just before this line
    // Also add them to the networkConsumerTable
    NETCONSUMER_MAX
} NETCONSUMER_MASK_ENUM_t;

typedef enum NetworkTypes_t {
    NETWORK_NONE = -1,    // The values below must start at zero and be sequential
    NETWORK_ETHERNET,     // 0
    NETWORK_WIFI_STATION, // 1
    NETWORK_CELLULAR,     // 2
    // Add new networks above this line in default priority order
    NETWORK_ANY, // 3
    NETWORK_MAX = NETWORK_ANY,
} NetworkTypes_t;

void networkBegin();
void networkUpdate();

void networkConsumerAdd(NETCONSUMER_t consumer, NetIndex_t network, const char* fileName, uint32_t lineNumber);
void networkConsumerRemove(NETCONSUMER_t consumer, NetIndex_t network, const char* fileName, uint32_t lineNumber);
void networkSoftApConsumerAdd(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber);
void networkSoftApConsumerRemove(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber);
bool networkConsumerIsConnected(NETCONSUMER_t consumer);

void networkUserAdd(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber);
void networkUserRemove(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber);

bool networkInterfaceHasInternet(NetIndex_t network);
bool networkIsPresent(NetIndex_t network);
bool networkIsStarted(NetIndex_t network);
const char* networkGetNameByIndex(NetIndex_t network);
void networkDisplayStatus();
