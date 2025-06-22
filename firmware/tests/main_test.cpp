// src/main_test.cpp
// Continuous hardware test for basic functionality validation

#include <Arduino.h>
#include <Wire.h>
#include "config/Config.h"

// INA3221 test constants
#define INA3221_I2C_ADDRESS     0x40
#define INA3221_CHANNEL_BATTERY 3   // Channel 1: LiPo battery voltage/current
#define INA3221_CHANNEL_CHARGER 1 

// Test states
enum TestState {
  TEST_SBC_POWER,
  TEST_LED_ON,
  TEST_LED_FADE,
  TEST_INPUT_PINS,
  TEST_I2C_SCAN,
  TEST_INA3221,
  TEST_IDLE,
  TEST_COUNT
};

// Test parameters
const unsigned long TEST_DURATION = 3000;    // 3 seconds per test
const unsigned long FADE_STEP_TIME = 20;     // PWM fade speed
const int PWM_CHANNEL = 0;                   // PWM channel for LED
const int PWM_RESOLUTION = 8;                // 8-bit resolution (0-255)
const int PWM_FREQUENCY = 5000;              // 5 KHz PWM frequency

// Global variables
TestState currentTest = TEST_SBC_POWER;
unsigned long testStartTime = 0;
unsigned long lastUpdateTime = 0;
bool testInitialized = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n=== GripDeck Continuous Hardware Test ===");
  Serial.println("Tests will run in sequence, one by one");

  // Initialize pins
  pinMode(PIN_SBC_POWER_MOSFET, OUTPUT);
  pinMode(PIN_LED_POWER_MOSFET, OUTPUT);
  pinMode(PIN_POWER_BUTTON, INPUT_PULLUP);
  pinMode(PIN_POWER_INPUT_DETECT, INPUT_PULLUP);

  // Setup PWM for LED
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(PIN_LED_POWER_MOSFET, PWM_CHANNEL);

  // Initialize I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  testStartTime = millis();
}

void runSbcPowerTest() {
  if (!testInitialized) {
    Serial.println("\n--- Testing SBC Power MOSFET ---");
    Serial.printf("Pin %d will toggle HIGH/LOW every second\n", PIN_SBC_POWER_MOSFET);
    testInitialized = true;
  }

  if (millis() - lastUpdateTime >= 1000) {
    digitalWrite(PIN_SBC_POWER_MOSFET, !digitalRead(PIN_SBC_POWER_MOSFET));
    Serial.printf("SBC Power: %s\n", digitalRead(PIN_SBC_POWER_MOSFET) ? "ON" : "OFF");
    lastUpdateTime = millis();
  }
}

void runLedOnTest() {
  if (!testInitialized) {
    Serial.println("\n--- Testing LED Power MOSFET (ON/OFF) ---");
    Serial.printf("Pin %d will toggle HIGH/LOW every 500ms\n", PIN_LED_POWER_MOSFET);
    ledcWrite(PWM_CHANNEL, 255);  // Full brightness
    testInitialized = true;
    lastUpdateTime = millis();
  }

  if (millis() - lastUpdateTime >= 500) {
    static bool ledState = true;
    ledState = !ledState;
    ledcWrite(PWM_CHANNEL, ledState ? 255 : 0);
    Serial.printf("LED: %s\n", ledState ? "ON" : "OFF");
    lastUpdateTime = millis();
  }
}

