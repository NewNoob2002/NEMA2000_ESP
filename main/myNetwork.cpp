#include "myNetwork.h"

#include "CompileConfig.h"

#ifdef COMPILE_NETWORK

#include <Arduino.h>
#include <string.h>
#include "Support.h"
#include "mcu_settings.h"
#include "myWIFI.h"

namespace {

const char* const kNetworkConsumerNames[] = {
    "HTTP_CLIENT",
    "NTP_SERVER",
    "NTRIP_CLIENT",
    "NTRIP_SERVER_0",
    "NTRIP_SERVER_1",
    "NTRIP_SERVER_2",
    "NTRIP_SERVER_3",
    "OTA_CLIENT",
    "POINTPERFECT_KEY_UPDATE",
    "POINTPERFECT_MQTT_CLIENT",
    "TCP_CLIENT",
    "TCP_SERVER",
    "UDP_SERVER",
    "WEB_CONFIG",
};

const char* const kNetworkNames[] = {
    "Ethernet",
    "WiFi Station",
    "Cellular",
};

NETCONSUMER_MASK_t gAnyConsumers = 0;
NETCONSUMER_MASK_t gInterfaceConsumers[NETWORK_ANY] = {};
NETCONSUMER_MASK_t gSoftApConsumers = 0;
NETCONSUMER_MASK_t gUsers = 0;
NetIndex_t gConsumerNetwork[NETCONSUMER_MAX] = {};
NETCONSUMER_MASK_t gLastAnyConsumers = 0xffff;
NETCONSUMER_MASK_t gLastStationConsumers = 0xffff;
NETCONSUMER_MASK_t gLastSoftApConsumers = 0xffff;
NETCONSUMER_MASK_t gLastUsers = 0xffff;
uint8_t gLastSoftApClientCount = 0xff;
bool gLastStationOnline = true;
bool gLastSoftApRunning = true;
bool gStationNoConfigLogged = false;

NETCONSUMER_MASK_t
consumerBit(NETCONSUMER_t consumer) {
    return static_cast<NETCONSUMER_MASK_t>(1U << consumer);
}

bool
validConsumer(NETCONSUMER_t consumer) {
    return consumer < NETCONSUMER_MAX;
}

bool
validInterface(NetIndex_t network) {
    return network < NETWORK_ANY;
}

const char*
consumerName(NETCONSUMER_t consumer) {
    return validConsumer(consumer) ? kNetworkConsumerNames[consumer] : "INVALID";
}

void
logRequest(const char* action, NETCONSUMER_t consumer, const char* network, const char* fileName, uint32_t lineNumber) {
    if (settings.debugNetworkLayer) {
        systemPrintf("Network: %s %s on %s from %s:%lu\r\n", action, consumerName(consumer), network, fileName,
                     static_cast<unsigned long>(lineNumber));
    }
}

void
logStatus(const char* reason) {
    if (!settings.debugNetworkLayer) {
        return;
    }

    systemPrintf(
        "Network: %s req_any=0x%04X req_sta=0x%04X req_softap=0x%04X users=0x%04X sta=%s softap=%s clients=%u\r\n",
        reason, gAnyConsumers, gInterfaceConsumers[NETWORK_WIFI_STATION], gSoftApConsumers, gUsers,
        wifiStationOnline() ? "online" : "offline", wifiSoftApRunning() ? "running" : "off",
        static_cast<unsigned>(wifiSoftApClientCount()));
}

void
logStatusIfChanged(const char* reason) {
    if (!settings.debugNetworkLayer) {
        return;
    }

    const bool stationOnline = wifiStationOnline();
    const bool softApRunning = wifiSoftApRunning();
    const uint8_t softApClientCount = wifiSoftApClientCount();

    if ((gAnyConsumers == gLastAnyConsumers) && (gInterfaceConsumers[NETWORK_WIFI_STATION] == gLastStationConsumers)
        && (gSoftApConsumers == gLastSoftApConsumers) && (gUsers == gLastUsers)
        && (stationOnline == gLastStationOnline) && (softApRunning == gLastSoftApRunning)
        && (softApClientCount == gLastSoftApClientCount)) {
        return;
    }

    logStatus(reason);
    gLastAnyConsumers = gAnyConsumers;
    gLastStationConsumers = gInterfaceConsumers[NETWORK_WIFI_STATION];
    gLastSoftApConsumers = gSoftApConsumers;
    gLastUsers = gUsers;
    gLastStationOnline = stationOnline;
    gLastSoftApRunning = softApRunning;
    gLastSoftApClientCount = softApClientCount;
}

void
logActionResult(const char* action, bool success) {
    if (settings.debugNetworkLayer) {
        systemPrintf("Network: %s %s\r\n", action, success ? "ok" : "failed");
    }
}

bool
stationConsumersActive() {
    return (gAnyConsumers | gInterfaceConsumers[NETWORK_WIFI_STATION]) != 0;
}

bool
softApConsumersActive() {
    return gSoftApConsumers != 0;
}

void
applyRequestedNetworkState(const char* fileName, uint32_t lineNumber) {
#ifdef COMPILE_WIFI
    if (softApConsumersActive() && !wifiSoftApRunning()) {
        logStatus("starting WiFi SoftAP");
        logActionResult("WiFi SoftAP start", wifiSoftApOn(fileName, lineNumber));
    }

    if (stationConsumersActive() && !wifiStationOnline()) {
        if (wifiNetworkCount() > 0) {
            gStationNoConfigLogged = false;
            logStatus("starting WiFi Station");
            logActionResult("WiFi Station start", wifiStationOn(fileName, lineNumber));
        } else if (!gStationNoConfigLogged) {
            logStatus("WiFi Station requested but no saved network");
            gStationNoConfigLogged = true;
        }
    }

    if (!stationConsumersActive() && wifiStationOnline()) {
        gStationNoConfigLogged = false;
        logStatus("stopping WiFi Station");
        logActionResult("WiFi Station stop", wifiStationOff(fileName, lineNumber));
    } else if (!stationConsumersActive()) {
        gStationNoConfigLogged = false;
    }

    if (!softApConsumersActive() && wifiSoftApRunning()) {
        logStatus("stopping WiFi SoftAP");
        logActionResult("WiFi SoftAP stop", wifiSoftApOff(fileName, lineNumber));
    }

    logStatusIfChanged("state");
#else
    (void)fileName;
    (void)lineNumber;
#endif
}

} // namespace

