// include/managers/StatusManager.h
#ifndef STATUS_MANAGER_H
#define STATUS_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <config/Config.h>
#include <utils/DebugSerial.h>

// Forward declarations
class PowerManager;
class BLEManager;
class USBManager;
class SystemManager;

enum DeviceStatus {
  STATUS_IDLE,             // It's all good - steady LED at max brightness
  STATUS_BLE_CONNECTED,    // BLE client connected - fast blink then steady
  STATUS_BLE_DISCONNECTED, // BLE client disconnected - fast blink then steady
  STATUS_POWER_ON,         // Powering on the SBC - slow blink
  STATUS_POWER_OFF,        // Powering off the SBC - slow blink
  STATUS_LOW_POWER_MODE,   // Low power mode activated - steady LED at reduced brightness
  STATUS_BLE_CMD_ERROR,    // BLE error occurred - fast blink pattern
  STATUS_HID_CONNECTED,    // HID device connected - fast blink then steady
  STATUS_HID_DISCONNECTED, // HID device disconnected - fast blink then steady
  STATUS_SHUTDOWN          // System shutdown - fade out
};

struct StatusMessage {
  DeviceStatus status;
  uint32_t timestamp;
  uint32_t duration;  // How long to display this status (0 = permanent)
};

enum LEDPattern {
  LED_PATTERN_OFF,          // LEDs off
  LED_PATTERN_STEADY,       // Steady brightness
  LED_PATTERN_BLINK_FAST,   // Fast blinking
  LED_PATTERN_BLINK_SLOW,   // Slow blinking
  LED_PATTERN_FADE_OUT      // Fade out to off
};

class StatusManager {
private:
  QueueHandle_t statusQueue;
  SemaphoreHandle_t statusMutex;

  DeviceStatus currentStatus;
  LEDPattern currentPattern;
  uint8_t currentBrightness;
  uint32_t patternStartTime;
  uint32_t lastBlinkTime;
  bool blinkState;
  bool isLowPowerMode;

  bool prevBLEConnected;
  bool prevUSBConnected;
  bool prevLowPowerMode;

  void setLEDPattern(LEDPattern pattern, uint8_t brightness = LED_BRIGHTNESS_MAX);
  void updateLEDPattern();
  void processStatusQueue();
  void checkConnectionStates();
  void handleStatusChange(DeviceStatus newStatus, uint32_t duration = 0);

  void updateSteadyPattern();
  void updateBlinkPattern(uint32_t blinkInterval);
  void updateFadeOutPattern();

  uint8_t getLEDBrightness() const;
  bool isTemporaryStatus(DeviceStatus status) const;

public:
  StatusManager();
  ~StatusManager();

  bool begin();
  void update();

  void setStatus(DeviceStatus status, uint32_t duration = 0);
  void setLowPowerMode(bool enabled);

  DeviceStatus getCurrentStatus() const { return currentStatus; }
  bool isInLowPowerMode() const { return isLowPowerMode; }
};

#endif // STATUS_MANAGER_H