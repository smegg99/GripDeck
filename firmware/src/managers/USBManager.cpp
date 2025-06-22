// src/managers/USBManager.cpp

#include "managers/USBManager.h"
#include <utils/DebugSerial.h>
#include <USB.h>
#include "esp32-hal-tinyusb.h"
#include "esp_event.h"
#include "USBCDC.h"

USBManager* USBManager::instance = nullptr;
USBManager::USBManager() {
  instance = this;
}

void USBManager::staticEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (instance) {
    instance->handleEvent(event_base, event_id, event_data);
  }
}

void USBManager::handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data) {
  switch (event_id) {
  case ARDUINO_HW_CDC_CONNECTED_EVENT:
    DEBUG_PRINTLN("USB device connected");
    usbConnected = true;
    break;
  case ARDUINO_HW_CDC_BUS_RESET_EVENT:
    DEBUG_PRINTLN("USB device disconnected");
    usbConnected = false;
    break;
  default:
    DEBUG_VERBOSE_PRINTF("Unknown USB event: %d\n", event_id);
  }
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

  // USB.onEvent(staticEventHandler);
  esp_event_handler_register(ARDUINO_HW_CDC_EVENTS, ESP_EVENT_ANY_ID, staticEventHandler, NULL);

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

  usbConnected = false;

  DEBUG_PRINTLN("USBManager initialized successfully");
  return true;
}

void USBManager::update() {
  if (!usbConnected) {
    DEBUG_PRINTLN("USB not connected, skipping update");
    return;
  }

  if (millis() - lastUpdateTime < TASK_INTERVAL_USB) {
    return;
  }

  processHIDCommands();

  lastUpdateTime = millis();
}

void USBManager::executeHIDCommand(const HIDMessage& command) {
  if (!usbConnected) {
    DEBUG_PRINTLN("WARNING: HID command rejected USB not connected");
    return;
  }

  DEBUG_PRINTF("Executing HID command: %d, key: %d\n", command.command, command.key);

  if (!xSemaphoreTake(hidMutex, pdMS_TO_TICKS(100))) {
    DEBUG_PRINTLN("Failed to acquire HID mutex");
    return;
  }

  switch (command.command) {
  case HID_KEYBOARD_PRESS:
    DEBUG_PRINTF("Keyboard: Pressing key code %d\n", command.key);

    if (command.key == 0 || command.key > 255) {
      DEBUG_PRINTF("Invalid key code: %d\n", command.key);
      break;
    }
    keyboard.press(command.key);
    DEBUG_PRINTF("Key %d pressed\n", command.key);

    delay(USB_HID_KEYBOARD_PRESS_DELAY);
    keyboard.release(command.key);
    DEBUG_PRINTF("Key %d released\n", command.key);

    keyboard.releaseAll();
    DEBUG_PRINTLN("Keyboard HID report sent");
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

  case HID_MOUSE_CLICK:
    DEBUG_PRINTF("Mouse: Clicking buttons %d\n", command.buttons);
    if (command.buttons & 0x01) mouse.click(MOUSE_LEFT);
    if (command.buttons & 0x02) mouse.click(MOUSE_RIGHT);
    if (command.buttons & 0x04) mouse.click(MOUSE_MIDDLE);
    break;

  case HID_MOUSE_SCROLL:
    DEBUG_PRINTF("Mouse: Scrolling by x=%d, y=%d\n", command.x, command.y);
    mouse.move(0, 0, command.x, command.y);
    break;

  case HID_GAMEPAD_BUTTON:
    DEBUG_PRINTF("Gamepad: Button %d, pressed: %s\n", command.key, (command.buttons & 0x80) ? "true" : "false");
    // TODO: Implement this
    break;

  case HID_GAMEPAD_AXIS:
    DEBUG_PRINTF("Gamepad: Axis movement (%d, %d)\n", command.x, command.y);
    // TODO: Implement this
    break;

  case HID_SYSTEM_POWER:
    DEBUG_PRINTLN("System: Sending HID System Power Down key (0x81)");
    keyboard.press(0x81);
    delay(200);
    keyboard.release(0x81);
    keyboard.releaseAll();
    DEBUG_PRINTLN("HID System Power Down key sent");
    break;

  default:
    DEBUG_PRINTF("Unknown HID command: %d\n", command.command);
    break;
  }

  xSemaphoreGive(hidMutex);
  DEBUG_PRINTLN("HID command execution complete");
}

void USBManager::processHIDCommands() {
  HIDMessage message;

  while (xQueueReceive(hidQueue, &message, 0) == pdTRUE) {
    executeHIDCommand(message);
  }
}

bool USBManager::sendKeyPress(uint8_t key) {
  if (!isValidKey(key)) return false;

  HIDMessage message = {
      .command = HID_KEYBOARD_PRESS,
      .key = key,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendKeyRelease(uint8_t key) {
  if (!isValidKey(key)) return false;

  HIDMessage message = {
      .command = HID_KEYBOARD_RELEASE,
      .key = key,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendKeyboard(uint8_t modifier, uint8_t key) {
  HIDMessage message = {
      .command = HID_KEYBOARD_PRESS,
      .key = key,
      .buttons = modifier,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::typeText(const char* text) {
  if (!text || strlen(text) == 0) return false;

  HIDMessage message = {
      .command = HID_KEYBOARD_TYPE,
      .timestamp = millis()
  };

  strncpy(message.text, text, sizeof(message.text) - 1);
  message.text[sizeof(message.text) - 1] = '\0';

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseMove(int16_t x, int16_t y) {
  HIDMessage message = {
      .command = HID_MOUSE_MOVE,
      .x = x,
      .y = y,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseClick(uint8_t button) {
  if (!isValidMouseButton(button)) return false;

  HIDMessage message = {
      .command = HID_MOUSE_CLICK,
      .buttons = button,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendMouseScroll(int8_t scroll) {
  HIDMessage message = {
      .command = HID_MOUSE_SCROLL,
      .x = scroll,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendGamepadButton(uint8_t button, bool pressed) {
  HIDMessage message = {
      .command = HID_GAMEPAD_BUTTON,
      .key = button,
      .buttons = static_cast<uint8_t>(pressed ? 0x80 : 0x00),
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::sendSystemPowerKey() {
  DEBUG_PRINTLN("Sending system power key");

  HIDMessage message = {
      .command = HID_SYSTEM_POWER,
      .key = 0,
      .timestamp = millis()
  };

  return xQueueSend(hidQueue, &message, 0) == pdTRUE;
}

bool USBManager::isValidKey(uint8_t key) {
  // ASCII printable characters (32-126)
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
  return button > 0 && button <= 7;
}