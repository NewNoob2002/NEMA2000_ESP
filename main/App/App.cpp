#include "App.h"
#include "DataProc/DataProc.h"

void
App_Init() {
    /* Initialize the data processing node */
    DataProc_Init();
}

void
App_Uninit() {
    // ACCOUNT_SEND_CMD(SysConfig, SYSCONFIG_CMD_SAVE);
    // ACCOUNT_SEND_CMD(Storage,   STORAGE_CMD_SAVE);
    // ACCOUNT_SEND_CMD(Recorder,  RECORDER_CMD_STOP);
}
