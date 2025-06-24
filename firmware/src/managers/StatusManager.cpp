// src/managers/StatusManager.cpp
#include "managers/StatusManager.h"
#include "managers/PowerManager.h"
#include "managers/BLEManager.h" 
#include "managers/USBManager.h"
#include "managers/SystemManager.h"

extern PowerManager* powerManager;
extern BLEManager* bleManager;
extern USBManager* usbManager;
extern SystemManager* systemManager;

StatusManager::StatusManager() :
  statusQueue(nullptr),
  statusMutex(nullptr),
  currentStatus(STATUS_IDLE),
  currentPattern(LED_PATTERN_OFF),
  currentBrightness(0),
  patternStartTime(0),
  lastBlinkTime(0),
  blinkState(false),
  isLowPowerMode(false),
  prevBLEConnected(false),
  prevUSBConnected(false),
  prevLowPowerMode(false),
  prevCharging(false) {
}

StatusManager::~StatusManager() {
  if (statusQueue) {
    vQueueDelete(statusQueue);
  }
  if (statusMutex) {
    vSemaphoreDelete(statusMutex);
  }
}

bool StatusManager::begin() {
  DEBUG_PRINTLN("StatusManager: Initializing...");

  // Create status queue
  statusQueue = xQueueCreate(10, sizeof(StatusMessage));
  if (!statusQueue) {
    DEBUG_PRINTLN("ERROR: StatusManager - Failed to create status queue");
    return false;
  }

  // Create mutex for thread safety
  statusMutex = xSemaphoreCreateMutex();
  if (!statusMutex) {
    DEBUG_PRINTLN("ERROR: StatusManager - Failed to create status mutex");
    return false;
  }

  // Initialize LED pattern to idle state
  setLEDPattern(LED_PATTERN_STEADY, getLEDBrightness());
  currentStatus = STATUS_IDLE;

  DEBUG_PRINTLN("StatusManager: Initialization complete");
  return true;
}

void StatusManager::update() {
  if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Process any queued status messages
    processStatusQueue();

    // Check for connection state changes
    checkConnectionStates();

    // Update current LED pattern
    updateLEDPattern();

    xSemaphoreGive(statusMutex);
  }
}

void StatusManager::setStatus(DeviceStatus status, uint32_t duration) {
  StatusMessage msg;
  msg.status = status;
  msg.timestamp = millis();
  msg.duration = duration;

  if (xQueueSend(statusQueue, &msg, 0) != pdTRUE) {
    DEBUG_PRINTF("WARNING: StatusManager - Failed to queue status %d\n", status);
  }
}

