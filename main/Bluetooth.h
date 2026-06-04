#pragma once

#include "BluetoothSelect.h"
#include "mcu_typedef.h"

#define BLE_SERVICE_UUID                 "6e401819-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_RX_UUID                      "6e402AAD-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_TX_UUID                      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define BLE_COMMAND_SERVICE_UUID         "7e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_COMMAND_RX_UUID              "7e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_COMMAND_TX_UUID              "7e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define AMOUNT_OF_RING_BUFFER_TO_DISCARD (settings.ringBufferSizeofBT >> 2)
#define AVERAGE_LENGTH_IN_BYTES          64

BTState_e bluetoothGetState();

void bluetoothSetDataInterfaceEnabled(bool enabled);
bool bluetoothDataInterfaceIsEnabled();

int bluetoothRead(uint8_t* buffer, int length);

// Determine if data is available
int bluetoothRxDataAvailable();

// Write data to the Bluetooth device
int bluetoothWrite(const uint8_t* buffer, int length);

// Write data to the Bluetooth device
int bluetoothWrite(uint8_t value);

// Flush Bluetooth device
void bluetoothFlush();

// Test each interface to see if there is a connection
// Return true if one is
bool bluetoothIsConnected();
// Check if Bluetooth is connected
void bluetoothUpdate();

void bluetoothStart();
void bluetoothStartSkipOnlineCheck();
void bluetoothStart(bool onlineCheck);

void bluetoothEnd();
void bluetoothStop();
void bluetoothEndCommon(bool endMe);
