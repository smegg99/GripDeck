// src/managers/PowerManager.cpp
#include "managers/PowerManager.h"
#include "managers/StatusManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include <Wire.h>
#include <semphr.h>
#include <managers/USBManager.h>

extern USBManager* usbManager;
extern StatusManager* statusManager;

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

  DEBUG_PRINTLN("Forcing SBC power OFF during initialization");
  digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
  setLEDPower(0);

  DEBUG_PRINTLN("PowerManager initialized successfully");
  return true;
}

void PowerManager::update() {
  static uint32_t lastHeapCheckTime = 0;
  const uint32_t heapCheckInterval = 5000;
  uint32_t currentTime = millis();

  if (currentTime - lastHeapCheckTime >= heapCheckInterval) {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minFreeHeap = ESP.getMinFreeHeap();
    if (freeHeap < 10000) {
      DEBUG_PRINTF("WARNING: Low heap memory! Free: %u bytes, Min: %u bytes\n",
        freeHeap, minFreeHeap);
    }
    lastHeapCheckTime = currentTime;
  }

  BatteryData batteryData;
  ChargerData chargerData;

  readChannels(batteryData, chargerData);
  setPowerData(batteryData, chargerData);

  if (batteryData.toFullyDischargeS == 0) {
    DEBUG_VERBOSE_PRINTF("Discharge Time Debug - No discharge time calculated\n");
  }
  else {
    DEBUG_VERBOSE_PRINTF("Discharge Time Debug - Calculated: %us (%.1f hours)\n",
      batteryData.toFullyDischargeS, batteryData.toFullyDischargeS / 3600.0f);
  }

  if (!shouldSBCBePoweredOn() && isSBCPowerOn()) {
    DEBUG_PRINTLN("SBC power is ON but should be OFF, turning it OFF");
    trySetSBCPower(false);
  }

  bool currentPowerSavingMode = isPowerSavingMode();
  if (statusManager && currentPowerSavingMode != previousPowerSavingMode) {
    statusManager->setLowPowerMode(currentPowerSavingMode);
    previousPowerSavingMode = currentPowerSavingMode;
  }

  PowerData currentPowerData = getPowerData();
  DEBUG_PRINTLN(currentPowerData.toString());
}

void PowerManager::trySetSBCPower(bool on) {
  if (on) {
    DEBUG_PRINTLN("Checking if SBC can be powered on...");
    DEBUG_PRINTLN("About to call canPowerOnSBC()");
    bool canPower = canPowerOnSBC();
    DEBUG_PRINTF("canPowerOnSBC() returned: %s\n", canPower ? "true" : "false");
    bool sbcAlreadyOn = isSBCPowerOn();
    DEBUG_PRINTF("isSBCPowerOn() returned: %s\n", sbcAlreadyOn ? "true" : "false");

    if (!canPower || sbcAlreadyOn) {
      DEBUG_PRINTLN("WARNING: SBC cannot be powered on due to low battery or already powered on");
      statusManager->setStatus(STATUS_POWER_OFF, LED_BLINK_DURATION);
      return;
    }
    DEBUG_PRINTLN("Turning SBC power ON");
    digitalWrite(PIN_SBC_POWER_MOSFET, HIGH);

    unsigned long startTime = millis();
    while (!usbManager->isUSBConnected() && millis() - startTime < USB_CONNECTION_TIMEOUT) {
      delay(100);
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

    if (!usbManager) {
      DEBUG_PRINTLN("WARNING: USBManager not available, forcing power off without graceful shutdown");
      digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
      return;
    }

    usbManager->sendSystemPowerKey();

    unsigned long startTime = millis();
    while (usbManager->isUSBConnected() && millis() - startTime < USB_CONNECTION_TIMEOUT) {
      delay(100);
    }

    if (!usbManager->isUSBConnected()) {
      DEBUG_PRINTLN("SBC stopped recognizing USB controller");
    }
    else {
      DEBUG_PRINTLN("WARNING: SBC did not stop recognizing USB controller within timeout, forcing power off anyway");
    }

    digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
  }
}

bool PowerManager::shouldSBCBePoweredOn() {
  PowerData currentPowerData = getPowerData();
  return currentPowerData.battery.percentage >= BATTERY_MIN_PERCENTAGE;
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
  default:
    DEBUG_PRINTF("ERROR: Invalid channel %d for shunt voltage reading\n", channel);
    return 0.0f;
  }

  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    DEBUG_PRINTF("ERROR: Failed to write to shunt register 0x%02X (channel %d)\n", reg, channel);
    return 0.0f;
  }

  Wire.requestFrom((uint8_t)INA3221_I2C_ADDRESS, (uint8_t)2);
  if (Wire.available() >= 2) {
    uint16_t rawShunt = (Wire.read() << 8) | Wire.read();
    int16_t signedShunt = (int16_t)rawShunt;
    float shuntVoltage = (signedShunt >> 3) * 0.00004f; // 40μV per LSB, right-shift by 3 to ignore reserved bits

    DEBUG_VERBOSE_PRINTF("Ch%d Shunt: Raw=0x%04X, Signed=%d, Voltage=%.6fV\n",
      channel, rawShunt, signedShunt, shuntVoltage);

    return shuntVoltage;
  }

  DEBUG_PRINTF("ERROR: Failed to read from shunt register 0x%02X (channel %d)\n", reg, channel);
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
  float shuntVoltage = readShuntVoltage(channel);
  if (shuntVoltage == 0.0f) {
    return 0.0f;
  }

  float current = shuntVoltage / INA3221_SHUNT_RESISTANCE;
  DEBUG_VERBOSE_PRINTF("Ch%d Current: Shunt=%.6fV, Resistance=%.3fΩ, Current=%.6fA\n",
    channel, shuntVoltage, INA3221_SHUNT_RESISTANCE, current);

  return current;
}

