#include "DataProc.h"

#include <stdint.h>

#include "Bluetooth.h"

namespace {

constexpr uint32_t kNmeaFrameBufferSize = 256;

void
pullGnssNmea(Account* account) {
    uint8_t buffer[kNmeaFrameBufferSize] = {};
    uint32_t size = sizeof(buffer);

    if (account->Pull("GNSS_NMEA", buffer, &size) == Account::RES_OK) {
        if (bluetoothIsConnected()) {
            bluetoothWrite(buffer, size);
        }
        size = sizeof(buffer);
    }
}

int
onEvent(Account* account, Account::EventParam_t* param) {
    if (!account || !param) {
        return Account::RES_PARAM_ERROR;
    }

    if (param->event != Account::EVENT_PUB_PUBLISH_PULL) {
        return Account::RES_UNSUPPORTED_REQUEST;
    }

    pullGnssNmea(account);
    return Account::RES_OK;
}

} // namespace

DATA_PROC_INIT_DEF(Bluetooth) {
    account->Subscribe("GNSS_NMEA");
    account->SetEventCallback(onEvent);
}
