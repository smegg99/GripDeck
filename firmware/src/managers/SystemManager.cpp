// src/managers/SystemManager.cpp
#include "managers/SystemManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include <managers/PowerManager.h>
#include <WiFi.h>

extern PowerManager* powerManager;

SystemManager::SystemManager() {}

SystemManager::~SystemManager() {}

bool SystemManager::begin() {
  DEBUG_PRINTLN("Initializing SystemManager...");

  lastButtonTime = 0;
  lastButtonState = false;
  buttonPressed = false;
  buttonPressStartTime = 0;

  DEBUG_PRINTLN("SystemManager initialized successfully");
  return true;
}

void SystemManager::update() {
  checkPowerButton();
}

void SystemManager::checkPowerButton() {
  bool currentButtonState = digitalRead(PIN_POWER_BUTTON) == LOW;  // Active low
  uint32_t currentTime = millis();

  if (currentButtonState != lastButtonState &&
    (currentTime - lastButtonTime > POWER_BUTTON_DEBOUNCE)) {

    lastButtonState = currentButtonState;
    lastButtonTime = currentTime;

    if (currentButtonState) {
      buttonPressed = true;
      buttonPressStartTime = currentTime;
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