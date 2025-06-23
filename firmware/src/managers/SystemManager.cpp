// src/managers/SystemManager.cpp
#include "managers/SystemManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include <managers/PowerManager.h>
#include <managers/BLEManager.h>
#include <WiFi.h>
#include <esp_sleep.h>

extern PowerManager* powerManager;
extern BLEManager* bleManager;

SystemManager::SystemManager() {}

SystemManager::~SystemManager() {}

bool SystemManager::begin() {
  DEBUG_PRINTLN("Initializing SystemManager...");

  lastButtonTime = 0;
  lastButtonState = false;
  buttonPressed = false;
  buttonPressStartTime = 0;

  lastActivityTime = millis();
  lastActivityCheck = millis();
  deepSleepEnabled = true;

  DEBUG_PRINTLN("SystemManager initialized successfully");
  return true;
}

void SystemManager::update() {
  checkPowerButton();
  updateDeepSleepWatchdog();
}

void SystemManager::checkPowerButton() {
  bool currentButtonState = digitalRead(PIN_POWER_BUTTON) == LOW; // Active low
  uint32_t currentTime = millis();

  if (currentButtonState != lastButtonState &&
    (currentTime - lastButtonTime > POWER_BUTTON_DEBOUNCE)) {

    lastButtonState = currentButtonState;
    lastButtonTime = currentTime;

    if (currentButtonState) {
      buttonPressed = true;
      buttonPressStartTime = currentTime;
      notifyActivity();
      DEBUG_PRINTLN("Power button pressed");
    }
    else if (buttonPressed) {
      buttonPressed = false;
      uint32_t pressDuration = currentTime - buttonPressStartTime;
      DEBUG_PRINTF("Power button released after %lums\n", pressDuration);

      if (pressDuration >= POWER_BUTTON_SHORT_PRESS_MIN &&
        pressDuration <= POWER_BUTTON_SHORT_PRESS_MAX) {
        DEBUG_PRINTLN("Short press: Toggling SBC power");
        powerManager->trySetSBCPower(!powerManager->isSBCPowerOn());
      }
      else if (pressDuration >= POWER_BUTTON_LONG_PRESS_MIN) {
        DEBUG_PRINTLN("Long press: Hard shutdown");
        powerManager->forceSetSBCPower(false);
      }
    }
  }
}

void SystemManager::updateDeepSleepWatchdog() {
  uint32_t currentTime = millis();

  if (currentTime - lastActivityCheck >= DEEP_SLEEP_ACTIVITY_RESET_INTERVAL_MS) {
    lastActivityCheck = currentTime;

    if (deepSleepEnabled) {
      if (shouldEnterDeepSleep()) {
        uint32_t timeSinceActivity = currentTime - lastActivityTime;
        if (timeSinceActivity >= DEEP_SLEEP_WATCHDOG_TIMEOUT_MS) {
          DEBUG_PRINTF("Deep sleep watchdog triggered after %lu ms of inactivity\n", timeSinceActivity);
          enterDeepSleep();
        }
      }
      else {
        resetActivityTimer();
      }
    }
  }
}

bool SystemManager::shouldEnterDeepSleep() {
  if (powerManager && powerManager->isSBCPowerOn()) {
    return false;
  }

  if (bleManager && bleManager->isConnected()) {
    return false;
  }

  return true;
}

void SystemManager::resetActivityTimer() {
  lastActivityTime = millis();
}

void SystemManager::notifyActivity() {
  resetActivityTimer();
  DEBUG_PRINTLN("Activity detected - deep sleep watchdog reset");
}

void SystemManager::notifyWakeFromDeepSleep() {
  resetActivityTimer();
  DEBUG_PRINTLN("=== WAKE FROM DEEP SLEEP DETECTED ===");
  DEBUG_PRINTLN("Deep sleep watchdog timer reset");
}

void SystemManager::enterDeepSleep() {
  DEBUG_PRINTLN("=== ENTERING DEEP SLEEP ===");
  DEBUG_PRINTLN("Configuring wake-up sources...");

  // NOTE: Idk if i should do it the second time here, but it seems to work fine
  esp_sleep_enable_ext1_wakeup(WAKE_UP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH);

  DEBUG_PRINTF("Wake-up pin mask: 0x%llX\n", WAKE_UP_PIN_MASK);

  delay(100);

  DEBUG_PRINTLN("Going to deep sleep now...");
  delay(50);

  esp_deep_sleep_start();

  DEBUG_PRINTLN("ERROR: Failed to enter deep sleep!");
  esp_restart();
}

void SystemManager::enableDeepSleep() {
  deepSleepEnabled = true;
  resetActivityTimer();
  DEBUG_PRINTLN("Deep sleep watchdog enabled");
}

void SystemManager::disableDeepSleep() {
  deepSleepEnabled = false;
  DEBUG_PRINTLN("Deep sleep watchdog disabled");
}

bool SystemManager::isDeepSleepEnabled() const {
  return deepSleepEnabled;
}

uint32_t SystemManager::getTimeUntilDeepSleep() const {
  if (!deepSleepEnabled) {
    return 0; // Deep sleep disabled
  }

  // Check blocking conditions
  if (powerManager && powerManager->isSBCPowerOn()) {
    return 0; // Deep sleep blocked by SBC power
  }

  if (bleManager && bleManager->isConnected()) {
    return 0; // Deep sleep blocked by BLE connection
  }

  uint32_t timeSinceActivity = millis() - lastActivityTime;
  if (timeSinceActivity >= DEEP_SLEEP_WATCHDOG_TIMEOUT_MS) {
    return 0; // Should already be in deep sleep
  }

  return DEEP_SLEEP_WATCHDOG_TIMEOUT_MS - timeSinceActivity;
}

const char* SystemManager::getSystemInfo() const {
  static char info[256];

  String wifiMac = WiFi.macAddress();
  String btMac = String(ESP.getEfuseMac(), HEX);
  btMac.toUpperCase();

  String formattedBtMac = "";
  for (int i = 0; i < btMac.length(); i += 2) {
    if (i > 0) formattedBtMac += ":";
    formattedBtMac += btMac.substring(i, i + 2);
  }

  char fwVersion[16];
  snprintf(fwVersion, sizeof(fwVersion), "0x%04X", FIRMWARE_VERSION);

  uint32_t uptimeSeconds = millis() / 1000;

  snprintf(info, sizeof(info),
    "SYSTEM_INFO|%s|%s|%s|%lu",
    wifiMac.c_str(),
    formattedBtMac.c_str(),
    fwVersion,
    uptimeSeconds);

  return info;
}