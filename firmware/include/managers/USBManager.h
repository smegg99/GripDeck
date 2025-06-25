// include/managers/USBManager.h
#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <cstdint>
#include "config/Config.h"

#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDGamepad.h>
#include <USBHIDConsumerControl.h>
#include <USBHID.h>
#include <USB.h>

#include <classes/GripDeckVendorHID.h>

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

struct __attribute__((packed)) StatusPayload {
  uint16_t battery_voltage_mv;
  int16_t battery_current_ma;
  uint32_t to_fully_discharge_s;
  uint16_t charger_voltage_mv;
  int16_t charger_current_ma;
  uint32_t to_fully_charge_s;
  uint8_t battery_percentage;
  uint32_t uptime_seconds;
};

struct __attribute__((packed)) InfoPayload {
  uint16_t firmware_version;
  char serial_number[12];
  uint8_t reserved[8];
};

class USBManager {
private:
  USBHIDKeyboard keyboard;
  USBHIDMouse mouse;
  USBHIDGamepad gamepad;
  USBHIDConsumerControl consumerControl;
  USBHID hid;
  GripDeckVendorHID* vendorDevice;

  QueueHandle_t hidQueue;
  SemaphoreHandle_t hidMutex;

  bool usbConnected = false;
  bool initialized = false;
  uint32_t sequenceCounter = 0;

  // Vendor protocol response storage
  VendorPacket vendorResponse;
  bool vendorResponseReady = false;

  static USBManager* instance;

private:
  void processHIDCommands();
  void executeHIDCommand(const HIDMessage& command);
  void checkInitialUSBStatus();
  void handleUSBEvent(arduino_usb_event_t event, void* event_data);
  bool initializeFreeRTOSResources();

  bool isValidKey(uint8_t key);
  bool isValidMouseButton(uint8_t button);

  void sendVendorResponse(const VendorPacket& request, VendorResponse response_type, const void* payload, size_t payload_size);
  void handlePingCommand(const VendorPacket& request);
  void handleGetStatusCommand(const VendorPacket& request);
  void handleGetInfoCommand(const VendorPacket& request);
  StatusPayload buildStatusPayload();
  InfoPayload buildInfoPayload();

  inline bool isUSBHIDEnabled() const { return !DISABLE_USB_HID; }

public:
  void handleVendorReport(uint8_t report_id, const uint8_t* buffer, uint16_t len);
  bool getVendorResponse(VendorPacket* response);
  static USBManager* getInstance() { return instance; }

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

  bool isUSBConnected() const { return usbConnected; }
};

#endif // USB_MANAGER_H