void StatusManager::setLowPowerMode(bool enabled) {
  if (xSemaphoreTake(statusMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (isLowPowerMode != enabled) {
      isLowPowerMode = enabled;

      if (enabled) {
        DEBUG_PRINTLN("StatusManager: Entering low power mode");
        setStatus(STATUS_LOW_POWER_MODE);
      }
      else {
        DEBUG_PRINTLN("StatusManager: Exiting low power mode");
        setStatus(STATUS_IDLE);
      }
    }
    xSemaphoreGive(statusMutex);
  }
}

void StatusManager::processStatusQueue() {
  StatusMessage msg;
  while (xQueueReceive(statusQueue, &msg, 0) == pdTRUE) {
    handleStatusChange(msg.status, msg.duration);
  }
}

void StatusManager::checkConnectionStates() {
  bool currentBLE = bleManager->isConnected();
  bool currentUSB = usbManager->isUSBConnected();
  bool currentLowPower = isLowPowerMode;

  PowerData powerData = powerManager->getPowerData();
  bool currentCharging = powerData.charger.connected;

  if (currentBLE != prevBLEConnected) {
    if (currentBLE) {
      DEBUG_PRINTLN("StatusManager: BLE connected");
      setStatus(STATUS_BLE_CONNECTED, LED_BLINK_DURATION);
    }
    else {
      DEBUG_PRINTLN("StatusManager: BLE disconnected");
      setStatus(STATUS_BLE_DISCONNECTED, LED_BLINK_DURATION);
    }
    prevBLEConnected = currentBLE;
  }

  // Check USB/HID connection state changes
  if (currentUSB != prevUSBConnected) {
    if (currentUSB) {
      DEBUG_PRINTLN("StatusManager: HID connected");
      setStatus(STATUS_HID_CONNECTED, LED_BLINK_DURATION);
    }
    else {
      DEBUG_PRINTLN("StatusManager: HID disconnected");
      setStatus(STATUS_HID_DISCONNECTED, LED_BLINK_DURATION);
    }
    prevUSBConnected = currentUSB;
  }

  if (currentCharging != prevCharging) {
    if (currentCharging) {
      DEBUG_PRINTLN("StatusManager: Battery charging started");
      setStatus(STATUS_CHARGING, 0);
    }
    else {
      DEBUG_PRINTLN("StatusManager: Battery charging stopped");
      setStatus(STATUS_IDLE, 0);
    }
    prevCharging = currentCharging;
  }

  prevLowPowerMode = currentLowPower;
}

void StatusManager::handleStatusChange(DeviceStatus newStatus, uint32_t duration) {
  DEBUG_PRINTF("StatusManager: Status change to %d (duration: %lu ms)\n", newStatus, duration);

  currentStatus = newStatus;
  patternStartTime = millis();

  // Set appropriate LED pattern based on status
  switch (newStatus) {
  case STATUS_IDLE:
    setLEDPattern(LED_PATTERN_STEADY, getLEDBrightness());
    break;

  case STATUS_BLE_CONNECTED:
  case STATUS_BLE_DISCONNECTED:
  case STATUS_HID_CONNECTED:
  case STATUS_HID_DISCONNECTED:
    setLEDPattern(LED_PATTERN_BLINK_FAST, getLEDBrightness());
    break;

  case STATUS_POWER_ON:
  case STATUS_POWER_OFF:
    setLEDPattern(LED_PATTERN_BLINK_SLOW, getLEDBrightness());
    break;

  case STATUS_LOW_POWER_MODE:
    setLEDPattern(LED_PATTERN_STEADY, LED_BRIGHTNESS_POWER_SAVE);
    break;

  case STATUS_CHARGING:
    setLEDPattern(LED_PATTERN_PULSE, getLEDBrightness());
    break;

  case STATUS_BLE_CMD_ERROR:
    setLEDPattern(LED_PATTERN_BLINK_FAST, getLEDBrightness());
    break;

  case STATUS_SHUTDOWN:
    setLEDPattern(LED_PATTERN_FADE_OUT, currentBrightness);
    break;

  default:
    setLEDPattern(LED_PATTERN_STEADY, getLEDBrightness());
    break;
  }

  // Schedule return to idle state if this is a temporary status
  if (duration > 0 && isTemporaryStatus(newStatus)) {
    // The pattern will automatically return to steady state after the blink duration
  }
}

void StatusManager::setLEDPattern(LEDPattern pattern, uint8_t brightness) {
  currentPattern = pattern;
  currentBrightness = brightness;
  patternStartTime = millis();
  lastBlinkTime = millis();
  blinkState = false;

  DEBUG_PRINTF("StatusManager: LED pattern set to %d, brightness %d\n", pattern, brightness);
}

void StatusManager::updateLEDPattern() {
  uint32_t currentTime = millis();

  switch (currentPattern) {
  case LED_PATTERN_OFF:
    powerManager->setLEDPower(0);
    break;

  case LED_PATTERN_STEADY:
    updateSteadyPattern();
    break;

  case LED_PATTERN_BLINK_FAST:
    updateBlinkPattern(LED_BLINK_FAST);
    break;

  case LED_PATTERN_BLINK_SLOW:
    updateBlinkPattern(LED_BLINK_SLOW);
    break;

  case LED_PATTERN_PULSE:
    updatePulsePattern();
    break;

  case LED_PATTERN_FADE_OUT:
    updateFadeOutPattern();
    break;
  }

  // Check if temporary status should return to idle
  if (isTemporaryStatus(currentStatus) &&
    (currentTime - patternStartTime) >= LED_BLINK_DURATION) {
    DEBUG_PRINTLN("StatusManager: Temporary status expired, returning to idle");
    handleStatusChange(STATUS_IDLE, 0);
  }
}

void StatusManager::updateSteadyPattern() {
  powerManager->setLEDPower(currentBrightness);
}

void StatusManager::updateBlinkPattern(uint32_t blinkInterval) {
  uint32_t currentTime = millis();

  if ((currentTime - lastBlinkTime) >= blinkInterval) {
    blinkState = !blinkState;
    lastBlinkTime = currentTime;

    powerManager->setLEDPower(blinkState ? currentBrightness : 0);
  }
}

void StatusManager::updatePulsePattern() {
  uint32_t currentTime = millis();
  uint32_t elapsed = (currentTime - patternStartTime) % LED_PULSE_CYCLE;

  float phase = (float)elapsed * 2.0f * PI / LED_PULSE_CYCLE;

  float sineValue = (sin(phase) + 1.0f) / 2.0f;

  uint8_t minBrightness = currentBrightness / 5;
  uint8_t pulseBrightness = minBrightness + (uint8_t)((currentBrightness - minBrightness) * sineValue);

  powerManager->setLEDPower(pulseBrightness);
}

void StatusManager::updateFadeOutPattern() {
  uint32_t currentTime = millis();
  uint32_t elapsed = currentTime - patternStartTime;
  uint32_t fadeTime = 2000; // 2 seconds fade

  if (elapsed >= fadeTime) {
    powerManager->setLEDPower(0);
    currentPattern = LED_PATTERN_OFF;
  }
  else {
    uint8_t fadeBrightness = currentBrightness * (fadeTime - elapsed) / fadeTime;
    powerManager->setLEDPower(fadeBrightness);
  }
}

uint8_t StatusManager::getLEDBrightness() const {
  if (isLowPowerMode) {
    return LED_BRIGHTNESS_POWER_SAVE;
  }
  return LED_BRIGHTNESS_MAX;
}

bool StatusManager::isTemporaryStatus(DeviceStatus status) const {
  switch (status) {
  case STATUS_BLE_CONNECTED:
  case STATUS_BLE_DISCONNECTED:
  case STATUS_HID_CONNECTED:
  case STATUS_HID_DISCONNECTED:
  case STATUS_BLE_CMD_ERROR:
    return true;
  default:
    return false;
  }
}
