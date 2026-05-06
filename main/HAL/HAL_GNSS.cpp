#include "HAL.h"
#include "HAL_Config.h"
#include "HardwareSerial.h"
#include "Support.h"
#include "Unicore_UM980.h"
#include "mcu_settings.h"

namespace HAL {

HardwareSerial* gnssSerial = nullptr;

UnicoreUM980* gUm980 = nullptr;

void
GNSS_Init() {
    if (settings.printTaskStartStop) {
        systemPrintln("Task pinGnssUartTask started");
    }

    if (gnssSerial == nullptr) {
        gnssSerial = new HardwareSerial(1);
        gnssSerial->setRxBufferSize(settings.uartReceiveBufferSize);
        gnssSerial->setTimeout(settings.serialTimeoutGNSS);
        gnssSerial->setRxFIFOFull(settings.serialGNSSRxFullThreshold);
        gnssSerial->begin(settings.dataPortBaud, SERIAL_8N1, GNSS_RX_PIN, GNSS_TX_PIN);
    }
    if (settings.printTaskStartStop) {
        systemPrintln("Task pinGnssUartTask stopped");
    }

    if (settings.printTaskStartStop) {
        systemPrintln("Unicore GNSS library initialization started");
    }

    if (gUm980 == nullptr) {
        gUm980 = new UnicoreUM980(GNSS_POWER_PIN);
    }

    gUm980->enableDebugLogging(Serial, UnicoreLogLevel::Debug,
                               UNICORE_LOG_COMMAND | UNICORE_LOG_DATA | UNICORE_LOG_TASK);
    // gUm980->setNmeaCallback(OnNmea);
    // gUm980->setRtcmCallback(OnRtcm);
    // gUm980->setBinaryCallback(OnBinary);
    gUm980->init();
    gUm980->powerOn();
    if (gUm980->begin(*gnssSerial, nullptr, &Serial)) {
        if (gUm980->requestVersion(2000) == Unicore_RESULT_RESPONSE_COMMAND_OK) {
            online_devices.gnss = true;
        } else {
            systemPrintln("Failed to get GNSS version");
        }
    }

    if (settings.printTaskStartStop) {
        systemPrintln("Unicore GNSS library initialization stopped");
    }
    // Nothing to do here since the GNSS library is initialized lazily
}

void
GNSS_Configure() {
    if (gUm980) {
        gUm980->configureOnceTime();
    }
}
} // namespace HAL
