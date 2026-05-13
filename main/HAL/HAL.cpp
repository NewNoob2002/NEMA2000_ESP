#include "HAL.h"
#include "Bluetooth.h"
#include "HAL_Config.h"
#include "HardwareSerial.h"
#include "mcu_settings.h"
#include "myWIFI.h"
#include "myWebServer.h"
// Display boot times
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
#define MAX_BOOT_TIME_ENTRIES 41
uint8_t bootTimeIndex;
uint32_t bootTime[MAX_BOOT_TIME_ENTRIES];
const char* bootTimeString[MAX_BOOT_TIME_ENTRIES];

#define DMW_b(string)                                                                                                  \
    {                                                                                                                  \
        if (bootTimeIndex < MAX_BOOT_TIME_ENTRIES) {                                                                   \
            bootTime[bootTimeIndex] = millis();                                                                        \
            bootTimeString[bootTimeIndex] = string;                                                                    \
        }                                                                                                              \
        bootTimeIndex += 1;                                                                                            \
    }

static void
showBootTimes() {
    if (1) {
        const uint8_t entryCount = (bootTimeIndex < MAX_BOOT_TIME_ENTRIES) ? bootTimeIndex : MAX_BOOT_TIME_ENTRIES;
        if (entryCount == 0) {
            return;
        }

        const uint32_t totalBootTime = millis();

        // Display the boot times and compute the delta times
        systemPrintln();
        systemPrintln("Time when calling:");
        for (uint8_t index = 0; index < entryCount; index++) {
            systemPrintf("%8lu mSec: %s\r\n", static_cast<unsigned long>(bootTime[index]),
                         bootTimeString[index] ? bootTimeString[index] : "");
        }
        systemPrintln();

        uint32_t deltaTime[MAX_BOOT_TIME_ENTRIES] = {};
        for (uint8_t index = 0; index < entryCount; index++) {
            const uint32_t nextTime = (index + 1U < entryCount) ? bootTime[index + 1U] : totalBootTime;
            deltaTime[index] = nextTime - bootTime[index];
        }

        // Set the initial sort values
        uint8_t sortOrder[MAX_BOOT_TIME_ENTRIES];
        for (uint8_t x = 0; x < entryCount; x++) {
            sortOrder[x] = x;
        }

        // Bubble sort the boot time values
        for (uint8_t x = 0; x + 1U < entryCount; x++) {
            for (uint8_t y = x + 1U; y < entryCount; y++) {
                if (deltaTime[sortOrder[x]] > deltaTime[sortOrder[y]]) {
                    uint8_t temp;
                    temp = sortOrder[y];
                    sortOrder[y] = sortOrder[x];
                    sortOrder[x] = temp;
                }
            }
        }

        // Display the boot times
        systemPrintln("Delta times:");
        for (int index = static_cast<int>(entryCount) - 1; index >= 0; index--) {
            const uint8_t sortedIndex = sortOrder[index];
            systemPrintf("%8lu mSec: %s\r\n", static_cast<unsigned long>(deltaTime[sortedIndex]),
                         bootTimeString[sortedIndex] ? bootTimeString[sortedIndex] : "");
        }
        systemPrintln("--------");
        systemPrintf("%8lu mSec: Total boot time\r\n", static_cast<unsigned long>(totalBootTime));
        systemPrintln();
    }
}

namespace HAL {

TaskHandle_t HAL_Update_Task = nullptr;

void
HAL_Update(void* e) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (1) {
        System_Update();

        bluetoothUpdate();

        // webServerUpdate();

        vTaskDelayUntil(&xLastWakeTime, 10);
    }
}

void
HAL_Init() {
    DMW_b("Serial.begin");
    Serial.begin(115200);
    // Initialize the power module.
    DMW_b("Power_Init");
    Power_Init();
    DMW_b("Power_OnCheck");
    Power_OnCheck();
    // Initialize the i2c modules after the switched peripherals are powered.
#ifdef COMPILE_I2C
    DMW_b("I2C_Init");
    I2C_Init();
    DMW_b("I2C_Scan");
    I2C_Scan(nullptr);
#endif
    DMW_b("FileSystem_Init");
    FileSystem_Init();
    // Init GNSS Module
    DMW_b("GNSS_Init");
    GNSS_Init();
    DMW_b("GNSS_Configure");
    GNSS_Configure();
    // Init Bluetooth
    DMW_b("Bluetooth_Init");
    Bluetooth_Init();

    // DMW_b("wifiUpdateSettings");
    // wifiUpdateSettings();
    // if (settings.wifiConfigOverAP) {
    //     DMW_b("WebServer_Start");
    //     webServerStart();
    // }

    showBootTimes();

    xTaskCreatePinnedToCore(HAL_Update, "HAL_Update", HAL_UPDATE_STACK_SIZE, nullptr, HAL_UPDATE_PROI, &HAL_Update_Task,
                            1);
}
} // namespace HAL
