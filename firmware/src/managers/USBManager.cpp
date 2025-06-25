// src/managers/USBManager.cpp

#include "managers/USBManager.h"
#include "managers/SystemManager.h"
#include "managers/PowerManager.h"
#include <classes/GripDeckVendorHID.h>
#include <utils/DebugSerial.h>

#include <USB.h>
#include "esp32-hal-tinyusb.h"
#include "esp_event.h"
#include "USBCDC.h"

extern SystemManager* systemManager;
extern PowerManager* powerManager;

extern const uint8_t vendorReportDescriptor[];
extern const size_t vendorReportDescriptorSize;

USBManager* USBManager::instance = nullptr;

USBManager::USBManager() : usbConnected(false), initialized(false), sequenceCounter(0) {
  instance = this;
  hidQueue = nullptr;
  hidMutex = nullptr;
  vendorDevice = nullptr;
  vendorResponseReady = false;
  memset(&vendorResponse, 0, sizeof(vendorResponse));
}

USBManager::~USBManager() {
  if (instance == this) {
    instance = nullptr;
  }
  if (hidQueue) {
    vQueueDelete(hidQueue);
  }
  if (hidMutex) {
    vSemaphoreDelete(hidMutex);
  }
  if (vendorDevice) {
    delete vendorDevice;
    vendorDevice = nullptr;
  }
}

void USBManager::handleUSBEvent(arduino_usb_event_t event, void* event_data) {
  if (!isUSBHIDEnabled()) {
    return;
  }

  switch (event) {
  case ARDUINO_USB_STARTED_EVENT:
    DEBUG_PRINTLN("USB device enumerated by host");
    usbConnected = true;
    break;
  case ARDUINO_USB_STOPPED_EVENT:
    DEBUG_PRINTLN("USB device disconnected from host");
    usbConnected = false;
    break;
  case ARDUINO_USB_SUSPEND_EVENT:
    DEBUG_PRINTLN("USB device suspended");
    // Don't change connection state - device is still enumerated
    break;
  case ARDUINO_USB_RESUME_EVENT:
    DEBUG_PRINTLN("USB device resumed");
    // Device was already connected, just resumed
    usbConnected = true;
    break;
  default:
    DEBUG_VERBOSE_PRINTF("Unknown USB event: %d\n", event);
  }
}

bool USBManager::initializeFreeRTOSResources() {
  if (!isUSBHIDEnabled()) {
    initialized = true;
    return true;
  }

  if (initialized) {
    return true;
  }

  DEBUG_PRINTLN("Initializing FreeRTOS resources for USBManager...");

  hidQueue = xQueueCreate(10, sizeof(HIDMessage));
  if (!hidQueue) {
    DEBUG_PRINTLN("ERROR: Failed to create HID queue");
    return false;
  }

  hidMutex = xSemaphoreCreateMutex();
  if (!hidMutex) {
    DEBUG_PRINTLN("ERROR: Failed to create HID mutex");
    vQueueDelete(hidQueue);
    hidQueue = nullptr;
    return false;
  }

  initialized = true;
  DEBUG_PRINTLN("FreeRTOS resources initialized successfully");
  return true;
}

