#include "HAL.h"
#include "HAL_Config.h"
#include "HardwareSerial.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "mcu_settings.h"

namespace HAL {
HardwareSerial* gnssSerial = nullptr;

UnicoreUM980* gUm980 = nullptr;
} // namespace HAL

namespace HAL {

void
GNSS_Init() {}

void
GNSS_Configure() {}
} // namespace HAL