void PowerManager::readChannels(BatteryData& batteryData, ChargerData& chargerData) {
  float batteryVoltage = readBusVoltage(INA3221_CHANNEL_BATTERY);
  float batteryCurrent = readCurrent(INA3221_CHANNEL_BATTERY);
  float batteryPercentage = calculateBatteryPercentage(batteryCurrent, batteryVoltage);

  DEBUG_VERBOSE_PRINTF("Battery Channel - Voltage: %.3fV, Current: %.6fA\n", batteryVoltage, batteryCurrent);

  float chargerVoltage = readBusVoltage(INA3221_CHANNEL_CHARGER);
  float chargerCurrent = readCurrent(INA3221_CHANNEL_CHARGER);

  DEBUG_VERBOSE_PRINTF("Charger Channel - Voltage: %.3fV, Current: %.6fA\n", chargerVoltage, chargerCurrent);

  batteryData = BatteryData{
    batteryVoltage,
    batteryCurrent,
    batteryVoltage * batteryCurrent,
    batteryPercentage,
    calculateEstimatedTimeToFullyDischarge(chargerCurrent, chargerVoltage, batteryCurrent, batteryVoltage, batteryPercentage)
  };

  chargerData = ChargerData{
    chargerVoltage,
    chargerCurrent,
    chargerVoltage * chargerCurrent,
    chargerVoltage >= MIN_BATTERY_CHARGING_VOLTAGE,
    calculateEstimatedTimeToFullyCharge(chargerCurrent, chargerVoltage, batteryCurrent, batteryVoltage, batteryPercentage)
  };

  DEBUG_VERBOSE_PRINTF("Charge Time Calculation - Current: %.6fA, Voltage: %.3fV, Percentage: %.1f%%, ETA: %us\n",
    chargerCurrent, batteryVoltage, batteryPercentage, chargerData.toFullyChargeS);
}