bool USBManager::begin() {
  DEBUG_PRINTLN("Initializing USBManager...");

  if (!isUSBHIDEnabled()) {
    DEBUG_PRINTLN("USBManager: USB HID functionality disabled");
    usbConnected = false;
    initialized = true;
    return true;
  }

  DEBUG_PRINTF("HIDMessage size: %d bytes\n", sizeof(HIDMessage));
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

  if (!vendorDevice) {
    DEBUG_PRINTLN("Creating vendor HID device...");
    vendorDevice = new GripDeckVendorHID(this, &hid);
    if (vendorDevice) {
      DEBUG_PRINTLN("Vendor HID device created successfully");
    }
    else {
      DEBUG_PRINTLN("ERROR: Failed to create vendor HID device");
    }
  }

  if (!USB.begin()) {
    DEBUG_PRINTLN("ERROR: Failed to initialize USB subsystem");
    return false;
  }

  DEBUG_PRINTLN("USB subsystem initialized, configuring HID devices...");

  delay(500);

#if DEBUG_ENABLED
  USB.enableDFU();
  DEBUG_PRINTLN("DFU mode enabled");
  delay(200);
#endif

  keyboard.begin();
  delay(100);
  DEBUG_PRINTLN("USB keyboard initialized");

  mouse.begin();
  delay(100);
  DEBUG_PRINTLN("USB mouse initialized");

  gamepad.begin();
  DEBUG_PRINTLN("USB gamepad initialized");

  consumerControl.begin();
  DEBUG_PRINTLN("USB consumer control initialized");

  hid.begin();
  delay(100);
  DEBUG_PRINTLN("USB HID subsystem initialized");

  if (vendorDevice) {
    DEBUG_PRINTLN("Initializing vendor HID device...");
    vendorDevice->begin();
    DEBUG_PRINTLN("Vendor HID device initialized");
  }

  usbConnected = false;

  // Check if USB is already connected on startup after a longer delay
  // This prevents crashes during early initialization
  delay(1000);
  checkInitialUSBStatus();

  DEBUG_PRINTLN("USBManager basic initialization complete");
  DEBUG_PRINTLN("FreeRTOS resources will be initialized when first task runs");

  return true;
}

void USBManager::update() {
  if (!isUSBHIDEnabled()) {
    return;
  }

  // So we don't initialize FreeRTOS resources too early and cause issues like before, it would not wake up from deep sleep
  if (!initialized && !initializeFreeRTOSResources()) {
    DEBUG_PRINTLN("ERROR: Failed to initialize FreeRTOS resources");
    return;
  }

  bool currentStatus = false;

  if (millis() > 5000) {
    currentStatus = tud_mounted();
  }

  if (currentStatus != usbConnected) {
    DEBUG_PRINTF("USB status change detected: %s -> %s\n",
      usbConnected ? "Connected" : "Disconnected",
      currentStatus ? "Connected" : "Disconnected");
    usbConnected = currentStatus;
  }

  processHIDCommands();
}

