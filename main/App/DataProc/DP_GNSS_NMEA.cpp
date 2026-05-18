#include "DataProc.h"

#include <string.h>

namespace {

Account* gnssNmeaAccount = nullptr;

} // namespace

void
DP_GNSS_DataHandler(const char* sentence, uint16_t length, void* userdata) {
    if (!sentence) {
        return;
    }
    if (gnssNmeaAccount) {
        gnssNmeaAccount->Commit((void*)sentence, length);
    }
}

DATA_PROC_INIT_DEF(GNSS_NMEA) { gnssNmeaAccount = account; }
