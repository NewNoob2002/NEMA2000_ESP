#include "NMEA2000_esp32.h"
#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

static const char* TAG = "[NMEA2000_esp32]";

bool tNMEA2000_esp32::CanInUse = false;

// ----------------------------------------------------------------------------

void
LogTwaiErrorFlags(twai_error_flags_t flags) {
    ESP_LOGW(TAG, "TWAI error flags=0x%08" PRIx32 " arb_lost=%u bit=%u form=%u stuff=%u ack=%u", flags.val,
             flags.arb_lost, flags.bit_err, flags.form_err, flags.stuff_err, flags.ack_err);
}

// ----------------------------------------------------------------------------

tNMEA2000_esp32::tNMEA2000_esp32(gpio_num_t _TxPin, gpio_num_t _RxPin)
    : tNMEA2000(), IsOpen(false), speed(CAN_SPEED_250KBPS), TxPin(_TxPin), RxPin(_RxPin), RxQueue(NULL), TxQueue(NULL),
      CtrlQueue(NULL), TxTaskHandle(NULL), CtrlTaskHandle(NULL), Node(NULL), StopRequested(false), BusReady(false),
      TxDoneSuccess(false), TxDoneCount(0), TxFailCount(0), RxFrameCount(0), ErrorCount(0), StateChangeCount(0),
      LastErrorFlags(0), LastNotifiedErrorFlags(0), LastErrorNotifyTick(0), LastState(TWAI_ERROR_ACTIVE) {
    memset(&TxInflight, 0, sizeof(TxInflight));
}