void USBManager::executeHIDCommand(const HIDMessage& command) {
  if (!isUSBHIDEnabled()) {
    DEBUG_PRINTLN("WARNING: HID command rejected - USB HID functionality disabled");
    return;
  }

  if (!initialized) {
    DEBUG_PRINTLN("WARNING: HID command rejected - USBManager not initialized");
    return;
  }

  if (!usbConnected) {
    DEBUG_PRINTLN("WARNING: HID command rejected USB not connected");
    return;
  }

  if (systemManager) {
    systemManager->notifyActivity();
  }

  DEBUG_PRINTF("Executing HID command: %d, key: %d, x: %d, y: %d, buttons: %d\n",
    command.command, command.key, command.x, command.y, command.buttons);

  if (!xSemaphoreTake(hidMutex, pdMS_TO_TICKS(100))) {
    DEBUG_PRINTLN("Failed to acquire HID mutex");
    return;
  }

  switch (command.command) {
  case HID_KEYBOARD_PRESS:
    DEBUG_PRINTF("Keyboard: Processing key press for key code %d\n", command.key);

    if (command.key == 0) {
      DEBUG_PRINTF("ERROR: Invalid key code: %d (key is 0!)\n", command.key);
      break;
    }
    if (command.key > 255) {
      DEBUG_PRINTF("ERROR: Invalid key code: %d (key > 255)\n", command.key);
      break;
    }

    DEBUG_PRINTF("Keyboard: Pressing key code %d\n", command.key);
    keyboard.press(command.key);
    DEBUG_PRINTF("Key %d pressed\n", command.key);

    delay(USB_HID_KEYBOARD_PRESS_DELAY);
    keyboard.release(command.key);
    DEBUG_PRINTF("Key %d released\n", command.key);

    keyboard.releaseAll();
    DEBUG_PRINTLN("Keyboard HID report sent");
    break;

  case HID_KEYBOARD_HOLD:
    DEBUG_PRINTF("Keyboard: Holding key code %d\n", command.key);
    if (command.key == 0 || command.key > 255) {
      DEBUG_PRINTF("Invalid key code: %d\n", command.key);
      break;
    }
    keyboard.press(command.key);
    DEBUG_PRINTF("Holding key %d \n", command.key);
    break;

  case HID_KEYBOARD_RELEASE:
    DEBUG_PRINTF("Keyboard: Releasing key code %d\n", command.key);
    keyboard.release(command.key);
    break;

  case HID_KEYBOARD_TYPE:
    DEBUG_PRINTF("Keyboard: Typing text: %s\n", command.text);
    keyboard.print(command.text);
    break;

  case HID_MOUSE_MOVE:
    DEBUG_PRINTF("Mouse: Moving by (%d, %d)\n", command.x, command.y);
    mouse.move(command.x, command.y);
    break;

  case HID_MOUSE_PRESS:
    DEBUG_PRINTF("Mouse: Pressing buttons %d\n", command.buttons);
    if (command.buttons & 0x01) {
      mouse.press(MOUSE_LEFT);
      delay(USB_HID_MOUSE_PRESS_DELAY);
      mouse.release(MOUSE_LEFT);
    }
    if (command.buttons & 0x02) {
      mouse.press(MOUSE_RIGHT);
      delay(USB_HID_MOUSE_PRESS_DELAY);
      mouse.release(MOUSE_RIGHT);
    }
    if (command.buttons & 0x04) {
      mouse.press(MOUSE_MIDDLE);
      delay(USB_HID_MOUSE_PRESS_DELAY);
      mouse.release(MOUSE_MIDDLE);
    }
    break;

  case HID_MOUSE_HOLD:
    DEBUG_PRINTF("Mouse: Holding buttons %d\n", command.buttons);
    if (command.buttons & 0x01) mouse.press(MOUSE_LEFT);
    if (command.buttons & 0x02) mouse.press(MOUSE_RIGHT);
    if (command.buttons & 0x04) mouse.press(MOUSE_MIDDLE);
    break;

  case HID_MOUSE_RELEASE:
    DEBUG_PRINTF("Mouse: Releasing buttons %d\n", command.buttons);
    if (command.buttons & 0x01) mouse.release(MOUSE_LEFT);
    if (command.buttons & 0x02) mouse.release(MOUSE_RIGHT);
    if (command.buttons & 0x04) mouse.release(MOUSE_MIDDLE);
    break;

  case HID_MOUSE_SCROLL:
  {
    DEBUG_PRINTF("Mouse: Scrolling by x=%d, y=%d\n", command.x, command.y);
    int8_t horizontal = (command.x > 127) ? 127 : (command.x < -128) ? -128 : (int8_t)command.x;
    int8_t vertical = (command.y > 127) ? 127 : (command.y < -128) ? -128 : (int8_t)command.y;
    mouse.move(0, 0, vertical, horizontal);
    DEBUG_PRINTF("Mouse: Scroll executed - vertical: %d, horizontal: %d\n", vertical, horizontal);
    break;
  }

  case HID_GAMEPAD_PRESS:
    DEBUG_PRINTF("Gamepad: Pressing button %d\n", command.key);
    if (command.key == 0 || command.key > 16) {
      DEBUG_PRINTF("Invalid gamepad button: %d\n", command.key);
      break;
    }
    gamepad.pressButton(command.key);
    delay(USB_HID_GAMEPAD_PRESS_DELAY);
    gamepad.releaseButton(command.key);
    break;

  case HID_GAMEPAD_HOLD:
    DEBUG_PRINTF("Gamepad: Holding button %d\n", command.key);
    if (command.key == 0 || command.key > 16) {
      DEBUG_PRINTF("Invalid gamepad button: %d\n", command.key);
      break;
    }
    gamepad.pressButton(command.key);
    break;

  case HID_GAMEPAD_RELEASE:
    DEBUG_PRINTF("Gamepad: Releasing button %d\n", command.key);
    if (command.key == 0 || command.key > 16) {
      DEBUG_PRINTF("Invalid gamepad button: %d\n", command.key);
      break;
    }
    gamepad.releaseButton(command.key);
    break;

  case HID_GAMEPAD_BUTTON:
    DEBUG_PRINTF("Gamepad: Button %d, pressed: %s\n", command.key, (command.buttons & 0x80) ? "true" : "false");
    if (command.key == 0 || command.key > 16) {
      DEBUG_PRINTF("Invalid gamepad button: %d\n", command.key);
      break;
    }
    if (command.buttons & 0x80) {
      gamepad.pressButton(command.key);
    }
    else {
      gamepad.releaseButton(command.key);
    }
    break;

  case HID_GAMEPAD_AXIS_RIGHT:
    DEBUG_PRINTF("Gamepad: Right axis movement (%d, %d)\n", command.x, command.y);
    gamepad.rightStick(command.x, command.y);
    break;

  case HID_GAMEPAD_AXIS_LEFT:
    DEBUG_PRINTF("Gamepad: Left axis movement (%d, %d)\n", command.x, command.y);
    gamepad.leftStick(command.x, command.y);
    break;

  case HID_SYSTEM_POWER:
    DEBUG_PRINTLN("System: Sending HID Consumer Control Power key");
    consumerControl.press(CONSUMER_CONTROL_POWER);
    delay(200);
    consumerControl.release();
    break;

  default:
    DEBUG_PRINTF("Unknown HID command: %d\n", command.command);
    break;
  }

  xSemaphoreGive(hidMutex);
  DEBUG_PRINTLN("HID command execution complete");
}

