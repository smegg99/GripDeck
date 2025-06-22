// include/managers/PowerManager.h
#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <cstdint>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <config/Config.h>

enum PowerState {
  POWER_OFF,           // Power is off and the controller is not recognized by the SBC
  POWER_STARTING,      // Power is starting up, before SBC recognizes the controller
  POWER_ON,            // Power is on and SBC is running after the controller is recognized by the SBC
  POWER_SHUTTING_DOWN, // Power is shutting down, the controller still is recognized by the SBC
  POWER_SLEEP          // The controller is in sleep mode, SBC is not running
};

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
    return "Charger: " + String(voltage, 3) + "V, " +
      String(current, 3) + "A, " +
      String(power, 3) + "W, " +
      (connected ? "Connected" : "Disconnected") +
      ", ETA: " + String(toFullyChargeMs / 1000.0, 1) + "s";
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
  PowerState powerState = POWER_OFF;
  PowerData powerData = { { 0.0f, 0.0f, 0.0f, 0 }, { 0.0f, 0.0f, 0.0f, false }, 0, false };

  SemaphoreHandle_t powerStateMutex = nullptr;
  SemaphoreHandle_t powerDataMutex = nullptr;

  uint32_t lastUpdateTime = 0;

  bool ledsEnabled = false;

  void setSBCPower(bool on);
  bool isSBCPowerOn() const {
    return digitalRead(PIN_SBC_POWER_MOSFET) == HIGH;
  }
  bool canPowerOnSBC() const {
    bool canPowerOn = false;
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) != pdTRUE) {
      PowerData powerData = getPowerData();
      canPowerOn = powerData.battery.percentage >= BATTERY_MIN_PERCENTAGE;
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
  BatteryData readBatteryChannel();
  ChargerData readChargerChannel();

  uint16_t readRegister(uint8_t reg);
  float readBusVoltage(uint8_t channel);
  float readShuntVoltage(uint8_t channel);
  float readCurrent(uint8_t channel);

  static const int READING_SAMPLES = 5;
  float voltageReadings[READING_SAMPLES];
  float currentReadings[READING_SAMPLES];
  int readingIndex;

  static const int UPDATE_INTERVAL = 100;

  float calculateBatteryPercentage(float current, float voltage);
  float calculateEstimatedTimeToFullyCharge(float current, float voltage, float percentage);
public:
  PowerManager();
  ~PowerManager();

  bool begin();
  void update();

  PowerState getPowerState() const {
    if (xSemaphoreTake(powerStateMutex, portMAX_DELAY) == pdPASS) {
      xSemaphoreGive(powerStateMutex);
      return powerState;
    }
  }

  void setPowerState(PowerState state) {
    if (xSemaphoreTake(powerStateMutex, portMAX_DELAY) == pdPASS) {
      powerState = state;
      xSemaphoreGive(powerStateMutex);
    }
  }

  PowerData getPowerData() const {
    if (xSemaphoreTake(powerDataMutex, portMAX_DELAY) == pdPASS) {
      xSemaphoreGive(powerDataMutex);
      return powerData;
    }
  }
};

#endif // POWER_MANAGER_H