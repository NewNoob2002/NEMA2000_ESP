/*
 * NMEA0183 GNSS to NMEA2000 gateway for ESP32.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "App/App.h"
#include "HAL/HAL.h"
#include "esp_log.h"

namespace {

constexpr const char* kTaskMonitorTag = "TaskMonitor";
constexpr uint32_t kTaskMonitorPeriodMs = 10000;
constexpr uint32_t kTaskMonitorStackSize = 4096;
constexpr UBaseType_t kTaskMonitorPriority = tskIDLE_PRIORITY + 1;
constexpr BaseType_t kTaskMonitorCore = 1;

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

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
        const UBaseType_t taskCount = uxTaskGetNumberOfTasks();
        const size_t reportBytes = static_cast<size_t>(taskCount) * 96U + 256U;
        char* report = static_cast<char*>(malloc(reportBytes));
        if (report != nullptr) {
            report[0] = '\0';
            vTaskList(report);
            ESP_LOGI(kTaskMonitorTag, "name            state prio stack num core\n%s", report);
            free(report);
        } else {
            ESP_LOGW(kTaskMonitorTag, "failed to allocate task report buffer: %u bytes",
                     static_cast<unsigned>(reportBytes));
        }
#else
        ESP_LOGW(kTaskMonitorTag, "FreeRTOS task list is disabled in sdkconfig");
#endif

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
    // startTaskMonitor();

    while (true) {
        HAL::HAL_Update(nullptr);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