void runLedFadeTest() {
  static int fadeValue = 0;
  static bool fadeDirection = true;  // true = up, false = down

  if (!testInitialized) {
    Serial.println("\n--- Testing LED Power MOSFET (PWM Fade) ---");
    Serial.printf("Pin %d will fade up and down\n", PIN_LED_POWER_MOSFET);
    fadeValue = 0;
    fadeDirection = true;
    testInitialized = true;
    lastUpdateTime = millis();
  }

  if (millis() - lastUpdateTime >= FADE_STEP_TIME) {
    // Change fade direction if needed
    if (fadeValue >= 255) {
      fadeDirection = false;
    }
    else if (fadeValue <= 0) {
      fadeDirection = true;
    }

    // Adjust fade value
    fadeValue += (fadeDirection ? 1 : -1);
    fadeValue = constrain(fadeValue, 0, 255);

    // Apply PWM
    ledcWrite(PWM_CHANNEL, fadeValue);

    // Print status occasionally
    if (fadeValue % 25 == 0) {
      Serial.printf("LED fade: %d/255\n", fadeValue);
    }

    lastUpdateTime = millis();
  }
}

void runInputPinsTest() {
  if (!testInitialized) {
    Serial.println("\n--- Testing Input Pins ---");
    Serial.printf("Reading Power Button (Pin %d) and Power Input Detect (Pin %d)\n",
      PIN_POWER_BUTTON, PIN_POWER_INPUT_DETECT);
    testInitialized = true;
  }

  if (millis() - lastUpdateTime >= 500) {
    Serial.printf("Power Button: %s, Power Input: %s\n",
      digitalRead(PIN_POWER_BUTTON) ? "UP" : "DOWN",
      digitalRead(PIN_POWER_INPUT_DETECT) ? "NO" : "YES");
    lastUpdateTime = millis();
  }
}

void runI2CScanTest() {
  if (!testInitialized) {
    Serial.println("\n--- Testing I2C Bus ---");
    Serial.printf("Scanning I2C bus (SDA=%d, SCL=%d)...\n", PIN_I2C_SDA, PIN_I2C_SCL);
    testInitialized = true;

    // Scan for I2C devices
    int device_count = 0;
    for (int address = 1; address < 127; address++) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) {
        Serial.printf("I2C device found at address 0x%02X\n", address);
        device_count++;
      }
    }
    Serial.printf("Found %d I2C devices\n", device_count);
    lastUpdateTime = millis();
  }

  // Only scan once per test period
}

