// src/managers/USBManager.cpp

#include "managers/USBManager.h"
#include <utils/DebugSerial.h>
#include <USB.h>
#include "esp32-hal-tinyusb.h"
#include "esp_event.h"
#include "USBCDC.h"

USBManager::USBManager() {}

USBManager::~USBManager() {
  if (hidQueue) {
    vQueueDelete(hidQueue);
  }
  if (hidMutex) {
    vSemaphoreDelete(hidMutex);
  }
}

bool USBManager::begin() {
  DEBUG_PRINTLN("Initializing USBManager...");

  hidQueue = xQueueCreate(10, sizeof(uint8_t));
  if (!hidQueue) {
    DEBUG_PRINTLN("ERROR: Failed to create HID queue");
    return false;
  }

  hidMutex = xSemaphoreCreateMutex();
  if (!hidMutex) {
    DEBUG_PRINTLN("ERROR: Failed to create HID mutex");
    return false;
  }

  DEBUG_PRINTLN("Configuring USB device descriptor...");

  USB.VID(USB_MY_VID);
  USB.PID(USB_MY_PID);
  USB.productName(USB_PRODUCT);
  USB.manufacturerName(USB_MANUFACTURER);
  USB.serialNumber(USB_SERIAL_NUMBER);
  USB.firmwareVersion(USB_PRODUCT_VERSION);
  USB.usbVersion(0x0200);
  USB.usbPower(500);
  USB.usbClass(0x00);

  DEBUG_PRINTLN("Starting USB subsystem...");

  if (!USB.begin()) {
    DEBUG_PRINTLN("ERROR: Failed to initialize USB subsystem");
    return false;
  }

  DEBUG_PRINTLN("USB subsystem initialized, configuring HID devices...");

  delay(500);

  keyboard.begin();
  delay(100);
  DEBUG_PRINTLN("USB keyboard initialized");

  mouse.begin();
  delay(100);
  DEBUG_PRINTLN("USB mouse initialized");

  gamepad.begin();
  delay(100);
  DEBUG_PRINTLN("USB gamepad initialized");

  delay(1000);

  usbConnected = false;
  
  USB.onEvent([] (void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_id == ARDUINO_USB_CDC_CONNECTED_EVENT) {
      DEBUG_PRINTLN("USB host connected");
    }
    else if (event_id == ARDUINO_USB_CDC_DISCONNECTED_EVENT) {
      DEBUG_PRINTLN("USB host disconnected");
    }
    });

  DEBUG_PRINTLN("USBManager initialized successfully");
  return true;
}