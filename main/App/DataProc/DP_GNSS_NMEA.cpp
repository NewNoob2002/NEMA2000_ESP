#include <string.h>
#include "DataProc.h"
#include "States.h"

namespace {

Account* gnssNmeaAccount = nullptr;

} // namespace

void
DP_GNSS_DataHandler(const char* sentence, uint16_t length, void* userdata) {
    if (!sentence) {
        return;
    }
    if (!inRoverMode() && !inBaseMode() && !inWebConfigMode()) {
        return;
    }
    if (gnssNmeaAccount) {
        if (gnssNmeaAccount->Commit((void*)sentence, length)) {
            gnssNmeaAccount->Publish();
        }
    }
}

DATA_PROC_INIT_DEF(GNSS_NMEA) { gnssNmeaAccount = account; }