void runINA3221Test() {
  if (!testInitialized) {
    Serial.println("\n--- Testing INA3221 Current/Voltage Monitor ---");
    Serial.printf("Testing INA3221 at address 0x%02X\n", INA3221_I2C_ADDRESS);
    Serial.printf("Battery channel: %d, Charger channel: %d\n",
      INA3221_CHANNEL_BATTERY, INA3221_CHANNEL_CHARGER);
    testInitialized = true;

    // Test INA3221 presence
    Wire.beginTransmission(INA3221_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) {
      Serial.println("INA3221 device detected!");

      // Try to read manufacturer ID (register 0xFE)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0xFE);  // Manufacturer ID register
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          uint16_t manufacturerID = (Wire.read() << 8) | Wire.read();
          Serial.printf("Manufacturer ID: 0x%04X (Expected: 0x5449 for TI)\n", manufacturerID);
        }
      }

      // Try to read die ID (register 0xFF)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0xFF);  // Die ID register
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          uint16_t dieID = (Wire.read() << 8) | Wire.read();
          Serial.printf("Die ID: 0x%04X (Expected: 0x3220 for INA3221)\n", dieID);
        }
      }
    }
    else {
      Serial.println("INA3221 device NOT detected!");
    }
    lastUpdateTime = millis();
  }

  // Read voltage and current values periodically
  if (millis() - lastUpdateTime >= 1000) {
    Wire.beginTransmission(INA3221_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) {
      // === BATTERY CHANNEL (Channel 3) ===
      // Read bus voltage for battery channel (channel 3 = register 0x06)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0x06);  // Bus voltage register for channel 3
      float batteryVoltage = 0.0;
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          uint16_t rawVoltage = (Wire.read() << 8) | Wire.read();
          // INA3221 bus voltage is 8mV per LSB, right-shifted by 3 bits
          batteryVoltage = (rawVoltage >> 3) * 0.008;
        }
      }

      // Read shunt voltage for battery channel (channel 3 = register 0x05)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0x05);  // Shunt voltage register for channel 3
      float batteryCurrent = 0.0;
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          int16_t rawShunt = (Wire.read() << 8) | Wire.read();
          // INA3221 shunt voltage is 40µV per LSB, right-shifted by 3 bits
          float shuntVoltage = (rawShunt >> 3) * 0.00004; // 40µV per LSB
          // Current = Shunt Voltage / Shunt Resistance (assuming 0.1Ω shunt)
          batteryCurrent = shuntVoltage / 0.1; // Convert to Amps
        }
      }

      Serial.printf("Battery (Ch %d): %.3fV, %.3fA (%.1fmA)\n",
        INA3221_CHANNEL_BATTERY, batteryVoltage, batteryCurrent, batteryCurrent * 1000);

      // === CHARGER CHANNEL (Channel 1) ===
      // Read bus voltage for charger channel (channel 1 = register 0x02)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0x02);  // Bus voltage register for channel 1
      float chargerVoltage = 0.0;
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          uint16_t rawVoltage = (Wire.read() << 8) | Wire.read();
          chargerVoltage = (rawVoltage >> 3) * 0.008;
        }
      }

      // Read shunt voltage for charger channel (channel 1 = register 0x01)
      Wire.beginTransmission(INA3221_I2C_ADDRESS);
      Wire.write(0x01);  // Shunt voltage register for channel 1
      float chargerCurrent = 0.0;
      if (Wire.endTransmission() == 0) {
        Wire.requestFrom(INA3221_I2C_ADDRESS, 2);
        if (Wire.available() >= 2) {
          int16_t rawShunt = (Wire.read() << 8) | Wire.read();
          // INA3221 shunt voltage is 40µV per LSB, right-shifted by 3 bits
          float shuntVoltage = (rawShunt >> 3) * 0.00004; // 40µV per LSB
          // Current = Shunt Voltage / Shunt Resistance (assuming 0.1Ω shunt)
          chargerCurrent = shuntVoltage / 0.1; // Convert to Amps
        }
      }

      Serial.printf("Charger (Ch %d): %.3fV, %.3fA (%.1fmA)\n",
        INA3221_CHANNEL_CHARGER, chargerVoltage, chargerCurrent, chargerCurrent * 1000);
    }
    lastUpdateTime = millis();
  }
}

void runIdleTest() {
  if (!testInitialized) {
    Serial.println("\n--- Idle Period (All Off) ---");
    digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
    ledcWrite(PWM_CHANNEL, 0);
    testInitialized = true;
  }

  if (millis() - lastUpdateTime >= 1000) {
    Serial.printf("Button: %s, Power: %s, Heap: %u\n",
      digitalRead(PIN_POWER_BUTTON) ? "UP" : "DOWN",
      digitalRead(PIN_POWER_INPUT_DETECT) ? "NO" : "YES",
      ESP.getFreeHeap());
    lastUpdateTime = millis();
  }
}

void loop() {
  // Check if it's time to move to next test
  if (millis() - testStartTime >= TEST_DURATION) {
    currentTest = static_cast<TestState>((currentTest + 1) % TEST_COUNT);
    testStartTime = millis();
    testInitialized = false;
  }

  // Run the current test
  switch (currentTest) {
  case TEST_SBC_POWER:
    runSbcPowerTest();
    break;
  case TEST_LED_ON:
    runLedOnTest();
    break;
  case TEST_LED_FADE:
    runLedFadeTest();
    break;
  case TEST_INPUT_PINS:
    runInputPinsTest();
    break;
  case TEST_I2C_SCAN:
    runI2CScanTest();
    break;
  case TEST_INA3221:
    runINA3221Test();
    break;
  case TEST_IDLE:
    runIdleTest();
    break;
  }

  delay(10);  // Small delay to avoid excessive looping
}
