// src/managers/PowerManager.cpp
#include "managers/PowerManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include <Wire.h>
#include <semphr.h>
#include <managers/USBManager.h>

extern USBManager* usbManager;

PowerManager::PowerManager() {}

PowerManager::~PowerManager() {
  if (powerDataMutex) {
    vSemaphoreDelete(powerDataMutex);
  }
}

bool PowerManager::begin() {
  DEBUG_PRINTLN("Initializing PowerManager...");

  powerDataMutex = xSemaphoreCreateMutex();
  if (!powerDataMutex) {
    DEBUG_PRINTLN("ERROR: Failed to create power data mutex");
    return false;
  }

  if (!initializeINA3221()) {
    DEBUG_PRINTLN("ERROR: INA3221 initialization failed");
    return false;
  }

  trySetSBCPower(false);
  setLEDPower(0);

  DEBUG_PRINTLN("PowerManager initialized successfully");
  return true;
}

void PowerManager::update() {
  // Don't update too frequently to avoid I2C bus issues
  if (millis() - lastUpdateTime < TASK_INTERVAL_POWER) {
    return;
  }

  BatteryData batteryData;
  ChargerData chargerData;

  readChannels(batteryData, chargerData);
  setPowerData(batteryData, chargerData);

  DEBUG_PRINTLN(powerData.toString());

  lastUpdateTime = millis();
}

void PowerManager::trySetSBCPower(bool on) {
  if (on) {
    DEBUG_PRINTLN("Turning SBC power ON");
    digitalWrite(PIN_SBC_POWER_MOSFET, HIGH);

    unsigned long startTime = millis();
    while (!usbManager->isUSBConnected() && millis() - startTime < USB_CONNECTION_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (usbManager->isUSBConnected()) {
      DEBUG_PRINTLN("SBC recognized USB controller");
      return;
    }

    DEBUG_PRINTLN("WARNING: SBC did not recognize USB controller within timeout, trying to turn off power");
    trySetSBCPower(false);
    return;
  }
  else {
    DEBUG_PRINTLN("Turning SBC power OFF");
    // TODO: Send 0x80 HID command to SBC to turn off and wait for it to stop recognizing the controller or timeout
    digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
  }
}

void PowerManager::setLEDPower(uint8_t brightness) {
  if (!ledsEnabled && brightness > 0) {
    ledsEnabled = true;
  }
  else if (ledsEnabled && brightness == 0) {
    ledsEnabled = false;
  }

  if (isPowerSavingMode() && brightness > 0) {
    brightness = brightness / 4;
  }

  ledcWrite(LED_PWM_CHANNEL, brightness);
}

void PowerManager::enableLEDs(bool enable) {
  ledsEnabled = enable;
  if (!enable) {
    setLEDPower(0);
  }
}

uint16_t PowerManager::readRegister(uint8_t reg) {
  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    DEBUG_PRINTF("ERROR: Failed to write to register 0x%02X\n", reg);
    return 0;
  }

  Wire.requestFrom((uint8_t)INA3221_I2C_ADDRESS, (uint8_t)2);
  if (Wire.available() < 2) {
    DEBUG_PRINTF("ERROR: Failed to read from register 0x%02X\n", reg);
    return 0;
  }

  uint16_t value = (Wire.read() << 8) | Wire.read();
  return value;
}

float PowerManager::readShuntVoltage(uint8_t channel) {
  uint8_t reg;
  switch (channel) {
  case 1: reg = INA3221_CHANNEL_1_SHUNT_REGISTER; break;
  case 2: reg = INA3221_CHANNEL_2_SHUNT_REGISTER; break;
  case 3: reg = INA3221_CHANNEL_3_SHUNT_REGISTER; break;
  default: return 0.0f;
  }

  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return 0.0f;
  }

  Wire.requestFrom((uint8_t)INA3221_I2C_ADDRESS, (uint8_t)2);
  if (Wire.available() >= 2) {
    int16_t rawShunt = (Wire.read() << 8) | Wire.read();
    return (rawShunt >> 3) * 0.00004f;
  }

  return 0.0f;
}

