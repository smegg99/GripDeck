// include/managers/USBManager.h
#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDGamepad.h>
#include <USBHIDConsumerControl.h>
#include <USB.h>

enum HIDCommand {
  HID_KEYBOARD_PRESS,
  HID_KEYBOARD_HOLD,
  HID_KEYBOARD_RELEASE,
  HID_KEYBOARD_TYPE,
  HID_MOUSE_MOVE,
  HID_MOUSE_PRESS,
  HID_MOUSE_HOLD,
  HID_MOUSE_RELEASE,
  HID_MOUSE_SCROLL,
  HID_GAMEPAD_PRESS,
  HID_GAMEPAD_HOLD,
  HID_GAMEPAD_RELEASE,
  HID_GAMEPAD_BUTTON,
  HID_GAMEPAD_AXIS_RIGHT,
  HID_GAMEPAD_AXIS_LEFT,
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
  USBHIDConsumerControl consumerControl;

  bool usbConnected = false;

  QueueHandle_t hidQueue;
  SemaphoreHandle_t hidMutex;

  static USBManager* instance;

  static void staticEventHandler(arduino_usb_event_t event, void* event_data);
  void handleUSBEvent(arduino_usb_event_t event, void* event_data);
  void processHIDCommands();
  void executeHIDCommand(const HIDMessage& command);
  void checkInitialUSBStatus();

  bool isValidKey(uint8_t key);
  bool isValidMouseButton(uint8_t button);
public:
  USBManager();
  ~USBManager();

  bool begin();
  void update();

  bool sendKeyPress(uint8_t key);
  bool sendKeyHold(uint8_t key);
  bool sendKeyRelease(uint8_t key);
  bool typeText(const char* text);

  bool sendMouseMove(int16_t x, int16_t y);
  bool sendMousePress(uint8_t button);
  bool sendMouseHold(uint8_t button);
  bool sendMouseRelease(uint8_t button);
  bool sendMouseScroll(int16_t x, int16_t y);

  bool sendGamepadButton(uint8_t button, bool pressed);
  bool sendGamepadRightAxis(int16_t x, int16_t y);
  bool sendGamepadLeftAxis(int16_t x, int16_t y);

  bool sendSystemPowerKey();

  // This uses a custom protocol to send system status reports
  // TODO: Implement this
  bool sendStatusReport(const SystemStatus& status);

  bool isUSBConnected() const { return usbConnected; }
};

#endif // USB_MANAGER_H