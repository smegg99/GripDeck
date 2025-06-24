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
#include "managers/StatusManager.h"

PowerManager* powerManager;
USBManager* usbManager;
BLEManager* bleManager;
SystemManager* systemManager;
StatusManager* statusManager;

TaskHandle_t powerTaskHandle = nullptr;

bool wokeUpFromPowerButton = false;

void powerManagerTask(void* arg);
void usbManagerTask(void* arg);
void bleManagerTask(void* arg);
void systemManagerTask(void* arg);
void statusManagerTask(void* arg);

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

  // Configure wake-up sources (using WAKE_UP_PIN_MASK from config)
  esp_sleep_enable_ext1_wakeup(WAKE_UP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  DEBUG_PRINTF("Wake-up sources configured with mask: 0x%llX\n", WAKE_UP_PIN_MASK);

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

  statusManager = new StatusManager();
  if (!statusManager->begin()) {
    DEBUG_PRINTLN("ERROR: StatusManager initialization failed");
    esp_restart();
    return;
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    systemManager->notifyWakeFromDeepSleep();
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

  result = xTaskCreatePinnedToCore(
    statusManagerTask,
    "StatusTask",
    TASK_STACK_SIZE_LARGE,
    nullptr,
    TASK_PRIORITY_NORMAL,
    nullptr,
    0
  );
  if (result != pdPASS) {
    DEBUG_PRINTF("ERROR: Failed to create StatusTask (error code %d)\n", result);
    esp_restart();
    return;
  }
  DEBUG_PRINTLN("StatusTask created successfully");
  DEBUG_PRINTLN("=== GripDeck SBC Controller Initialization Complete ===\n\n\n");

  if (wokeUpFromPowerButton) {
    DEBUG_PRINTLN("Power button pressed, turning SBC power ON");
    powerManager->trySetSBCPower(true);
  }
}

void powerManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  static uint32_t lastDebugTime = 0;
  static uint32_t lastHeapCheckTime = 0;
  const uint32_t debugInterval = 10000;
  const uint32_t heapCheckInterval = 5000; 

  for (;;) {
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

    if (currentTime - lastDebugTime >= debugInterval) {
      DEBUG_PRINTF("PowerTask: Running at %u ms (free stack: %u bytes)\n",
        currentTime, uxTaskGetStackHighWaterMark(NULL));
      lastDebugTime = currentTime;
    }

    powerManager->update();

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

void statusManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  static uint32_t lastDebugTime = 0;
  const uint32_t debugInterval = 15000; 

  for (;;) {
    uint32_t currentTime = millis();
    if (currentTime - lastDebugTime >= debugInterval) {
      DEBUG_PRINTF("StatusTask: Running at %u ms (free stack: %u bytes)\n",
        currentTime, uxTaskGetStackHighWaterMark(NULL));
      lastDebugTime = currentTime;
    }

    statusManager->update();

    esp_task_wdt_reset();

    delay(TASK_INTERVAL_STATUS);
  }
}

void loop() {}