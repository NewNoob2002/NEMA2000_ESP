#include "Bluetooth.h"
#include <cstdio>
#include <cstring>
#include <stdio.h>
#include "Support.h"
#include "mcu_settings.h"

static BluetoothRadioType_e bluetoothRadioType = BLUETOOTH_RADIO_SPP;
static volatile BTState_e bluetoothState = BT_OFF;
static bool bluetoothEnded = false;

BTSerialInterface* bluetoothSerialSpp = nullptr;
BTSerialInterface* bluetoothSerialBle = nullptr;

BTState_e
bluetoothGetState() {
    return bluetoothState;
}

int
bluetoothRead(uint8_t* buffer, int length) {
    if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
        int bytesRead = 0;

        // Give incoming BLE the priority
        bytesRead = bluetoothSerialBle->readBytes(buffer, length);

        if (bytesRead > 0) {
            return (bytesRead);
        }

        bytesRead = bluetoothSerialSpp->readBytes(buffer, length);

        return (bytesRead);
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
        return bluetoothSerialSpp->readBytes(buffer, length);
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
        return bluetoothSerialBle->readBytes(buffer, length);
    }

    return 0;
}

// Determine if data is available
int
bluetoothRxDataAvailable() {
    if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
        // Give incoming BLE the priority
        if (bluetoothSerialBle->available()) {
            return (bluetoothSerialBle->available());
        }

        return (bluetoothSerialSpp->available());
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
        return bluetoothSerialSpp->available();
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
        return bluetoothSerialBle->available();
    }

    return (0);
}

// Write data to the Bluetooth device
int
bluetoothWrite(const uint8_t* buffer, int length) {
    if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
        // Write to both interfaces
        int bleWrite = bluetoothSerialBle->write(buffer, length);
        int sppWrite = bluetoothSerialSpp->write(buffer, length);
        // We hope and assume both interfaces pass the same byte count
        // through their respective stacks
        // If not, report the larger number
        if (bleWrite >= sppWrite) {
            return (bleWrite);
        }
        return (sppWrite);
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
        return bluetoothSerialSpp->write(buffer, length);
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
        // BLE write does not handle 0 length requests correctly
        if (length > 0) {
            return bluetoothSerialBle->write(buffer, length);
        }
        return length;
    }

    return 0;
}

// Flush Bluetooth device
void
bluetoothFlush() {
    if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
        bluetoothSerialBle->flush();
        bluetoothSerialSpp->flush();
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
        bluetoothSerialSpp->flush();
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
        bluetoothSerialBle->flush();
    }
}

// Test each interface to see if there is a connection
// Return true if one is
bool
bluetoothIsConnected() {
    if (bluetoothGetState() == BT_OFF) {
        return (false);
    }

    if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
        return (bluetoothSerialSpp->connected() == true || bluetoothSerialBle->connected() == true);
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
        return (bluetoothSerialSpp->connected());
    } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
        return (bluetoothSerialBle->connected());
    }

    return (false);
}

// Check if Bluetooth is connected
void
bluetoothUpdate() {
    static uint32_t lastCheck = 0; // Check if connected every 100ms
    if (millis() - lastCheck >= 100) {
        lastCheck = millis();

        // If bluetoothState == BT_OFF, don't call bluetoothIsConnected()

        if ((bluetoothState == BT_NOTCONNECTED) && (bluetoothIsConnected())) {
            bluetoothState = BT_CONNECTED;
        } else if ((bluetoothState == BT_CONNECTED) && (!bluetoothIsConnected())) {
            bluetoothState = BT_NOTCONNECTED;
        }
    }
}

// Begin Bluetooth
void
bluetoothStart() {
    bluetoothStart(true); // Do an online check before (re)starting
}

void
bluetoothStartSkipOnlineCheck() {
    bluetoothStart(false); // Skip the online check, (re)start Bluetooth
}

