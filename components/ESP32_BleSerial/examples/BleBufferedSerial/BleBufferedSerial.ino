
/**
 * Bluetooth LE Buffered Serial Example
 *
 * Shows 3 threads writing small messages
 * to a single BLE Serial port at high rate.
 *
 * The messages are grouped and sent as a single packet
 * with size that equals the MTU.
 *
 * Avinab Malla
 * 6 October 2025
 **/

#include <Arduino.h>
#include <BleSerial.h>
#include <BleBatteryService.h>
#include <esp_attr.h>
#include <esp_task_wdt.h>
#include <driver/rtc_io.h>

#include "BleBufferedSerial.h"


const char *BLE_SERIAL2_SERVICE_UUID = "7e400001-b5a3-f393-e0a9-e50e24dcca9e";
const char *BLE_RX2_UUID = "7e400002-b5a3-f393-e0a9-e50e24dcca9e";
const char *BLE_TX2_UUID = "7e400003-b5a3-f393-e0a9-e50e24dcca9e";


BleBufferedSerial SerialBT1;

int serial1Count = 0;
int serial2Count = 0;
int serial3Count = 0;

void serial1Task(void* e){
	while(true){
		SerialBT1.printf("Hello Serial1 %d\r\n",serial1Count++);
		delay(5);
	}
}


void serial2Task(void* e){
	while(true){
		SerialBT1.printf("Hello Serial2 %d\r\n",serial2Count++);
		delay(5);
	}
}

void serial3Task(void* e){
	while(true){
		SerialBT1.printf("Hello Serial3 %d\r\n",serial3Count++);
		delay(5);
	}
}


void setup() {
	Serial.begin(115200);
	SerialBT1.begin("TEST_BT");

	xTaskCreate(serial1Task, "Serial1", 8192, nullptr, 0, nullptr);
	xTaskCreate(serial2Task, "Serial2", 8192, nullptr, 0, nullptr);
	xTaskCreate(serial3Task, "Serial3", 8192, nullptr, 0, nullptr);
}

void loop() {
	vTaskDelete(nullptr);
}