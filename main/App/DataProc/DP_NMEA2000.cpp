#include "DataProc.h"

#include <stdint.h>

#include "NMEA2000_esp32.h"
#include "nmea0183_to_n2k.h"

namespace {

constexpr uint32_t kNmeaFrameBufferSize = 256;
constexpr uint32_t kNmea2000TimerPeriodMs = 10;

tGatewayNmea0183Parser nmeaParser;
tNMEA2000_esp32 nmea2000;
bool nmea2000Ready = false;
uint32_t n2kSent = 0;
uint32_t n2kFailed = 0;

bool
sendN2kMessage(const tN2kMsg& message) {
    if (!nmea2000Ready) {
        return false;
    }

    if (nmea2000.SendMsg(message)) {
        n2kSent++;
        return true;
    }

    n2kFailed++;
    return false;
}

void
onGatewayMessages(const tGatewayN2kMessages& messages, void* userContext) {
    (void)userContext;

    sendN2kMessage(messages.LatLonRapid);
    sendN2kMessage(messages.CogSogRapid);
    sendN2kMessage(messages.Gnss);
}

void
pullGnssNmea(Account* account) {
    uint8_t buffer[kNmeaFrameBufferSize] = {};
    uint32_t size = sizeof(buffer);

    while (account->Pull("GNSS_NMEA", buffer, &size) == Account::RES_OK) {
        nmeaParser.FeedBytes(buffer, size);
        size = sizeof(buffer);
    }
}

int
onEvent(Account* account, Account::EventParam_t* param) {
    if (!account || !param) {
        return Account::RES_PARAM_ERROR;
    }

    if (param->event == Account::EVENT_PUB_PUBLISH_PULL) {
        pullGnssNmea(account);
        return Account::RES_OK;
    }

    if (param->event == Account::EVENT_TIMER) {
        if (nmea2000Ready) {
            nmea2000.ParseMessages();
        }
        return Account::RES_OK;
    }

    return Account::RES_UNSUPPORTED_REQUEST;
}

} // namespace

DATA_PROC_INIT_DEF(NMEA2000) {
    account->Subscribe("GNSS_NMEA");
    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(kNmea2000TimerPeriodMs);

    if (!nmeaParser.Begin()) {
        return;
    }
    nmeaParser.SetMessageCallback(onGatewayMessages, nullptr);

    ConfigureGatewayNmea2000(nmea2000);
    nmea2000.ClearCANStatus();
    nmea2000Ready = nmea2000.Open();
}