void
networkBegin() {
    gAnyConsumers = 0;
    gSoftApConsumers = 0;
    gUsers = 0;
    memset(gInterfaceConsumers, 0, sizeof(gInterfaceConsumers));
    memset(gConsumerNetwork, NETWORK_NONE, sizeof(gConsumerNetwork));
    gLastAnyConsumers = 0xffff;
    gLastStationConsumers = 0xffff;
    gLastSoftApConsumers = 0xffff;
    gLastUsers = 0xffff;
    gLastSoftApClientCount = 0xff;
    gLastStationOnline = true;
    gLastSoftApRunning = true;
    gStationNoConfigLogged = false;
    logStatusIfChanged("begin");
}

void
networkUpdate() {
    applyRequestedNetworkState(__FILE__, __LINE__);
}

void
networkConsumerAdd(NETCONSUMER_t consumer, NetIndex_t network, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        systemPrintf("Network: invalid consumer %u\r\n", static_cast<unsigned>(consumer));
        return;
    }

    NETCONSUMER_MASK_t* consumers = &gAnyConsumers;
    const char* networkName = "Any";
    if (network != NETWORK_ANY) {
        if (!validInterface(network)) {
            systemPrintf("Network: invalid network %u for %s\r\n", static_cast<unsigned>(network),
                         consumerName(consumer));
            return;
        }
        consumers = &gInterfaceConsumers[network];
        networkName = networkGetNameByIndex(network);
    }

    logRequest("add consumer", consumer, networkName, fileName, lineNumber);
    *consumers |= consumerBit(consumer);
    gConsumerNetwork[consumer] = network;
    logStatusIfChanged("consumer add");
    applyRequestedNetworkState(fileName, lineNumber);
}

