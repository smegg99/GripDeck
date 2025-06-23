// src/main.cpp
#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include "utils/DebugSerial.h"
#include "config/Config.h"
#include "managers/PowerManager.h"
#include "managers/USBManager.h"
#include "managers/BLEManager.h"
#include "managers/SystemManager.h"

PowerManager* powerManager;
USBManager* usbManager;
BLEManager* bleManager;
SystemManager* systemManager;

TaskHandle_t powerTaskHandle = nullptr;

bool wokeUpFromPowerButton = false;

void powerManagerTask(void* arg);
void usbManagerTask(void* arg);
void bleManagerTask(void* arg);
void systemManagerTask(void* arg);

void initializeHardware() {
  DEBUG_PRINTLN("Initializing hardware...");

  pinMode(PIN_SBC_POWER_MOSFET, OUTPUT);
  pinMode(PIN_LED_POWER_MOSFET, OUTPUT);
  pinMode(PIN_POWER_BUTTON, INPUT_PULLUP);
  pinMode(PIN_POWER_INPUT_DETECT, INPUT_PULLUP);

  digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
  digitalWrite(PIN_LED_POWER_MOSFET, LOW);

  DEBUG_PRINTLN("GPIO pins configured");

  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
  ledcAttachPin(PIN_LED_POWER_MOSFET, LED_PWM_CHANNEL);
  DEBUG_PRINTLN("PWM configured");

  esp_sleep_enable_ext1_wakeup(WAKE_UP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  DEBUG_PRINTLN("Wake-up sources configured");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  DEBUG_PRINTLN("I2C initialized");
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  DebugSerial::begin();

  DEBUG_PRINTLN("\n=== GripDeck SBC Controller Starting ===");
  DEBUG_PRINTF("Reset reason: %d\n", esp_reset_reason());

  DEBUG_PRINTLN("USB port reserved for HID, debug via external UART");

  esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT, true);

  initializeHardware();
  delay(100);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT1: {
    DEBUG_PRINTLN("=== WAKE UP FROM DEEP SLEEP ===");
    DEBUG_PRINTLN("Wake-up triggered by external pin");

    uint64_t wake_pin_mask = esp_sleep_get_ext1_wakeup_status();
    DEBUG_PRINTF("Wake pin mask: 0x%llX\n", wake_pin_mask);

    if (wake_pin_mask & (1ULL << PIN_POWER_BUTTON)) {
      DEBUG_PRINTLN("Wake-up caused by POWER BUTTON press");
      wokeUpFromPowerButton = true;
    }
    if (wake_pin_mask & (1ULL << PIN_POWER_INPUT_DETECT)) {
      DEBUG_PRINTLN("Wake-up caused by POWER INPUT detection");
    }
    break;
  }
  case ESP_SLEEP_WAKEUP_UNDEFINED:
  default:
    DEBUG_PRINTLN("=== COLD BOOT / RESET ===");
    DEBUG_PRINTLN("Normal startup (not from sleep)");
    break;
  }

  powerManager = new PowerManager();
  if (!powerManager->begin()) {
    DEBUG_PRINTLN("ERROR: PowerManager initialization failed");
    esp_restart();
    return;
  }

  usbManager = new USBManager();
  if (!usbManager->begin()) {
    DEBUG_PRINTLN("ERROR: USBManager initialization failed");
    esp_restart();
    return;
  }
 
  bleManager = new BLEManager();
  if (!bleManager->begin()) {
    DEBUG_PRINTLN("ERROR: BLEManager initialization failed");
    esp_restart();
    return;
  }

  systemManager = new SystemManager();
  if (!systemManager->begin()) {
    DEBUG_PRINTLN("ERROR: SystemManager initialization failed");
    esp_restart();
    return;
  }

  DEBUG_PRINTLN("Creating FreeRTOS tasks...");
  BaseType_t result = xTaskCreatePinnedToCore(
    powerManagerTask,
    "PowerTask",
    TASK_STACK_SIZE_MEDIUM,
    nullptr,
    TASK_PRIORITY_NORMAL,
    &powerTaskHandle,
    0
  );
  if (result != pdPASS) {
    DEBUG_PRINTF("ERROR: Failed to create PowerTask (error code %d)\n", result);
    esp_restart();
    return;
  }
  DEBUG_PRINTLN("PowerTask created successfully");

  result = xTaskCreatePinnedToCore(
    usbManagerTask,
    "USBTask",
    TASK_STACK_SIZE_LARGE,
    nullptr,
    TASK_PRIORITY_NORMAL,
    nullptr,
    1
  );
  if (result != pdPASS) {
    DEBUG_PRINTF("ERROR: Failed to create USBTask (error code %d)\n", result);
    esp_restart();
    return;
  }
  DEBUG_PRINTLN("USBTask created successfully");

  result = xTaskCreatePinnedToCore(
    bleManagerTask,
    "BLETask",
    TASK_STACK_SIZE_EXTRA_LARGE,
    nullptr,
    TASK_PRIORITY_NORMAL,
    nullptr,
    1
  );
  if (result != pdPASS) {
    DEBUG_PRINTF("ERROR: Failed to create BLETask (error code %d)\n", result);
    esp_restart();
    return;
  }
  DEBUG_PRINTLN("BLETask created successfully");

  result = xTaskCreatePinnedToCore(
    systemManagerTask,
    "SystemTask",
    TASK_STACK_SIZE_MEDIUM,
    nullptr,
    TASK_PRIORITY_NORMAL,
    nullptr,
    1
  );
  if (result != pdPASS) {
    DEBUG_PRINTF("ERROR: Failed to create SystemTask (error code %d)\n", result);
    esp_restart();
    return;
  }
  DEBUG_PRINTLN("SystemTask created successfully");
  DEBUG_PRINTLN("=== GripDeck SBC Controller Initialization Complete ===\n\n\n");

  if (wokeUpFromPowerButton) {
    DEBUG_PRINTLN("Power button pressed, turning SBC power ON");
    powerManager->trySetSBCPower(true);
  }
}

void powerManagerTask(void* arg) {
  // Add task to watchdog
  esp_task_wdt_add(NULL);

  for (;;) {
    powerManager->update();

    // Feed the watchdog
    esp_task_wdt_reset();

    delay(TASK_INTERVAL_POWER);
  }
}

void usbManagerTask(void* arg) {
  // Add task to watchdog
  esp_task_wdt_add(NULL);

  for (;;) {
    usbManager->update();

    // Feed the watchdog
    esp_task_wdt_reset();

    delay(TASK_INTERVAL_USB);
  }
}

void bleManagerTask(void* arg) {
  // Add task to watchdog
  esp_task_wdt_add(NULL);

  for (;;) {
    bleManager->update();

    // Feed the watchdog
    esp_task_wdt_reset();

    delay(TASK_INTERVAL_BLE);
  }
}

void systemManagerTask(void* arg) {
  // Add task to watchdog
  esp_task_wdt_add(NULL);

  for (;;) {
    systemManager->update();

    // Feed the watchdog
    esp_task_wdt_reset();

    delay(TASK_INTERVAL_SYSTEM);
  }
}

void loop() {}