tNMEA2000_esp32::~tNMEA2000_esp32() {
    CAN_deinit();

    if (RxQueue != NULL) {
        vQueueDelete(RxQueue);
        RxQueue = NULL;
    }
    if (TxQueue != NULL) {
        vQueueDelete(TxQueue);
        TxQueue = NULL;
    }
    if (CtrlQueue != NULL) {
        vQueueDelete(CtrlQueue);
        CtrlQueue = NULL;
    }
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::CAN_SetSpeed(CAN_speed_t new_speed) {
    if (IsOpen) {
        ESP_LOGW(TAG, "Cannot change speed while CAN is open");
        return;
    }
    speed = new_speed;
}

uint32_t
tNMEA2000_esp32::SpeedToBitrate() const {
    switch (speed) {
        case CAN_SPEED_100KBPS: return 100000;
        case CAN_SPEED_125KBPS: return 125000;
        case CAN_SPEED_250KBPS: return 250000;
        case CAN_SPEED_500KBPS: return 500000;
        case CAN_SPEED_800KBPS: return 800000;
        case CAN_SPEED_1000KBPS: return 1000000;
        default: return 250000; // NMEA2000 default
    }
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::ClearCANStatus() {
    TxDoneCount = 0;
    TxFailCount = 0;
    RxFrameCount = 0;
    ErrorCount = 0;
    StateChangeCount = 0;
    LastErrorFlags = 0;
    LastNotifiedErrorFlags = 0;
    LastErrorNotifyTick = 0;
    LastState = TWAI_ERROR_ACTIVE;
}

bool
tNMEA2000_esp32::GetCANStatus(tCANStatus& status) {
    memset(&status, 0, sizeof(status));

    status.IsOpen = IsOpen;
    status.BusReady = BusReady;
    status.TxDoneCount = TxDoneCount;
    status.TxFailCount = TxFailCount;
    status.RxFrameCount = RxFrameCount;
    status.ErrorCount = ErrorCount;
    status.StateChangeCount = StateChangeCount;
    status.LastErrorFlags = LastErrorFlags;
    status.LastState = LastState;

    if (Node == NULL) {
        return IsOpen;
    }

    twai_node_status_t node_status;
    twai_node_record_t node_record;
    memset(&node_status, 0, sizeof(node_status));
    memset(&node_record, 0, sizeof(node_record));

    esp_err_t err = twai_node_get_info(Node, &node_status, &node_record);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "twai_node_get_info failed: %s", esp_err_to_name(err));
        return false;
    }

    status.LastState = node_status.state;
    status.TxErrorCount = node_status.tx_error_count;
    status.RxErrorCount = node_status.rx_error_count;
    status.TxQueueRemaining = node_status.tx_queue_remaining;
    status.BusErrorCount = node_record.bus_err_num;
    return true;
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::InitCANFrameBuffers() {
    if (MaxCANReceiveFrames < 10) {
        MaxCANReceiveFrames = 50;
    }
    if (MaxCANSendFrames < 10) {
        MaxCANSendFrames = 40;
    }
    uint16_t CANGlobalBufSize = (MaxCANSendFrames > 4) ? (MaxCANSendFrames - 4) : 8;
    MaxCANSendFrames = 4;

    if (RxQueue == NULL) {
        RxQueue = xQueueCreate(MaxCANReceiveFrames, sizeof(tCANFrame));
    }
    if (TxQueue == NULL) {
        TxQueue = xQueueCreate(CANGlobalBufSize, sizeof(tCANFrame));
    }
    if (CtrlQueue == NULL) {
        CtrlQueue = xQueueCreate(8, sizeof(tCtrlEvent));
    }

    tNMEA2000::InitCANFrameBuffers();
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::CANOpen() {
    if (IsOpen) {
        return true;
    }
    if (CanInUse) {
        ESP_LOGE(TAG, "CANOpen rejected: another tNMEA2000_esp32 instance already owns TWAI");
        return false;
    }

    if (RxQueue == NULL || TxQueue == NULL || CtrlQueue == NULL) {
        InitCANFrameBuffers();
    }

    if (RxQueue == NULL || TxQueue == NULL || CtrlQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create queues");
        return false;
    }

    if (!CAN_init()) {
        CAN_deinit();
        return false;
    }

    IsOpen = true;
    CanInUse = true;
    return true;
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::CANGetFrame(unsigned long& id, unsigned char& len, unsigned char* buf) {
    tCANFrame frame;
    if (xQueueReceive(RxQueue, &frame, 0) == pdTRUE) {
        id = frame.id;
        len = frame.len;
        memcpy(buf, frame.buf, frame.len);
        return true;
    }
    return false;
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::CANSendFrame(unsigned long id, unsigned char len, const unsigned char* buf, bool wait_sent) {
    if (!IsOpen || TxQueue == NULL) {
        return false;
    }
    if (buf == NULL && len > 0) {
        return false;
    }

    tCANFrame frame;
    frame.id = static_cast<uint32_t>(id);
    frame.len = (len > 8) ? 8 : len;
    if (frame.len > 0) {
        memcpy(frame.buf, buf, frame.len);
    }

    TickType_t wait_ticks = wait_sent ? pdMS_TO_TICKS(50) : 0;
    return (xQueueSendToBack(TxQueue, &frame, wait_ticks) == pdTRUE);
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::CAN_init() {
    StopRequested = false;
    BusReady = false;
    TxDoneSuccess = false;

    twai_onchip_node_config_t node_config;
    memset(&node_config, 0, sizeof(node_config));

    node_config.io_cfg.tx = TxPin;
    node_config.io_cfg.rx = RxPin;
    node_config.io_cfg.quanta_clk_out = GPIO_NUM_NC;
    node_config.io_cfg.bus_off_indicator = GPIO_NUM_NC;

    node_config.bit_timing.bitrate = SpeedToBitrate();
    node_config.bit_timing.sp_permill = 875;
    node_config.tx_queue_depth = 5;
    node_config.intr_priority = 0;
#if defined(NMEA2000_ESP32_TWAI_SELF_TEST) && NMEA2000_ESP32_TWAI_SELF_TEST
    node_config.flags.enable_self_test = 1;
#endif
#if defined(NMEA2000_ESP32_TWAI_LOOPBACK) && NMEA2000_ESP32_TWAI_LOOPBACK
    node_config.flags.enable_loopback = 1;
#endif
    node_config.flags.no_receive_rtr = 1;

    esp_err_t err = twai_new_node_onchip(&node_config, &Node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip failed: %s", esp_err_to_name(err));
        Node = NULL;
        return false;
    }

    twai_event_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_tx_done = &tNMEA2000_esp32::OnTxDone;
    cbs.on_rx_done = &tNMEA2000_esp32::OnRxDone;
    cbs.on_error = &tNMEA2000_esp32::OnError;
    cbs.on_state_change = &tNMEA2000_esp32::OnStateChange;

    err = twai_node_register_event_callbacks(Node, &cbs, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_register_event_callbacks failed: %s", esp_err_to_name(err));
        return false;
    }

    // NMEA2000 只关心 29-bit 扩展帧，且不使用过滤，所有帧都接受
    twai_mask_filter_config_t filter_cfg;
    memset(&filter_cfg, 0, sizeof(filter_cfg));
    filter_cfg.id = 0;
    filter_cfg.mask = 0;
    filter_cfg.is_ext = true;

    err = twai_node_config_mask_filter(Node, 0, &filter_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_config_mask_filter failed: %s", esp_err_to_name(err));
        return false;
    }

    if (xTaskCreate(&tNMEA2000_esp32::TxTaskEntry, "n2k_twai_tx", 2048, this, tskIDLE_PRIORITY + 5, &TxTaskHandle)
        != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX task");
        return false;
    }

    if (xTaskCreate(&tNMEA2000_esp32::CtrlTaskEntry, "n2k_twai_ctrl", 3072, this, tskIDLE_PRIORITY + 6, &CtrlTaskHandle)
        != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CTRL task");
        return false;
    }

    err = twai_node_enable(Node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_enable failed: %s", esp_err_to_name(err));
        return false;
    }

    BusReady = true;
    ClearCANStatus();
    ESP_LOGI(TAG, "TWAI node started on TX=%d RX=%d bitrate=%lu", static_cast<int>(TxPin), static_cast<int>(RxPin),
             static_cast<unsigned long>(SpeedToBitrate()));

    return true;
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::CAN_deinit() {
    StopRequested = true;
    BusReady = false;

    if (TxTaskHandle != NULL) {
        xTaskNotifyGive(TxTaskHandle);
    }

    if (Node != NULL) {
        (void)twai_node_disable(Node);
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    if (TxTaskHandle != NULL) {
        vTaskDelete(TxTaskHandle);
        TxTaskHandle = NULL;
    }

    if (CtrlTaskHandle != NULL) {
        vTaskDelete(CtrlTaskHandle);
        CtrlTaskHandle = NULL;
    }

    if (Node != NULL) {
        (void)twai_node_delete(Node);
        Node = NULL;
    }

    IsOpen = false;
    CanInUse = false;
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::TxTaskEntry(void* arg) {
    static_cast<tNMEA2000_esp32*>(arg)->TxTask();
}

void
tNMEA2000_esp32::CtrlTaskEntry(void* arg) {
    static_cast<tNMEA2000_esp32*>(arg)->CtrlTask();
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::TxTask() {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (!StopRequested) {
        tCANFrame frame;
        if (xQueueReceive(TxQueue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        while (!StopRequested && !BusReady) {
            vTaskDelayUntil(&xLastWakeTime, 20);
        }
        if (StopRequested) {
            break;
        }

        memset(&TxInflight, 0, sizeof(TxInflight));
        memcpy(TxInflight.Data, frame.buf, frame.len);

        TxInflight.TwaiFrame.header.id = frame.id;
        TxInflight.TwaiFrame.header.ide = 1;
        TxInflight.TwaiFrame.header.rtr = 0;
        TxInflight.TwaiFrame.header.fdf = 0;
        TxInflight.TwaiFrame.header.brs = 0;
        TxInflight.TwaiFrame.header.dlc = twaifd_len2dlc(frame.len);
        TxInflight.TwaiFrame.buffer = TxInflight.Data;
        TxInflight.TwaiFrame.buffer_len = frame.len;

        (void)ulTaskNotifyTake(pdTRUE, 0);
        TxDoneSuccess = false;

        while (!StopRequested) {
            esp_err_t err = twai_node_transmit(Node, &TxInflight.TwaiFrame, 0);
            if (err == ESP_OK) {
                break;
            }
            vTaskDelayUntil(&xLastWakeTime, 5);
        }

        if (StopRequested) {
            break;
        }

        // 等待 ISR on_tx_done 通知发送完成
        while (!StopRequested) {
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0) {
                break;
            }
        }
    }

    TxTaskHandle = NULL;
    vTaskDelete(NULL);
}

// ----------------------------------------------------------------------------

void
tNMEA2000_esp32::CtrlTask() {
    while (!StopRequested) {
        tCtrlEvent evt;
        if (xQueueReceive(CtrlQueue, &evt, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        if (evt.Type == tCtrlEvent::evState) {
            if (evt.NewState == TWAI_ERROR_BUS_OFF) {
                BusReady = false;
                ESP_LOGW(TAG, "TWAI BUS-OFF, starting recovery");
                (void)twai_node_recover(Node);
            } else if (evt.OldState == TWAI_ERROR_BUS_OFF && evt.NewState == TWAI_ERROR_ACTIVE) {
                BusReady = true;
                ESP_LOGI(TAG, "TWAI recovered from BUS-OFF");
            }
        } else if (evt.Type == tCtrlEvent::evError) {
            LogTwaiErrorFlags(evt.ErrorFlags);
        }
    }

    CtrlTaskHandle = NULL;
    vTaskDelete(NULL);
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::OnTxDone(twai_node_handle_t handle, const twai_tx_done_event_data_t* edata, void* user_ctx) {
    (void)handle;
    tNMEA2000_esp32* self = static_cast<tNMEA2000_esp32*>(user_ctx);
    BaseType_t hp_task_woken = pdFALSE;
    if (self == nullptr || edata == nullptr) {
        return false;
    }

    self->TxDoneSuccess = edata->is_tx_success;
    if (edata->is_tx_success) {
        self->TxDoneCount = self->TxDoneCount + 1;
    } else {
        self->TxFailCount = self->TxFailCount + 1;
    }
    if (self->TxTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(self->TxTaskHandle, &hp_task_woken);
    }

    return (hp_task_woken == pdTRUE);
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::OnRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx) {
    (void)edata;
    tNMEA2000_esp32* self = static_cast<tNMEA2000_esp32*>(user_ctx);
    BaseType_t hp_task_woken = pdFALSE;
    if (self == nullptr || self->RxQueue == nullptr) {
        return false;
    }

    tCANFrame frame;
    memset(&frame, 0, sizeof(frame));

    twai_frame_t rx_frame;
    memset(&rx_frame, 0, sizeof(rx_frame));
    rx_frame.buffer = frame.buf;
    rx_frame.buffer_len = sizeof(frame.buf);

    if (twai_node_receive_from_isr(handle, &rx_frame) == ESP_OK) {
        if (!rx_frame.header.ide) {
            return false; // NMEA2000 只处理 29-bit extended frame
        }
        if (rx_frame.header.rtr || rx_frame.header.fdf) {
            return false; // 忽略 RTR/FD
        }

        frame.id = rx_frame.header.id;
        frame.len = static_cast<uint8_t>(twaifd_dlc2len(rx_frame.header.dlc));
        if (frame.len > 8) {
            frame.len = 8;
        }

        self->RxFrameCount = self->RxFrameCount + 1;
        (void)xQueueSendToBackFromISR(self->RxQueue, &frame, &hp_task_woken);
    }

    return (hp_task_woken == pdTRUE);
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::OnError(twai_node_handle_t handle, const twai_error_event_data_t* edata, void* user_ctx) {
    (void)handle;
    tNMEA2000_esp32* self = static_cast<tNMEA2000_esp32*>(user_ctx);
    BaseType_t hp_task_woken = pdFALSE;
    if (self == nullptr || edata == nullptr) {
        return false;
    }

    tCtrlEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.Type = tCtrlEvent::evError;
    evt.ErrorFlags = edata->err_flags;
    self->ErrorCount = self->ErrorCount + 1;
    self->LastErrorFlags = edata->err_flags.val;

    const TickType_t now = xTaskGetTickCountFromISR();
    const TickType_t minNotifyTicks = pdMS_TO_TICKS(1000);
    const bool flagsChanged = (self->LastNotifiedErrorFlags != edata->err_flags.val);
    const bool notifyExpired = ((TickType_t)(now - self->LastErrorNotifyTick) >= minNotifyTicks);

    if ((flagsChanged || notifyExpired) && self->CtrlQueue != NULL) {
        if (xQueueSendToBackFromISR(self->CtrlQueue, &evt, &hp_task_woken) == pdTRUE) {
            self->LastNotifiedErrorFlags = edata->err_flags.val;
            self->LastErrorNotifyTick = now;
        }
    }

    return (hp_task_woken == pdTRUE);
}

// ----------------------------------------------------------------------------

bool
tNMEA2000_esp32::OnStateChange(twai_node_handle_t handle, const twai_state_change_event_data_t* edata, void* user_ctx) {
    (void)handle;
    tNMEA2000_esp32* self = static_cast<tNMEA2000_esp32*>(user_ctx);
    BaseType_t hp_task_woken = pdFALSE;
    if (self == nullptr || edata == nullptr || self->CtrlQueue == nullptr) {
        return false;
    }

    tCtrlEvent evt;
    memset(&evt, 0, sizeof(evt));
    evt.Type = tCtrlEvent::evState;
    evt.OldState = edata->old_sta;
    evt.NewState = edata->new_sta;
    self->StateChangeCount = self->StateChangeCount + 1;
    self->LastState = edata->new_sta;

    (void)xQueueSendToBackFromISR(self->CtrlQueue, &evt, &hp_task_woken);
    return (hp_task_woken == pdTRUE);
}