void USBManager::processHIDCommands() {
  if (!isUSBHIDEnabled()) {
    return;
  }

  if (!initialized || !hidQueue) {
    return;
  }

  HIDMessage message;

  while (xQueueReceive(hidQueue, &message, 0) == pdTRUE) {
    DEBUG_PRINTF("=== Processing HID command from queue ===\n");
    DEBUG_PRINTF("Command: %d, Key: %d, X: %d, Y: %d, Buttons: %d, Text: '%s'\n",
      message.command, message.key, message.x, message.y, message.buttons, message.text);
    executeHIDCommand(message);
  }
}

bool USBManager::sendKeyPress(uint8_t key) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidKey(key) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_KEYBOARD_PRESS;
  message.key = key;
  message.x = 0;
  message.y = 0;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendKeyHold(uint8_t key) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidKey(key) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_KEYBOARD_HOLD;
  message.key = key;
  message.x = 0;
  message.y = 0;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendKeyRelease(uint8_t key) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidKey(key) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_KEYBOARD_RELEASE;
  message.key = key;
  message.x = 0;
  message.y = 0;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::typeText(const char* text) {
  if (!isUSBHIDEnabled()) return true;

  if (!text || strlen(text) == 0 || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_KEYBOARD_TYPE;
  message.key = 0;
  message.x = 0;
  message.y = 0;
  message.buttons = 0;
  message.timestamp = millis();

  strncpy(message.text, text, sizeof(message.text) - 1);
  message.text[sizeof(message.text) - 1] = '\0';

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseMove(int16_t x, int16_t y) {
  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_MOUSE_MOVE;
  message.key = 0;
  message.x = x;
  message.y = y;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMousePress(uint8_t button) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidMouseButton(button) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_MOUSE_PRESS;
  message.key = 0;
  message.x = 0;
  message.y = 0;
  message.buttons = button;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseHold(uint8_t button) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidMouseButton(button) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_MOUSE_HOLD;
  message.key = 0;
  message.x = 0;
  message.y = 0;
  message.buttons = button;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseRelease(uint8_t button) {
  if (!isUSBHIDEnabled()) return true;

  if (!isValidMouseButton(button) || !initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_MOUSE_RELEASE;
  message.key = 0;
  message.x = 0;
  message.y = 0;
  message.buttons = button;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseScroll(int16_t x, int16_t y) {
  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_MOUSE_SCROLL;
  message.key = 0;
  message.x = x;
  message.y = y;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendGamepadButton(uint8_t button, bool pressed) {
  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_GAMEPAD_BUTTON;
  message.key = button;
  message.x = 0;
  message.y = 0;
  message.buttons = static_cast<uint8_t>(pressed ? 0x80 : 0x00);
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendGamepadRightAxis(int16_t x, int16_t y) {
  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_GAMEPAD_AXIS_RIGHT;
  message.key = 0;
  message.x = x;
  message.y = y;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendGamepadLeftAxis(int16_t x, int16_t y) {
  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_GAMEPAD_AXIS_LEFT;
  message.key = 0;
  message.x = x;
  message.y = y;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendSystemPowerKey() {
  DEBUG_PRINTLN("Sending system power key");

  if (!isUSBHIDEnabled()) return true;

  if (!initialized || !hidQueue) return false;

  HIDMessage message = {};
  message.command = HID_SYSTEM_POWER;
  message.key = 0;
  message.x = 0;
  message.y = 0;
  message.buttons = 0;
  message.text[0] = '\0';
  message.timestamp = millis();

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

void USBManager::handleVendorReport(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  if (!isUSBHIDEnabled() || report_id != VENDOR_REPORT_ID || len != sizeof(VendorPacket)) {
    DEBUG_PRINTF("Invalid vendor report: ID=%d, len=%d\n", report_id, len);
    return;
  }

  const VendorPacket* request = reinterpret_cast<const VendorPacket*>(buffer);

  if (request->magic != PROTOCOL_MAGIC || request->protocol_version != PROTOCOL_VERSION) {
    DEBUG_PRINTF("Invalid protocol magic/version: magic=0x%04X, version=%d\n",
      request->magic, request->protocol_version);
    return;
  }

  DEBUG_PRINTF("Vendor command received: cmd=0x%02X, seq=%u\n", request->command, request->sequence);

  switch (static_cast<VendorCommand>(request->command)) {
  case CMD_PING:
    handlePingCommand(*request);
    break;
  case CMD_GET_STATUS:
    handleGetStatusCommand(*request);
    break;
  case CMD_GET_INFO:
    handleGetInfoCommand(*request);
    break;
  default:
    DEBUG_PRINTF("Unknown vendor command: 0x%02X\n", request->command);
    break;
  }
}

void USBManager::sendVendorResponse(const VendorPacket& request, VendorResponse response_type,
  const void* payload, size_t payload_size) {
  vendorResponse = {};
  vendorResponse.magic = PROTOCOL_MAGIC;
  vendorResponse.protocol_version = PROTOCOL_VERSION;
  vendorResponse.command = static_cast<uint8_t>(response_type);
  vendorResponse.sequence = request.sequence;

  if (payload && payload_size > 0) {
    size_t copy_size = (payload_size > sizeof(vendorResponse.payload)) ? sizeof(vendorResponse.payload) : payload_size;
    memcpy(vendorResponse.payload, payload, copy_size);
  }

  vendorResponseReady = true;
  DEBUG_PRINTF("Vendor response prepared: resp=0x%02X, seq=%u\n", response_type, vendorResponse.sequence);
}

void USBManager::handlePingCommand(const VendorPacket& request) {
  sendVendorResponse(request, RESP_PONG, nullptr, 0);
}

void USBManager::handleGetStatusCommand(const VendorPacket& request) {
  StatusPayload payload = buildStatusPayload();
  sendVendorResponse(request, RESP_STATUS, &payload, sizeof(payload));
}

void USBManager::handleGetInfoCommand(const VendorPacket& request) {
  InfoPayload payload = buildInfoPayload();
  sendVendorResponse(request, RESP_INFO, &payload, sizeof(payload));
}

StatusPayload USBManager::buildStatusPayload() {
  StatusPayload payload = {};
  PowerData powerData = powerManager->getPowerData();

  payload.battery_voltage_mv = static_cast<uint16_t>(powerData.battery.voltage * 1000);
  payload.battery_current_ma = static_cast<int16_t>(powerData.battery.current * 1000);
  payload.charger_voltage_mv = static_cast<uint16_t>(powerData.charger.voltage * 1000);
  payload.charger_current_ma = static_cast<int16_t>(powerData.charger.current * 1000);
  payload.charger_power_mw = static_cast<uint16_t>(powerData.charger.power * 1000);
  payload.charger_connected = powerData.charger.connected;
  payload.battery_percentage = powerData.battery.percentage;
  payload.uptime_seconds = static_cast<uint32_t>(millis() / 1000);
  payload.to_fully_discharge_s = powerData.battery.toFullyDischargeMs / 1000;
  payload.to_fully_charge_s = powerData.charger.toFullyChargeMs / 1000;

  return payload;
}

InfoPayload USBManager::buildInfoPayload() {
  InfoPayload payload = {};

  payload.firmware_version = FIRMWARE_VERSION;

  strncpy(payload.serial_number, USB_SERIAL_NUMBER, sizeof(payload.serial_number) - 1);
  payload.serial_number[sizeof(payload.serial_number) - 1] = '\0';

  return payload;
}

bool USBManager::isValidKey(uint8_t key) {
  if (!isUSBHIDEnabled()) return true;

  if (key >= 32 && key <= 126) {
    return true;
  }

  switch (key) {
  case 0x08: // Backspace
  case 0x09: // Tab
  case 0x0A: // Enter (Line Feed)
  case 0x0D: // Enter (Carriage Return)
  case 0x1B: // Escape
  case 0x20: // Space
  case 0x7F: // Delete
    return true;

  case 194: // F1 (0xC2)
  case 195: // F2 (0xC3)
  case 196: // F3 (0xC4)
  case 197: // F4 (0xC5)
  case 198: // F5
  case 199: // F6
  case 200: // F7
  case 201: // F8
  case 202: // F9
  case 203: // F10
  case 204: // F11
  case 205: // F12
    return true;

    // Arrow keys
  case 215: // Left Arrow
  case 216: // Up Arrow
  case 217: // Right Arrow
  case 218: // Down Arrow
    return true;

  default:
    break;
  }

  DEBUG_PRINTF("Key validation: Key code %d may not be valid\n", key);
  return true;
}

bool USBManager::isValidMouseButton(uint8_t button) {
  if (!isUSBHIDEnabled()) return true;
  return button > 0 && button <= 7;
}

void USBManager::checkInitialUSBStatus() {
  if (!isUSBHIDEnabled()) {
    DEBUG_PRINTLN("USBManager: Initial USB status check skipped (USB disabled)");
    usbConnected = false;
    return;
  }

  DEBUG_PRINTLN("Checking initial USB connection status...");

  delay(1000);
  usbConnected = false;
  if (millis() > 5000) {
    usbConnected = tud_mounted();
  }

  DEBUG_PRINTF("Initial USB connection status: %s\n", usbConnected ? "Connected" : "Disconnected");
}

bool USBManager::getVendorResponse(VendorPacket* response) {
  if (!response) {
    return false;
  }

  if (vendorResponseReady) {
    *response = vendorResponse;
    vendorResponseReady = false;
    DEBUG_PRINTF("Vendor response retrieved: resp=0x%02X, seq=%u\n", response->command, response->sequence);
    return true;
  }

  *response = {};
  response->magic = PROTOCOL_MAGIC;
  response->protocol_version = PROTOCOL_VERSION;
  response->command = static_cast<uint8_t>(RESP_ERROR);
  response->sequence = 0;

  DEBUG_PRINTLN("ERROR: No vendor response ready, returning error response");
  return true;
}