void
networkConsumerRemove(NETCONSUMER_t consumer, NetIndex_t network, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        return;
    }

    NETCONSUMER_MASK_t* consumers = &gAnyConsumers;
    const char* networkName = "Any";
    if (network != NETWORK_ANY) {
        if (!validInterface(network)) {
            return;
        }
        consumers = &gInterfaceConsumers[network];
        networkName = networkGetNameByIndex(network);
    }

    logRequest("remove consumer", consumer, networkName, fileName, lineNumber);
    *consumers &= ~consumerBit(consumer);
    gUsers &= ~consumerBit(consumer);
    logStatusIfChanged("consumer remove");
    applyRequestedNetworkState(fileName, lineNumber);
}

void
networkSoftApConsumerAdd(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        return;
    }

    logRequest("add consumer", consumer, "WiFi SoftAP", fileName, lineNumber);
    gSoftApConsumers |= consumerBit(consumer);
    logStatusIfChanged("softap consumer add");
    applyRequestedNetworkState(fileName, lineNumber);
}

void
networkSoftApConsumerRemove(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        return;
    }

    logRequest("remove consumer", consumer, "WiFi SoftAP", fileName, lineNumber);
    gSoftApConsumers &= ~consumerBit(consumer);
    gUsers &= ~consumerBit(consumer);
    logStatusIfChanged("softap consumer remove");
    applyRequestedNetworkState(fileName, lineNumber);
}

bool
networkConsumerIsConnected(NETCONSUMER_t consumer) {
    if (!validConsumer(consumer)) {
        return false;
    }

    const NETCONSUMER_MASK_t bit = consumerBit(consumer);
    if ((gSoftApConsumers & bit) && wifiSoftApRunning()) {
        return true;
    }
    if ((gAnyConsumers & bit) || (gInterfaceConsumers[NETWORK_WIFI_STATION] & bit)) {
        return wifiStationOnline();
    }

#ifdef COMPILE_ETHERNET
    if (gInterfaceConsumers[NETWORK_ETHERNET] & bit) {
        return false;
    }
#endif

    return false;
}

void
networkUserAdd(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        return;
    }
    logRequest("add user", consumer, "active network", fileName, lineNumber);
    gUsers |= consumerBit(consumer);
    logStatusIfChanged("user add");
}

void
networkUserRemove(NETCONSUMER_t consumer, const char* fileName, uint32_t lineNumber) {
    if (!validConsumer(consumer)) {
        return;
    }
    logRequest("remove user", consumer, "active network", fileName, lineNumber);
    gUsers &= ~consumerBit(consumer);
    logStatusIfChanged("user remove");
}

bool
networkInterfaceHasInternet(NetIndex_t network) {
    switch (network) {
    case NETWORK_WIFI_STATION:
        return wifiStationOnline();
#ifdef COMPILE_ETHERNET
    case NETWORK_ETHERNET:
        // Ethernet is intentionally stubbed until the driver/resource policy is implemented.
        return false;
#endif
    default:
        return false;
    }
}

bool
networkIsPresent(NetIndex_t network) {
    switch (network) {
    case NETWORK_WIFI_STATION:
        return wifiNetworkCount() > 0;
    case NETWORK_ETHERNET:
#ifdef COMPILE_ETHERNET
        // Ethernet is intentionally stubbed until the driver/resource policy is implemented.
        return false;
#else
        return false;
#endif
    case NETWORK_CELLULAR:
    default:
        return false;
    }
}

bool
networkIsStarted(NetIndex_t network) {
    switch (network) {
    case NETWORK_WIFI_STATION:
        return wifiStationOnline();
    case NETWORK_ETHERNET:
#ifdef COMPILE_ETHERNET
        // Ethernet is intentionally stubbed until the driver/resource policy is implemented.
        return false;
#endif
    case NETWORK_CELLULAR:
    default:
        return false;
    }
}

const char*
networkGetNameByIndex(NetIndex_t network) {
    return validInterface(network) ? kNetworkNames[network] : "None";
}

void
networkDisplayStatus() {
    systemPrintf("Network: consumers any=0x%04X softAP=0x%04X users=0x%04X\r\n", gAnyConsumers, gSoftApConsumers,
                 gUsers);
    systemPrintf("Network: WiFi STA=%s SoftAP=%s clients=%u\r\n", wifiStationOnline() ? "online" : "offline",
                 wifiSoftApRunning() ? "running" : "off", static_cast<unsigned>(wifiSoftApClientCount()));
}

#endif // COMPILE_NETWORK