bool PowerManager::testINA3221() {
  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  uint16_t manufacturerID = readRegister(0xFE);
  DEBUG_PRINTF("Manufacturer ID: 0x%04X\n", manufacturerID);

  uint16_t dieID = readRegister(0xFF);
  DEBUG_PRINTF("Die ID: 0x%04X\n", dieID);

  if (manufacturerID == 0x5449) {
    DEBUG_PRINTLN("INA3221 manufacturer ID verified");
  }
  else {
    DEBUG_PRINTF("WARNING: Unexpected manufacturer ID: 0x%04X (expected 0x5449)\n", manufacturerID);
  }

  DEBUG_PRINTLN("Testing INA3221 raw register readings...");
  for (int channel = 1; channel <= 3; channel++) {
    uint8_t shuntReg, busReg;
    switch (channel) {
    case 1: shuntReg = 0x01; busReg = 0x02; break;
    case 2: shuntReg = 0x03; busReg = 0x04; break;
    case 3: shuntReg = 0x05; busReg = 0x06; break;
    }

    uint16_t rawShunt = readRegister(shuntReg);
    uint16_t rawBus = readRegister(busReg);

    // Convert raw values
    int16_t shuntSigned = (int16_t)rawShunt;
    float shuntVoltage = (shuntSigned >> 3) * 0.00004f; // 40μV per LSB
    float busVoltage = (rawBus >> 3) * 0.008f; // 8mV per LSB
    float current = shuntVoltage / INA3221_SHUNT_RESISTANCE;

    DEBUG_PRINTF("Channel %d: Raw Shunt=0x%04X (%d), Raw Bus=0x%04X\n",
      channel, rawShunt, shuntSigned, rawBus);
    DEBUG_PRINTF("  Shunt: %.6fV, Bus: %.3fV, Current: %.6fA\n",
      shuntVoltage, busVoltage, current);
  }

  // Test specific channels used in the application
  float batteryVoltage = readBusVoltage(INA3221_CHANNEL_BATTERY);
  float batteryCurrent = readCurrent(INA3221_CHANNEL_BATTERY);
  float chargerVoltage = readBusVoltage(INA3221_CHANNEL_CHARGER);
  float chargerCurrent = readCurrent(INA3221_CHANNEL_CHARGER);

  DEBUG_PRINTF("Application channels:\n");
  DEBUG_PRINTF("  Battery (Ch%d): %.3fV, %.6fA\n", INA3221_CHANNEL_BATTERY, batteryVoltage, batteryCurrent);
  DEBUG_PRINTF("  Charger (Ch%d): %.3fV, %.6fA\n", INA3221_CHANNEL_CHARGER, chargerVoltage, chargerCurrent);

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

  // Reset the INA3221 to default configuration
  DEBUG_PRINTLN("Resetting INA3221 to default configuration...");
  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(0x00); // Configuration register
  Wire.write(0x82); // Reset bit (bit 15) + default config high byte
  Wire.write(0x00); // Default config low byte
  if (Wire.endTransmission() != 0) {
    DEBUG_PRINTLN("ERROR: Failed to reset INA3221");
    return false;
  }
  delay(10); // Wait for reset to complete

  // Configure the INA3221 for continuous mode with all channels enabled
  DEBUG_PRINTLN("Configuring INA3221 for continuous measurement...");
  Wire.beginTransmission(INA3221_I2C_ADDRESS);
  Wire.write(0x00); // Configuration register
  Wire.write(0x72); // Enable all channels, continuous mode, 1024 samples averaging
  Wire.write(0x47); // Bus voltage conversion time = 4.156ms, Shunt voltage conversion time = 4.156ms
  if (Wire.endTransmission() != 0) {
    DEBUG_PRINTLN("ERROR: Failed to configure INA3221");
    return false;
  }

  // Verify configuration was written correctly
  uint16_t config = readRegister(0x00);
  DEBUG_PRINTF("INA3221 Configuration register: 0x%04X\n", config);

  // Check if channels are enabled
  bool ch1_enabled = (config & 0x4000) != 0;
  bool ch2_enabled = (config & 0x2000) != 0;
  bool ch3_enabled = (config & 0x1000) != 0;
  DEBUG_PRINTF("Channel 1 enabled: %s\n", ch1_enabled ? "YES" : "NO");
  DEBUG_PRINTF("Channel 2 enabled: %s\n", ch2_enabled ? "YES" : "NO");
  DEBUG_PRINTF("Channel 3 enabled: %s\n", ch3_enabled ? "YES" : "NO");

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

uint32_t PowerManager::calculateEstimatedTimeToFullyCharge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage) {
  DEBUG_VERBOSE_PRINTF("Charge Time Calculation - Charger: %.6fA/%.3fV, Battery: %.6fA/%.3fV, Percentage: %.1f%%\n",
    chargerCurrent, chargerVoltage, batteryCurrent, batteryVoltage, percentage);

  if (chargerVoltage < MIN_BATTERY_CHARGING_VOLTAGE) {
    DEBUG_VERBOSE_PRINTF("Charge Time: 0 (charger voltage too low: %.3fV < %.3fV)\n",
      chargerVoltage, MIN_BATTERY_CHARGING_VOLTAGE);
    return 0;
  }

  if (chargerCurrent <= 0.01f) {
    DEBUG_VERBOSE_PRINTF("Charge Time: 0 (no charger current: %.6fA)\n", chargerCurrent);
    return 0;
  }

  if (percentage >= 99.0f) {
    DEBUG_VERBOSE_PRINTF("Charge Time: 0 (already fully charged: %.1f%%)\n", percentage);
    return 0;
  }

  float effectiveChargingCurrent = 0.0f;

  if (batteryCurrent >= 0.0f) {
    effectiveChargingCurrent = batteryCurrent;
    DEBUG_VERBOSE_PRINTF("Normal charging - Battery receiving: %.6fA\n", effectiveChargingCurrent);
  }
  else {
    effectiveChargingCurrent = chargerCurrent + batteryCurrent;
    DEBUG_VERBOSE_PRINTF("Load exceeds charger - Net charging: %.6fA (Charger: %.6fA - Load: %.6fA)\n",
      effectiveChargingCurrent, chargerCurrent, -batteryCurrent);

    if (effectiveChargingCurrent <= 0.01f) {
      DEBUG_VERBOSE_PRINTF("Charge Time: 0 (net charging too low: %.6fA)\n", effectiveChargingCurrent);
      return 0;
    }
  }

  float chargeRatePerHour = (effectiveChargingCurrent * 1000.0f * 100.0f) / BATTERY_CAPACITY_MAH;
  if (chargeRatePerHour <= 0.0f) {
    DEBUG_VERBOSE_PRINTF("Charge Time: 0 (charge rate invalid: %.6f%%/h)\n", chargeRatePerHour);
    return 0;
  }

  float remainingPercentage = 100.0f - percentage;
  float hoursToCharge = remainingPercentage / chargeRatePerHour;
  uint32_t secondsToCharge = (uint32_t)(hoursToCharge * 3600.0f);

  DEBUG_VERBOSE_PRINTF("Charge Time Calculation - Effective Current: %.6fA, Rate: %.3f%%/h, Hours: %.3f, ETA: %us\n",
    effectiveChargingCurrent, chargeRatePerHour, hoursToCharge, secondsToCharge);

  return secondsToCharge;
}

