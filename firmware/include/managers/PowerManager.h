// include/managers/PowerManager.h
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <cstdint>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <config/Config.h>
#include "utils/DebugSerial.h"

class StatusManager;

struct BatteryData {
  float voltage;
  float current;
  float power;
  float percentage;
  uint32_t toFullyDischargeS;

  String toString() const {
    String etaStr = "";
    if (toFullyDischargeS > 0) {
      int totalSeconds = toFullyDischargeS;
      int hours = totalSeconds / 3600;
      int minutes = (totalSeconds % 3600) / 60;
      int seconds = totalSeconds % 60;

      if (hours > 0) {
        etaStr = String(hours) + "h " + String(minutes) + "m";
      }
      else if (minutes > 0) {
        etaStr = String(minutes) + "m " + String(seconds) + "s";
      }
      else {
        etaStr = String(seconds) + "s";
      }
    }
    else {
      etaStr = "N/A";
    }

    return "Battery: " + String(voltage, 3) + "V, " +
      String(current, 3) + "A, " +
      String(power, 3) + "W, " +
      String(percentage, 1) + "%" +
      ", ETA: " + etaStr;
  }
};

struct ChargerData {
  float voltage;
  float current;
  float power;
  bool connected;
  uint32_t toFullyChargeS;  

  String toString() const {
    String etaStr = "";
    if (toFullyChargeS > 0) {
      int totalSeconds = toFullyChargeS;
      int minutes = totalSeconds / 60;
      int seconds = totalSeconds % 60;
      etaStr = String(minutes) + "m " + String(seconds) + "s";
    }
    else {
      etaStr = "N/A";
    }

    return "Charger: " + String(voltage, 3) + "V, " +
      String(current, 3) + "A, " +
      String(power, 3) + "W, " +
      (connected ? "Connected" : "Disconnected") +
      ", ETA: " + etaStr;
  }
};

struct PowerData {
  BatteryData battery;
  ChargerData charger;
  uint32_t timestamp;
  bool powerSavingMode;

  String toString() const {
    return "PowerData [" + String(timestamp) + "ms]:\n" +
      "  " + battery.toString() + "\n" +
      "  " + charger.toString() + "\n" +
      "  Power Saving: " + (powerSavingMode ? "ON" : "OFF");
  }
};

class PowerManager {
private:
  PowerData powerData = { { 0.0f, 0.0f, 0.0f, 0 }, { 0.0f, 0.0f, 0.0f, false }, 0, false };

  SemaphoreHandle_t powerDataMutex = nullptr;

  bool ledsEnabled = false;
  bool previousPowerSavingMode = false;

  void setPowerData(BatteryData battery, ChargerData charger) {
    if (powerDataMutex && xSemaphoreTake(powerDataMutex, pdMS_TO_TICKS(100)) == pdPASS) {
      powerData.battery = battery;
      powerData.charger = charger;
      powerData.timestamp = millis();
      powerData.powerSavingMode = !charger.connected && (battery.percentage <= BATTERY_SAVING_MODE);
      xSemaphoreGive(powerDataMutex);
    }
  }

  bool initializeINA3221();
  bool testINA3221();
  void readChannels(BatteryData& batteryData, ChargerData& chargerData);
  void calculateEstimatedTimes(BatteryData& batteryData, ChargerData& chargerData);

  uint16_t readRegister(uint8_t reg);
  float readBusVoltage(uint8_t channel);
  float readShuntVoltage(uint8_t channel);
  float readCurrent(uint8_t channel);

  bool shouldSBCBePoweredOn();

  float interpPercent(float v);

  float calculateBatteryPercentage(float current, float voltage);
  uint32_t calculateEstimatedTimeToFullyCharge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage);

  uint32_t calculateEstimatedTimeToFullyDischarge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage);
public:
  PowerManager();
  ~PowerManager();

  bool begin();
  void update();

  PowerData getPowerData() const {
    PowerData data;
    if (powerDataMutex && xSemaphoreTake(powerDataMutex, pdMS_TO_TICKS(100)) == pdPASS) {
      data = powerData;
      xSemaphoreGive(powerDataMutex);
    }
    return data;
  }

  void trySetSBCPower(bool on);
  void forceSetSBCPower(bool on) {
    digitalWrite(PIN_SBC_POWER_MOSFET, on ? HIGH : LOW);
  }

  bool isSBCPowerOn() const {
    return digitalRead(PIN_SBC_POWER_MOSFET) == HIGH;
  }

  bool canPowerOnSBC() const {
    if (!powerDataMutex) {
      return false;
    }

    bool canPowerOn = false;
    if (xSemaphoreTake(powerDataMutex, pdMS_TO_TICKS(100)) == pdPASS) {
      canPowerOn = powerData.battery.percentage >= BATTERY_MIN_PERCENTAGE;
      xSemaphoreGive(powerDataMutex);
    }
    else {
      return false;
    }

    return canPowerOn;
  }

  bool isPowerSavingMode() const {
    if (!powerDataMutex) {
      return false;
    }

    bool isPowerSaving = false;
    if (xSemaphoreTake(powerDataMutex, pdMS_TO_TICKS(100)) == pdPASS) {
      isPowerSaving = powerData.powerSavingMode;
      xSemaphoreGive(powerDataMutex);
    }
    return isPowerSaving;
  }

  void setLEDPower(uint8_t brightness);
  void enableLEDs(bool enable);
  bool areLEDsEnabled() const { return ledsEnabled; }

  const char* getPowerInfo() const {
    static char info[256];

    PowerData data = getPowerData();

    DEBUG_VERBOSE_PRINTF("BLE Power Info - Battery: %.3fV/%.3fA/%.1f%%, Discharge: %us, Charger: %.3fV/%.3fA, Charge: %us\n",
      data.battery.voltage, data.battery.current, data.battery.percentage, data.battery.toFullyDischargeS,
      data.charger.voltage, data.charger.current, data.charger.toFullyChargeS);

    snprintf(info, sizeof(info),
      "POWER_INFO:%.3f|%.3f|%u|%.3f|%.3f|%u|%.1f",
      data.battery.voltage,
      data.battery.current,
      data.battery.toFullyDischargeS,
      data.charger.voltage,
      data.charger.current,
      data.charger.toFullyChargeS,
      data.battery.percentage
    );

    DEBUG_VERBOSE_PRINTF("BLE Power Info String: %s\n", info);

    return info;
  }
};

#endif // POWER_MANAGER_H