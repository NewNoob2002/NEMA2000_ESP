#include "DataProc.h"

#include <stdint.h>

#include "HAL/HAL.h"
#include "N2kTimer.h"
#include "NMEA2000_esp32.h"
#include "Unicore_UM980.h"
#include "nmea0183_to_n2k.h"

namespace {

constexpr uint32_t kNmea2000TimerPeriodMs = 10;
constexpr uint32_t kNmea2000GnssSendPeriodMs = 1000;

tNMEA2000_esp32 nmea2000;
bool nmea2000Ready = false;
uint32_t lastGnssSendMs = 0;
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
sendGatewayMessages(const tGatewayN2kMessages& messages) {
    sendN2kMessage(messages.LatLonRapid);
    sendN2kMessage(messages.CogSogRapid);
    sendN2kMessage(messages.Gnss);
}

void
sendGnssSnapshot() {
    if (!nmea2000Ready || HAL::gUm980 == nullptr) {
        return;
    }

    tGatewayGnssData gnssData;
    tGatewayN2kMessages messages;
    if (ReadGatewayGnssData(*HAL::gUm980, gnssData) && BuildGatewayN2kMessages(gnssData, messages)) {
        sendGatewayMessages(messages);
    }
}

int
onEvent(Account* account, Account::EventParam_t* param) {
    if (!account || !param) {
        return Account::RES_PARAM_ERROR;
    }

    if (param->event == Account::EVENT_TIMER) {
        if (nmea2000Ready) {
            nmea2000.ParseMessages();
        }
        const uint32_t now = N2kMillis();
        if (lastGnssSendMs == 0 || (now - lastGnssSendMs) >= kNmea2000GnssSendPeriodMs) {
            lastGnssSendMs = now;
            sendGnssSnapshot();
        }
        return Account::RES_OK;
    }

    return Account::RES_UNSUPPORTED_REQUEST;
}

} // namespace

DATA_PROC_INIT_DEF(NMEA2000) {
    account->SetEventCallback(onEvent);
    account->SetTimerPeriod(kNmea2000TimerPeriodMs);

    ConfigureGatewayNmea2000(nmea2000);
    nmea2000.ClearCANStatus();
    nmea2000Ready = nmea2000.Open();
}
