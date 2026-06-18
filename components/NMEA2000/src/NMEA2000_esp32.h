#ifndef _NMEA2000_ESP32_H_
#define _NMEA2000_ESP32_H_

#include <stdint.h>

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include "esp_twai_types.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "NMEA2000.h"

#ifndef ESP32_CAN_TX_PIN
#define ESP32_CAN_TX_PIN GPIO_NUM_22
#endif

#ifndef ESP32_CAN_RX_PIN
#define ESP32_CAN_RX_PIN GPIO_NUM_21
#endif

typedef enum {
    CAN_SPEED_100KBPS = 100,
    CAN_SPEED_125KBPS = 125,
    CAN_SPEED_250KBPS = 250,
    CAN_SPEED_500KBPS = 500,
    CAN_SPEED_800KBPS = 800,
    CAN_SPEED_1000KBPS = 1000
} CAN_speed_t;

class tNMEA2000_esp32 : public tNMEA2000 {
  public:
    struct tCANStatus {
        bool IsOpen;
        bool BusReady;
        uint32_t TxDoneCount;
        uint32_t TxFailCount;
        uint32_t RxFrameCount;
        uint32_t ErrorCount;
        uint32_t StateChangeCount;
        uint32_t LastErrorFlags;
        twai_error_state_t LastState;
        uint16_t TxErrorCount;
        uint16_t RxErrorCount;
        uint32_t TxQueueRemaining;
        uint32_t BusErrorCount;
    };

  private:
    bool IsOpen;
    static bool CanInUse;

  protected:
    struct tCANFrame {
        uint32_t id; // 29-bit CAN identifier for NMEA2000
        uint8_t len; // 0..8
        uint8_t buf[8];
    };

    struct tCtrlEvent {
        enum tType : uint8_t {
            evState = 0,
            evError = 1,
        } Type;

        twai_error_state_t OldState;
        twai_error_state_t NewState;
        twai_error_flags_t ErrorFlags;
    };

    struct tTxInflight {
        twai_frame_t TwaiFrame;
        uint8_t Data[8];
    };

  protected:
    CAN_speed_t speed;
    gpio_num_t TxPin;
    gpio_num_t RxPin;

    QueueHandle_t RxQueue;
    QueueHandle_t TxQueue;
    QueueHandle_t CtrlQueue;

    TaskHandle_t TxTaskHandle;
    TaskHandle_t CtrlTaskHandle;

    twai_node_handle_t Node;

    volatile bool StopRequested;
    volatile bool BusReady;
    volatile bool TxDoneSuccess;
    volatile uint32_t TxDoneCount;
    volatile uint32_t TxFailCount;
    volatile uint32_t RxFrameCount;
    volatile uint32_t ErrorCount;
    volatile uint32_t StateChangeCount;
    volatile uint32_t LastErrorFlags;
    volatile uint32_t LastNotifiedErrorFlags;
    volatile TickType_t LastErrorNotifyTick;
    volatile twai_error_state_t LastState;

    tTxInflight TxInflight;

  protected:
    static void TxTaskEntry(void* arg);
    static void CtrlTaskEntry(void* arg);

    static bool OnTxDone(twai_node_handle_t handle, const twai_tx_done_event_data_t* edata, void* user_ctx);

    static bool OnRxDone(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx);

    static bool OnError(twai_node_handle_t handle, const twai_error_event_data_t* edata, void* user_ctx);

    static bool OnStateChange(twai_node_handle_t handle, const twai_state_change_event_data_t* edata, void* user_ctx);

    void TxTask();
    void CtrlTask();

    uint32_t SpeedToBitrate() const;
    bool CAN_init();
    void CAN_deinit();

  protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char* buf, bool wait_sent = true) override;
    bool CANOpen() override;
    bool CANGetFrame(unsigned long& id, unsigned char& len, unsigned char* buf) override;
    void InitCANFrameBuffers() override;

  public:
    tNMEA2000_esp32(gpio_num_t _TxPin = ESP32_CAN_TX_PIN, gpio_num_t _RxPin = ESP32_CAN_RX_PIN);

    virtual ~tNMEA2000_esp32();

    void CAN_SetSpeed(CAN_speed_t new_speed);
    void ClearCANStatus();
    bool GetCANStatus(tCANStatus& status);
};

#endif