void
bluetoothStart(bool onlineCheck) {
    if (settings.bluetoothRadioType == BLUETOOTH_RADIO_OFF) {
        return;
    }

    if (bluetoothRadioType == BLUETOOTH_RADIO_OFF) {
        return;
    }

    if (onlineCheck == true) {
        if (online_devices.bluetooth) {
            return; // No need to mess with Bluetooth, it's already online.
        }
    }

    bluetoothState = BT_OFF; // Indicate to tasks that BT is unavailable

    if (!online_devices.bluetooth) {
        bluetoothState = BT_OFF;
        char productName[12] = {0};
        strncpy(productName, productPropertiesTable[productType].name, sizeof(productName));
        productName[sizeof(productName) - 1] = '\0';

        // BLE is limited to ~28 characters in the device name. Shorten
        // platformPrefix if needed.
        if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE || bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            strncpy(productName, "E1-Lite*Ble", sizeof(productName));
            productName[sizeof(productName) - 1] = '\0';
        }

        char productPlanUID[sizeof(productPropertiesTable[productType].productPlanUID)] = {0};
        strncpy(productPlanUID, productPropertiesTable[productType].productPlanUID, sizeof(productPlanUID));
        productPlanUID[sizeof(productPlanUID) - 1] = '\0';

        snprintf(productPropertiesTable[productType].displayName,
                 sizeof(productPropertiesTable[productType].displayName), "%s-%s", productName, productPlanUID);

        if (strlen(productPropertiesTable[productType].displayName) > 28) {
            systemPrintf("Warning! The Bluetooth device name '%s' is %d characters "
                         "long. It may not work in BLE mode.\r\n",
                         productPropertiesTable[productType].displayName,
                         strlen(productPropertiesTable[productType].displayName));
        }

        // Select Bluetooth setup
        if (bluetoothRadioType == BLUETOOTH_RADIO_OFF) {
            return;
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
            bluetoothSerialSpp = new BTClassicSerial();
            // bluetoothSerialBle = new BTLESerial();
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
            bluetoothSerialSpp = new BTClassicSerial();
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            // bluetoothSerialBle = new BTLESerial();
        }

        // while (bluetoothPinned == false) // Wait for task to run once
        //     delay(1);

        bool beginSuccess = true;
        if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
            // localName, isMaster, disableBLE, rxBufferSize, txBufferSize, serviceID, rxID, txID
            beginSuccess &= bluetoothSerialSpp->begin(productPropertiesTable[RTK_S20].displayName, false, false,
                                                      settings.sppRxQueueSize, settings.sppTxQueueSize, 0, 0, 0);
            // localName, isMaster, disableBLE, rxBufferSize, txBufferSize, serviceID, rxID, txID
            beginSuccess &= bluetoothSerialBle->begin(productPropertiesTable[RTK_S20].displayName, false, false,
                                                      settings.sppRxQueueSize, settings.sppTxQueueSize,
                                                      BLE_SERVICE_UUID, BLE_RX_UUID, BLE_TX_UUID);
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
            // Disable BLE
            // localName, isMaster, disableBLE, rxBufferSize, txBufferSize, serviceID, rxID, txID
            beginSuccess &= bluetoothSerialSpp->begin(productPropertiesTable[RTK_S20].displayName, false, true,
                                                      settings.sppRxQueueSize, settings.sppTxQueueSize, 0, 0, 0);
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            // Don't disable BLE
            // localName, isMaster, disableBLE, rxBufferSize, txBufferSize, serviceID, rxID, txID
            beginSuccess &= bluetoothSerialBle->begin(productPropertiesTable[RTK_S20].displayName, false, false,
                                                      settings.sppRxQueueSize, settings.sppTxQueueSize,
                                                      BLE_SERVICE_UUID, BLE_RX_UUID, BLE_TX_UUID);
        }
        if (beginSuccess == false) {
            systemPrintf("An error occurred initializing Bluetooth");
            return;
        }
        online_devices.bluetooth = true;
        bluetoothState = BT_NOTCONNECTED;
        // Set PIN to 1234 so we can connect to older BT devices, but not require a
        // PIN for modern device pairing See issue:
        // https://github.com/sparkfun/SparkFun_RTK_Firmware/issues/5
        // https://github.com/espressif/esp-idf/issues/1541
        //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        // Note: Since version 3.0.0 this library does not support legacy pairing
        // (using fixed PIN consisting of 4 digits). esp_bt_sp_param_t param_type =
        // ESP_BT_SP_IOCAP_MODE;

        // esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE; // Requires pin 1234 on old
        // BT dongle, No prompt on new BT dongle
        // // esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_OUT; //Works but prompts for
        // either pin (old) or 'Does this 6 pin
        // // appear on the device?' (new)

        // esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
        // esp_bt_pin_code_t pin_code;
        // pin_code[0] = '1';
        // pin_code[1] = '2';
        // pin_code[2] = '3';
        // pin_code[3] = '4';
        // esp_bt_gap_set_pin(pin_type, 4, pin_code);

        //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
            // Bluetooth callbacks are handled by bluetoothUpdate()
            bluetoothSerialSpp->setTimeout(250);
            bluetoothSerialBle->setTimeout(10); // Using 10 from BleSerial example
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
            // Bluetooth callbacks are handled by bluetoothUpdate()
            bluetoothSerialSpp->setTimeout(250);
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            // Bluetooth callbacks are handled by bluetoothUpdate()
            bluetoothSerialBle->setTimeout(10);
        }

        if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
            systemPrintf("Bluetooth SPP and BLE broadcasting as: ");
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
            systemPrintf("Bluetooth SPP broadcasting as: ");
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            systemPrintf("Bluetooth Low-Energy broadcasting as: ");
        }

        systemPrintln(productPropertiesTable[RTK_S20].displayName);
    }
}

void
bluetoothStop() {
    if (online_devices.bluetooth) {
        if (bluetoothRadioType == BLUETOOTH_RADIO_SPP_AND_BLE) {
            bluetoothSerialBle->flush();      // Complete any transfers
            bluetoothSerialBle->disconnect(); // Drop any clients
            bluetoothSerialBle->end();        // Release resources

            bluetoothSerialSpp->flush();      // Complete any transfers
            bluetoothSerialSpp->disconnect(); // Drop any clients
            bluetoothSerialSpp->end();        // Release resources
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_SPP) {
            bluetoothSerialSpp->flush();      // Complete any transfers
            bluetoothSerialSpp->disconnect(); // Drop any clients
            bluetoothSerialSpp->end();        // Release resources
        } else if (bluetoothRadioType == BLUETOOTH_RADIO_BLE) {
            bluetoothSerialBle->flush();      // Complete any transfers
            bluetoothSerialBle->disconnect(); // Drop any clients
            bluetoothSerialBle->end();        // Release resources
        }

        systemPrintln("Bluetooth turned off");
    }
    // bluetoothIncomingRTCM = false;
}
