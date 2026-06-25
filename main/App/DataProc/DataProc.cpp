#include "DataProc.h"
#include "esp_log.h"
#define TAG "[DataCenter]"

static DataCenter center("CENTER");
static bool initialized = false;

DataCenter*
DataProc::Center() {
    return &center;
}

void
DataProc_Init() {
    if (initialized) {
        return;
    }
    ESP_LOGI(TAG, "Begin Init");
#define DP_DEF(NODE_NAME, BUFFER_SIZE, AVERAGE_IN_BYTES)                                                               \
    Account* act##NODE_NAME = new Account(#NODE_NAME, &center, BUFFER_SIZE, AVERAGE_IN_BYTES);
#include "DP_LIST.inc"
#undef DP_DEF

#define DP_DEF(NODE_NAME, BUFFER_SIZE, AVERAGE_IN_BYTES)                                                               \
    do {                                                                                                               \
        (void)AVERAGE_IN_BYTES;                                                                                        \
        DATA_PROC_INIT_DEF(NODE_NAME);                                                                                 \
        _DP_##NODE_NAME##_Init(act##NODE_NAME);                                                                        \
    } while (0)
#include "DP_LIST.inc"
#undef DP_DEF
    ESP_LOGI(TAG, "Init Done");
    initialized = true;
}
