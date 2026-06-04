#ifndef __DATA_CENTER_LOG_H
#define __DATA_CENTER_LOG_H

typedef enum {
    DATACENTER_LOG_LEVEL_INFO,
    DATACENTER_LOG_LEVEL_WARN,
    DATACENTER_LOG_LEVEL_ERROR,
} DataCenterLogLevel_t;

typedef void (*DataCenterLogCallback_t)(DataCenterLogLevel_t level, const char* tag, const char* message);

void DataCenterSetLogCallback(DataCenterLogCallback_t callback);
void DataCenterLog(DataCenterLogLevel_t level, const char* tag, const char* format, ...);

#endif
