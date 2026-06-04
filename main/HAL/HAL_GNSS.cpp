#include "DataProc/DataProc_Def.h"
#include "GNSS.h"
#include "HAL.h"
#include "HardwareSerial.h"
#include "Unicore_UM980.h"

void
userGnssNmeaCallback(const char* sentence, uint16_t length, void* userdata) {
    if (sentence[0] == '$' && sentence[1] == 'G') {
        DP_GNSS_DataHandler(sentence, length, userdata);
    }
}

namespace HAL {
HardwareSerial* gnssSerial = nullptr;

UnicoreUM980* gUm980 = nullptr;
} // namespace HAL

namespace HAL {

void
gnssInit() {
    gnssBegin(gnssSerial, gUm980);
    if (gUm980) {
        gUm980->setUserNmeaCallback(userGnssNmeaCallback);
    }
}

void
gnssUpdate() {
    gnssUpdate(gUm980);
}
} // namespace HAL
