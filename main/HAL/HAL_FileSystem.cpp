#include <LittleFS.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include "App/Config/Config.h"
#include "HAL.h"
#include "Support.h"
#include "mcu_settings.h"

#define TAG "[HAL_FileSystem]"

/**
* @brief Print the partition table of the ESP32
* @return void
*/
void
printPartitionTable(void) {
    systemPrintln("ESP32 Partition table:\n");

    systemPrintln("| Type | Sub |  Offset  |   Size   |       Label      |");
    systemPrintln("| ---- | --- | -------- | -------- | ---------------- |");

    esp_partition_iterator_t pi = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (pi != NULL) {
        do {
            const esp_partition_t* p = esp_partition_get(pi);
            systemPrintf("|  %02x  | %02x  | 0x%06X | 0x%06X | %-16s |\n", p->type, p->subtype, p->address, p->size,
                         p->label);
        } while ((pi = (esp_partition_next(pi))));
    }
}

/**
* @brief Find the partition with the label "littlefs"
* @return bool True if found, False otherwise
*/
bool
findLittlefsPartition(void) {
    systemPrintln("Searching for littlefs partition...");
    esp_partition_iterator_t pi = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (pi != NULL) {
        do {
            const esp_partition_t* p = esp_partition_get(pi);
            if (strcmp(p->label, "littlefs") == 0) {
                return true;
            }
        } while ((pi = (esp_partition_next(pi))));
    }
    return false;
}

/**
 * @brief Initialize the LittleFS file system
 * @return void
 */
void
beginFileSystem() {
    systemPrintln("Initializing fileSystem...");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        systemPrintln("[error] nvs_flash init failed");
        const esp_partition_t* partition =
            esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
        if (partition != NULL) {
            err = esp_partition_erase_range(partition, 0, partition->size);
            if (!err) {
                err = nvs_flash_init();
            }
        }
    }
    if (online_devices.littlefs == true) {
        if (LittleFS.begin(true, MOUNTPOINT, 5, "littlefs") == false) // Format LittleFS if begin fails
        {
            systemPrintln("Error: LittleFS not online");
        } else {
            systemPrintln("LittleFS Started");
            systemPrintf("LittleFS used  / total bytes: %d KB / %d KB\n", LittleFS.usedBytes() >> 10,
                         LittleFS.totalBytes() >> 10);
        }
    }
}

namespace HAL {
void
FileSystem_Init() {
    if (!findLittlefsPartition()) {
        printPartitionTable();
        online_devices.littlefs = false;
        systemPrintln("No LittleFS partition found");
    } else {
        online_devices.littlefs = true;
        systemPrintln("LittleFS partition found");
    }

    beginFileSystem();
}
} // namespace HAL