float PowerManager::readBusVoltage(uint8_t channel) {
  uint8_t reg;
  switch (channel) {
  case 1: reg = INA3221_CHANNEL_1_BUS_REGISTER; break;
  case 2: reg = INA3221_CHANNEL_2_BUS_REGISTER; break;
  case 3: reg = INA3221_CHANNEL_3_BUS_REGISTER; break;
  default: return 0.0f;
  }

  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return 0.0f;
  }

  Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
  if (Wire.available() >= 2) {
    uint16_t rawVoltage = (Wire.read() << 8) | Wire.read();
    return (rawVoltage >> 3) * 0.008f;
  }

  return 0.0f;
}

float PowerManager::readCurrent(uint8_t channel) {
  return readShuntVoltage(channel) / INA3221_SHUNT_RESISTANCE;
}

void PowerManager::readChannels(BatteryData& batteryData, ChargerData& chargerData) {
  // Read battery channel first
  float batteryVoltage = readBusVoltage(INA3221_CHANNEL_BATTERY);
  float batteryCurrent = readCurrent(INA3221_CHANNEL_BATTERY);
  float batteryPercentage = calculateBatteryPercentage(batteryCurrent, batteryVoltage);

  DEBUG_VERBOSE_PRINTF("Battery Channel - Voltage: %.3fV, Current: %.6fA\n", batteryVoltage, batteryCurrent);

  batteryData = BatteryData{
    batteryVoltage,
    batteryCurrent,
    batteryVoltage * batteryCurrent,
    batteryPercentage
  };

  // Read charger channel and use battery data for calculations
  float chargerVoltage = readBusVoltage(INA3221_CHANNEL_CHARGER);
  float chargerCurrent = readCurrent(INA3221_CHANNEL_CHARGER);

  DEBUG_VERBOSE_PRINTF("Charger Channel - Voltage: %.3fV, Current: %.6fA\n", chargerVoltage, chargerCurrent);

  chargerData = ChargerData{
    chargerVoltage,
    chargerCurrent,
    chargerVoltage * chargerCurrent,
    chargerVoltage >= MIN_BATTERY_CHARGING_VOLTAGE,
    calculateEstimatedTimeToFullyCharge(chargerCurrent, chargerVoltage, batteryCurrent, batteryVoltage, batteryPercentage)
  };

  DEBUG_VERBOSE_PRINTF("Charge Time Calculation - Current: %.6fA, Voltage: %.3fV, Percentage: %.1f%%, ETA: %.1fs\n",
    chargerCurrent, batteryVoltage, batteryPercentage, chargerData.toFullyChargeMs / 1000.0f);
}

bool PowerManager::testINA3221() {
  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  uint16_t manufacturerID = readRegister(0xFE);
  DEBUG_PRINTF("Manufacturer ID: 0x%04X\n", manufacturerID);

  if (manufacturerID == 0x5449) {
    DEBUG_PRINTLN("INA3221 manufacturer ID verified");
    return true;
  }

  DEBUG_PRINTF("WARNING: Unexpected manufacturer ID: 0x%04X (expected 0x5449)\n", manufacturerID);

  DEBUG_PRINTLN("Testing INA3221 readings...");
  for (int channel = 1; channel <= 3; channel++) {
    float voltage = readBusVoltage(channel);
    float current = readCurrent(channel);
    DEBUG_PRINTF("Channel %d: %.3fV, %.6fA\n", channel, voltage, current);
  }

  float batteryVoltage = readBusVoltage(INA3221_CHANNEL_BATTERY);
  if (batteryVoltage > 0.1f) {
    DEBUG_PRINTF("Found valid battery readings on channel %d: %.3fV\n",
      INA3221_CHANNEL_BATTERY, batteryVoltage);
  }
  else {
    DEBUG_PRINTLN("WARNING: No valid battery voltage found!");
  }

  return true;
}

