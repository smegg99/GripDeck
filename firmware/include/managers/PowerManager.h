// include/managers/PowerManager.h
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <cstdint>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <config/Config.h>

struct BatteryData {
  float voltage;          // Battery voltage (V)
  float current;          // Battery current (A) - positive = charging, negative = discharging
  float power;            // Battery power (W)
  float percentage;       // Battery percentage (0-100%)

  String toString() const {
    return "Battery: " + String(voltage, 3) + "V, " +
      String(current, 3) + "A, " +
      String(power, 3) + "W, " +
      String(percentage, 1) + "%";
  }
};

struct ChargerData {
  float voltage;          // Input voltage (V)
  float current;          // Input current (A)
  float power;            // Input power (W)
  bool connected;         // Is charger connected
  float toFullyChargeMs;  // Estimated time to fully charge in milliseconds

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

  void setPowerData(BatteryData battery, ChargerData charger) {
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) == pdPASS) {
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

  float calculateBatteryPercentage(float current, float voltage);
  float calculateEstimatedTimeToFullyCharge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage);
public:
  PowerManager();
  ~PowerManager();

  bool begin();
  void update();

  PowerData getPowerData() const {
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) == pdPASS) {
      xSemaphoreGive(powerDataMutex);
      return powerData;
    }
  }

  void trySetSBCPower(bool on);
  void forceSetSBCPower(bool on) {
    digitalWrite(PIN_SBC_POWER_MOSFET, on ? HIGH : LOW);
  }
  
  bool isSBCPowerOn() const {
    return digitalRead(PIN_SBC_POWER_MOSFET) == HIGH;
  }

  bool canPowerOnSBC() const {
    bool canPowerOn = false;
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) != pdTRUE) {
      PowerData powerData = getPowerData();
      canPowerOn = powerData.battery.percentage >= BATTERY_MIN_PERCENTAGE && !isSBCPowerOn();
      xSemaphoreGive(powerDataMutex);
    }

    return canPowerOn;
  }

  bool isPowerSavingMode() const {
    bool isPowerSaving = false;
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) == pdPASS) {
      isPowerSaving = powerData.powerSavingMode;
      xSemaphoreGive(powerDataMutex);
    }
    return isPowerSaving;
  }

  void setLEDPower(uint8_t brightness);
  void enableLEDs(bool enable);
  bool areLEDsEnabled() const { return ledsEnabled; }
};

#endif // POWER_MANAGER_H