// include/managers/PowerManager.h
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <cstdint>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <config/Config.h>

// Forward declaration
class StatusManager;

struct BatteryData {
  float voltage;               // Battery voltage (V)
  float current;               // Battery current (A) - positive = charging, negative = discharging
  float power;                 // Battery power (W)
  float percentage;            // Battery percentage (0-100%)
  uint32_t toFullyDischargeMs; // Estimated time to fully discharge in milliseconds

  String toString() const {
    return "Battery: " + String(voltage, 3) + "V, " +
      String(current, 3) + "A, " +
      String(power, 3) + "W, " +
      String(percentage, 1) + "%";
    ", ETA: " + (toFullyDischargeMs > 0 ? String(toFullyDischargeMs / 1000) + "s" : "N/A");
  }
};

struct ChargerData {
  float voltage;             // Input voltage (V)
  float current;             // Input current (A)
  float power;               // Input power (W)
  bool connected;            // Is charger connected
  uint32_t toFullyChargeMs;  // Estimated time to fully charge in milliseconds

  String toString() const {
    String etaStr = "";
    if (toFullyChargeMs > 0) {
      int totalSeconds = toFullyChargeMs / 1000;
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

  uint16_t readRegister(uint8_t reg);
  float readBusVoltage(uint8_t channel);
  float readShuntVoltage(uint8_t channel);
  float readCurrent(uint8_t channel);

  bool shouldSBCBePoweredOn();

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

    snprintf(info, sizeof(info),
      "POWER_INFO:%.3f|%.3f|%.3f|%.3f|%.3f|%.3f|%.1f",
      data.battery.voltage,
      data.battery.current,
      data.battery.toFullyDischargeMs / 1000.0f,
      data.charger.voltage,
      data.charger.current,
      data.charger.toFullyChargeMs / 1000.0f,
      data.battery.percentage
    );

    return info;
  }
};

#endif // POWER_MANAGER_H