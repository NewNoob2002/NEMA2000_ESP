#pragma once
#include <Arduino.h>
#include <BLECharacteristic.h>
#include <BLEService.h>

#include "BleSerialConstants.h"
#include "ByteRingBuffer.h"

typedef void (*BLE_CONNECT_CALLBACK)(bool connected);

class BleSerial : public BLECharacteristicCallbacks, public Stream {
  public:
    BleSerial();

    void begin(const char* name, int led_pin = -1);
    void begin(const char* name, const char* service_uuid, const char* rx_uuid, const char* tx_uuid, int led_pin = 13);

    void end();

    bool connected();

    int available() override;
    int peek() override;

    int read() override;
    size_t readBytes(uint8_t* buffer, size_t bufferSize) override;

    size_t write(uint8_t byte) override;
    size_t write(const uint8_t* buffer, size_t bufferSize) override;

    void flush() override;

    // Services
    BLEService* serialService = nullptr;

    // Serial Characteristics
    BLECharacteristic* txCharacteristic = nullptr;
    BLECharacteristic* rxCharacteristic = nullptr;

    char serialServiceUuid[40] = NORDIC_SERVICE_UUID;
    char rxUuid[40] = NORDIC_RX_UUID;
    char txUuid[40] = NORDIC_TX_UUID;

    // BLECharacteristicCallbacks
    void onWrite(BLECharacteristic* pCharacteristic) override;

    void setConnectCallback(BLE_CONNECT_CALLBACK callback);

    bool
    isStarted() const {
        return started;
    }

    size_t
    getTransmitBufferLength() {
        return transmitBufferLength;
    }

  private:
    BleSerial(BleSerial const& other) = delete;      // disable copy constructor
    void operator=(BleSerial const& other) = delete; // disable assign constructor

    ByteRingBuffer<BLE_RX_BUFFER_SIZE> receiveBuffer;
    size_t numAvailableLines = 0;

    unsigned long long lastFlushTime = 0;
    uint8_t transmitBuffer[BLE_MAX_PACKET_SIZE] = {};

    void setupSerialService();
    bool started = false;

    size_t transmitBufferLength = 0;
};
