// include/managers/USBManager.h
#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDGamepad.h>

enum HIDCommand {
  HID_KEYBOARD_PRESS,
  HID_KEYBOARD_RELEASE,
  HID_KEYBOARD_TYPE,
  HID_MOUSE_MOVE,
  HID_MOUSE_CLICK,
  HID_MOUSE_SCROLL,
  HID_GAMEPAD_BUTTON,
  HID_GAMEPAD_AXIS,
  HID_SYSTEM_POWER,
  HID_STATUS_REPORT
};

struct HIDMessage {
  HIDCommand command;
  uint8_t key;
  int16_t x, y;
  uint8_t buttons;
  char text[64];
  uint32_t timestamp;
};

struct SystemStatus {
  float batteryVoltage;
  float batteryCurrent;
  float chargerVoltage;
  float chargerCurrent;
  uint8_t batteryPercentage;
};

class USBManager {
private:
  USBHIDKeyboard keyboard;
  USBHIDMouse mouse;
  USBHIDGamepad gamepad;

  bool usbConnected = false;

  QueueHandle_t hidQueue;
  SemaphoreHandle_t hidMutex;

  uint32_t lastUpdateTime = 0;

  static USBManager* instance;

  static void staticEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
  void handleEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);
  void processHIDCommands();
  void executeHIDCommand(const HIDMessage& command);

  bool isValidKey(uint8_t key);
  bool isValidMouseButton(uint8_t button);
public:
  USBManager();
  ~USBManager();

  bool begin();
  void update();

  bool sendKeyPress(uint8_t key);
  bool sendKeyRelease(uint8_t key);
  bool sendKeyboard(uint8_t modifier, uint8_t key);
  bool typeText(const char* text);

  bool sendMouseMove(int16_t x, int16_t y);
  bool sendMouseClick(uint8_t button);
  bool sendMouseScroll(int8_t scroll);

  bool sendGamepadButton(uint8_t button, bool pressed);
  bool sendGamepadAxis(uint8_t axis, int16_t value);

  bool sendSystemPowerKey();
  bool sendStatusReport(const SystemStatus& status);

  bool isUSBConnected() const { return usbConnected; }
};

#endif // USB_MANAGER_H