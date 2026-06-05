/*
 * NMEA0183 GNSS to NMEA2000 gateway for ESP32.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>
#include <cstdio>
#include "App/App.h"
#include "HAL/HAL.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

constexpr const char* kTaskMonitorTag = "TaskMonitor";
constexpr uint32_t kTaskMonitorPeriodMs = 10000;
constexpr uint32_t kTaskMonitorStackSize = 4096;
constexpr UBaseType_t kTaskMonitorPriority = tskIDLE_PRIORITY + 1;
constexpr BaseType_t kTaskMonitorCore = 1;
constexpr size_t kHeapWarnBytes = 16U * 1024U;
constexpr configSTACK_DEPTH_TYPE kStackWarnWords = 768;

const char*
taskStateText(const eTaskState state) {
    switch (state) {
    case eRunning:
        return "running";
    case eReady:
        return "ready";
    case eBlocked:
        return "blocked";
    case eSuspended:
        return "suspended";
    case eDeleted:
        return "deleted";
    case eInvalid:
    default:
        return "invalid";
    }
}

void
formatCoreId(const BaseType_t coreId, char* buffer, const size_t bufferLength) {
    if (coreId == tskNO_AFFINITY) {
        snprintf(buffer, bufferLength, "any");
    } else {
        snprintf(buffer, bufferLength, "%ld", static_cast<long>(coreId));
    }
}

void
logTaskDetails() {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    const UBaseType_t taskCount = uxTaskGetNumberOfTasks();
    TaskStatus_t* taskStatus =
        static_cast<TaskStatus_t*>(malloc(static_cast<size_t>(taskCount) * sizeof(TaskStatus_t)));
    if (taskStatus == nullptr) {
        ESP_LOGW(kTaskMonitorTag, "failed to allocate task status buffer: %u bytes",
                 static_cast<unsigned>(static_cast<size_t>(taskCount) * sizeof(TaskStatus_t)));
        return;
    }

    configRUN_TIME_COUNTER_TYPE totalRunTime = 0;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    const UBaseType_t reportedTasks = uxTaskGetSystemState(taskStatus, taskCount, &totalRunTime);
#else
    const UBaseType_t reportedTasks = uxTaskGetSystemState(taskStatus, taskCount, nullptr);
#endif

    ESP_LOGI(kTaskMonitorTag, "task detail: name state prio core stack_free task_no runtime pct");
    for (UBaseType_t index = 0; index < reportedTasks; ++index) {
        char coreText[8] = {};
        formatCoreId(taskStatus[index].xCoreID, coreText, sizeof(coreText));

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
        const uint64_t runtime = static_cast<uint64_t>(taskStatus[index].ulRunTimeCounter);
        const uint64_t percentX100 =
            (totalRunTime > 0) ? ((runtime * 10000ULL) / static_cast<uint64_t>(totalRunTime)) : 0ULL;
        if (taskStatus[index].usStackHighWaterMark < kStackWarnWords) {
            ESP_LOGW(kTaskMonitorTag, "%-16s %-9s %2u %4s %10u %7u %10llu %3llu.%02llu%% stack-low",
                     taskStatus[index].pcTaskName, taskStateText(taskStatus[index].eCurrentState),
                     static_cast<unsigned>(taskStatus[index].uxCurrentPriority), coreText,
                     static_cast<unsigned>(taskStatus[index].usStackHighWaterMark),
                     static_cast<unsigned>(taskStatus[index].xTaskNumber), runtime, percentX100 / 100ULL,
                     percentX100 % 100ULL);
        } else {
            ESP_LOGI(kTaskMonitorTag, "%-16s %-9s %2u %4s %10u %7u %10llu %3llu.%02llu%%",
                     taskStatus[index].pcTaskName, taskStateText(taskStatus[index].eCurrentState),
                     static_cast<unsigned>(taskStatus[index].uxCurrentPriority), coreText,
                     static_cast<unsigned>(taskStatus[index].usStackHighWaterMark),
                     static_cast<unsigned>(taskStatus[index].xTaskNumber), runtime, percentX100 / 100ULL,
                     percentX100 % 100ULL);
        }
#else
        if (taskStatus[index].usStackHighWaterMark < kStackWarnWords) {
            ESP_LOGW(kTaskMonitorTag, "%-16s %-9s %2u %4s %10u %7u runtime-disabled stack-low",
                     taskStatus[index].pcTaskName, taskStateText(taskStatus[index].eCurrentState),
                     static_cast<unsigned>(taskStatus[index].uxCurrentPriority), coreText,
                     static_cast<unsigned>(taskStatus[index].usStackHighWaterMark),
                     static_cast<unsigned>(taskStatus[index].xTaskNumber));
        } else {
            ESP_LOGI(kTaskMonitorTag, "%-16s %-9s %2u %4s %10u %7u runtime-disabled",
                     taskStatus[index].pcTaskName, taskStateText(taskStatus[index].eCurrentState),
                     static_cast<unsigned>(taskStatus[index].uxCurrentPriority), coreText,
                     static_cast<unsigned>(taskStatus[index].usStackHighWaterMark),
                     static_cast<unsigned>(taskStatus[index].xTaskNumber));
        }
#endif
    }

    free(taskStatus);
#else
    ESP_LOGW(kTaskMonitorTag, "FreeRTOS trace facility is disabled in sdkconfig");
#endif
}

void __attribute__((unused))
taskMonitor(void* arg) {
    (void)arg;

    while (true) {
        const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t minEverFreeInternal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

        ESP_LOGI(kTaskMonitorTag, "heap internal free=%u min=%u largest=%u tasks=%u",
                 static_cast<unsigned>(freeInternal), static_cast<unsigned>(minEverFreeInternal),
                 static_cast<unsigned>(largestBlock), static_cast<unsigned>(uxTaskGetNumberOfTasks()));

        if (minEverFreeInternal < kHeapWarnBytes) {
            ESP_LOGW(kTaskMonitorTag, "heap minimum low: min=%u threshold=%u",
                     static_cast<unsigned>(minEverFreeInternal), static_cast<unsigned>(kHeapWarnBytes));
        }
        if (largestBlock < kHeapWarnBytes) {
            ESP_LOGW(kTaskMonitorTag, "heap largest block low: largest=%u threshold=%u",
                     static_cast<unsigned>(largestBlock), static_cast<unsigned>(kHeapWarnBytes));
        }

        logTaskDetails();

        vTaskDelay(pdMS_TO_TICKS(kTaskMonitorPeriodMs));
    }
}

void __attribute__((unused))
startTaskMonitor() {
    TaskHandle_t monitorTask = nullptr;
    const BaseType_t result = xTaskCreatePinnedToCore(taskMonitor, "task_monitor", kTaskMonitorStackSize, nullptr,
                                                      kTaskMonitorPriority, &monitorTask, kTaskMonitorCore);
    if (result != pdPASS) {
        ESP_LOGE(kTaskMonitorTag, "failed to create task monitor");
    }
}

} // namespace

extern "C" void
app_main(void) {
    HAL::HAL_Init();
    App_Init();
    startTaskMonitor();

    while (true) {
        HAL::HAL_Update(nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