bool PowerManager::initializeINA3221() {
  DEBUG_PRINTLN("Initializing INA3221...");

  DEBUG_PRINTLN("Scanning I2C bus for devices...");
  int deviceCount = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      DEBUG_PRINTF("Found I2C device at address 0x%02X\n", address);
      deviceCount++;
    }
  }
  DEBUG_PRINTF("Found %d I2C devices\n", deviceCount);

  if (!testINA3221()) {
    DEBUG_PRINTF("INA3221 not found at address 0x%02X\n", INA3221_I2C_ADDRESS);
    return false;
  }

  DEBUG_PRINTF("INA3221 initialized successfully at address 0x%02X\n", INA3221_I2C_ADDRESS);

  return true;
}

float PowerManager::calculateBatteryPercentage(float current, float voltage) {
  const float LIPO_MIN_VOLTAGE = 3.0f;
  const float LIPO_MAX_VOLTAGE = 4.2f;
  const float LIPO_NOMINAL = 3.7f;
  const float LIPO_CUTOFF = 3.3f;

  // Values based on typical LiPo discharge curve
  const float voltagePoints[] = { 3.0f, 3.3f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f, 4.2f };
  const float percentagePoints[] = { 0.0f, 5.0f, 15.0f, 25.0f, 40.0f, 60.0f, 75.0f, 85.0f, 95.0f, 100.0f };
  const int numPoints = sizeof(voltagePoints) / sizeof(voltagePoints[0]);

  if (voltage < LIPO_MIN_VOLTAGE) return 0.0f;
  if (voltage >= LIPO_MAX_VOLTAGE) return 100.0f;

  float basePercentage = 0.0f;
  for (int i = 0; i < numPoints - 1; i++) {
    if (voltage >= voltagePoints[i] && voltage <= voltagePoints[i + 1]) {
      float ratio = (voltage - voltagePoints[i]) / (voltagePoints[i + 1] - voltagePoints[i]);
      basePercentage = percentagePoints[i] + ratio * (percentagePoints[i + 1] - percentagePoints[i]);
      break;
    }
  }

  float voltageSagCompensation = 0.0f;
  if (current < -0.5f) {
    float estimatedSag = abs(current) * 0.1f;
    float compensatedVoltage = voltage + estimatedSag;

    for (int i = 0; i < numPoints - 1; i++) {
      if (compensatedVoltage >= voltagePoints[i] && compensatedVoltage <= voltagePoints[i + 1]) {
        float ratio = (compensatedVoltage - voltagePoints[i]) / (voltagePoints[i + 1] - voltagePoints[i]);
        float compensatedPercentage = percentagePoints[i] + ratio * (percentagePoints[i + 1] - percentagePoints[i]);
        voltageSagCompensation = compensatedPercentage - basePercentage;
        break;
      }
    }
  }

  float finalPercentage = basePercentage + voltageSagCompensation;

  if (finalPercentage < 0.0f) finalPercentage = 0.0f;
  if (finalPercentage > 100.0f) finalPercentage = 100.0f;

  return finalPercentage;
}

float PowerManager::calculateEstimatedTimeToFullyCharge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage) {
  if (chargerCurrent <= 0.001f || chargerVoltage < MIN_BATTERY_CHARGING_VOLTAGE || percentage >= 99.9f) {
    return 0.0f;
  }

  float netChargingCurrent = chargerCurrent + batteryCurrent;

  if (netChargingCurrent <= 0.001f) {
    return 0.0f;
  }

  float chargeRate = (netChargingCurrent * 1000.0f * 100.0f) / BATTERY_CAPACITY_MAH;
  if (chargeRate <= 0.0f) {
    return 0.0f;
  }

  float remainingPercentage = 100.0f - percentage;
  float hoursToCharge = remainingPercentage / chargeRate;

  return hoursToCharge * 3600.0f * 1000.0f;
}