uint32_t PowerManager::calculateEstimatedTimeToFullyDischarge(float chargerCurrent, float chargerVoltage, float batteryCurrent, float batteryVoltage, float percentage) {
  if (batteryCurrent >= 0.0f) {
    DEBUG_VERBOSE_PRINTF("Discharge Time: 0 (battery not discharging: %.6fA)\n", batteryCurrent);
    return 0;
  }

  if (percentage <= 1.0f) {
    DEBUG_VERBOSE_PRINTF("Discharge Time: 0 (battery too low: %.1f%%)\n", percentage);
    return 0;
  }

  float dischargeCurrent = -batteryCurrent;
  float dischargeRatePerHour = (dischargeCurrent * 1000.0f * 100.0f) / BATTERY_CAPACITY_MAH;

  float remainingPercentage = percentage - 1.0f;
  float hoursToDischarge = remainingPercentage / dischargeRatePerHour;
  uint32_t secondsToDischarge = (uint32_t)(hoursToDischarge * 3600.0f);

  DEBUG_VERBOSE_PRINTF("Discharge Time Calculation - Current: %.6fA, Rate: %.3f%%/h, Hours: %.3f, ETA: %us\n",
    dischargeCurrent, dischargeRatePerHour, hoursToDischarge, secondsToDischarge);

  return secondsToDischarge;
}
