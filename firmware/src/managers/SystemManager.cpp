// src/managers/SystemManager.cpp
#include "managers/SystemManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include <managers/PowerManager.h>
#include <managers/BLEManager.h>
#include <managers/USBManager.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/ledc.h>

extern PowerManager* powerManager;
extern BLEManager* bleManager;
extern USBManager* usbManager;

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
  deepSleepRequested = false;

  DEBUG_PRINTLN("SystemManager initialized successfully (deep sleep enabled)");
  return true;
}

void SystemManager::update() {
  checkPowerButton();
  updateDeepSleepWatchdog();

  if (deepSleepRequested) {
    deepSleepRequested = false;
    delay(10);
    enterDeepSleep();
  }

  static uint32_t lastStatusTime = 0;
  uint32_t currentTime = millis();
  if (currentTime - lastStatusTime >= 10000) {
    lastStatusTime = currentTime;
    uint32_t timeSinceActivity = currentTime - lastActivityTime;
    DEBUG_PRINTF("=== DEEP SLEEP STATUS === Enabled: %s, SBC: %s, BLE: %s, Inactive: %lu ms\n",
      deepSleepEnabled ? "YES" : "NO",
      powerManager->isSBCPowerOn() ? "ON" : "OFF",
      bleManager->isConnected() ? "CONN" : "DISC",
      timeSinceActivity);
  }
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
          deepSleepRequested = true;
        }
        else {
          uint32_t timeRemaining = DEEP_SLEEP_WATCHDOG_TIMEOUT_MS - timeSinceActivity;
          DEBUG_PRINTF("Deep sleep in %lu ms (inactive for %lu ms)\n", timeRemaining, timeSinceActivity);
        }
      }
      else {
        resetActivityTimer();
        DEBUG_PRINTLN("Deep sleep blocked - conditions not met, resetting timer");
      }
    }
    else {
      DEBUG_PRINTLN("Deep sleep watchdog disabled");
    }
  }
}

bool SystemManager::shouldEnterDeepSleep() {
  if (powerManager->isSBCPowerOn()) {
    DEBUG_PRINTLN("Deep sleep blocked: SBC power is ON");
    return false;
  }

  if (bleManager->isConnected()) {
    DEBUG_PRINTLN("Deep sleep blocked: BLE is connected");
    return false;
  }

  DEBUG_PRINTLN("Deep sleep conditions met - ready to sleep");
  return true;
}

void SystemManager::resetActivityTimer() {
  lastActivityTime = millis();
}

void SystemManager::notifyActivity() {
  resetActivityTimer();
  DEBUG_PRINTLN("Activity detected - deep sleep watchdog reset");
  DEBUG_PRINTF("Activity from: millis=%lu\n", millis());
}

void SystemManager::notifyWakeFromDeepSleep() {
  resetActivityTimer();
  DEBUG_PRINTLN("=== WAKE FROM DEEP SLEEP DETECTED ===");
  DEBUG_PRINTLN("Deep sleep watchdog timer reset");
}

void SystemManager::enterDeepSleep() {
  DEBUG_PRINTLN("=== ENTERING DEEP SLEEP ===");

  DEBUG_PRINTLN("Configuring RTC GPIO settings for deep sleep wake-up...");

  rtc_gpio_init((gpio_num_t)PIN_POWER_BUTTON);
  rtc_gpio_set_direction((gpio_num_t)PIN_POWER_BUTTON, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)PIN_POWER_BUTTON);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_POWER_BUTTON);

  rtc_gpio_init((gpio_num_t)PIN_POWER_INPUT_DETECT);
  rtc_gpio_set_direction((gpio_num_t)PIN_POWER_INPUT_DETECT, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en((gpio_num_t)PIN_POWER_INPUT_DETECT);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_POWER_INPUT_DETECT);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  esp_err_t ext1_result = esp_sleep_enable_ext1_wakeup(WAKE_UP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  DEBUG_PRINTF("EXT1 wake-up configuration result: %d\n", ext1_result);

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  DEBUG_FLUSH();
  delay(200);

  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
    DEBUG_PRINTLN("ERROR: FreeRTOS scheduler not running - cannot enter deep sleep safely");
    DEBUG_FLUSH();
    delay(100);
    esp_restart();
    return;
  }

  TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
  if (currentTask == NULL) {
    DEBUG_PRINTLN("ERROR: Not running in task context - cannot enter deep sleep safely");
    DEBUG_FLUSH();
    delay(100);
    esp_restart();
    return;
  }

  DEBUG_PRINTF("Deep sleep from task: %s\n", pcTaskGetName(currentTask));

  esp_task_wdt_delete(NULL);

  DEBUG_PRINTLN("Scheduler verified - entering deep sleep now...");
  DEBUG_FLUSH();
  delay(100);

  esp_deep_sleep_start();

  DEBUG_PRINTLN("ERROR: Failed to enter deep sleep! (This should not happen)");
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
    return 0;
  }

  if (powerManager && powerManager->isSBCPowerOn()) {
    return 0;
  }

  if (bleManager && bleManager->isConnected()) {
    return 0;
  }

  uint32_t timeSinceActivity = millis() - lastActivityTime;
  if (timeSinceActivity >= DEEP_SLEEP_WATCHDOG_TIMEOUT_MS) {
    return 0;
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
    "SYSTEM_INFO:%s|%s|%s|%lu",
    wifiMac.c_str(),
    formattedBtMac.c_str(),
    fwVersion,
    uptimeSeconds);

  return info;
}

const char* SystemManager::getDeepSleepInfo() const {
  static char statusBuffer[64];
  const char* enabledStr = isDeepSleepEnabled() ? "ENABLED" : "DISABLED";
  uint32_t timeUntilSleep = getTimeUntilDeepSleep();
  snprintf(statusBuffer, sizeof(statusBuffer), "DEEP_SLEEP_INFO:%s|%lu", enabledStr, timeUntilSleep);
  return statusBuffer;
}