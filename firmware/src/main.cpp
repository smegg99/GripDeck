// src/main.cpp
#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
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

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    DEBUG_PRINTLN("Reconfiguring wake-up pins from RTC domain to regular GPIO");
    rtc_gpio_deinit((gpio_num_t)PIN_POWER_BUTTON);
    rtc_gpio_deinit((gpio_num_t)PIN_POWER_INPUT_DETECT);
  }

  pinMode(PIN_POWER_BUTTON, INPUT);
  pinMode(PIN_POWER_INPUT_DETECT, INPUT);

  digitalWrite(PIN_SBC_POWER_MOSFET, LOW);
  digitalWrite(PIN_LED_POWER_MOSFET, LOW);
  DEBUG_PRINTLN("GPIO pins configured");

  ledcSetup(LED_PWM_CHANNEL, LED_PWM_FREQUENCY, LED_PWM_RESOLUTION);
  ledcAttachPin(PIN_LED_POWER_MOSFET, LED_PWM_CHANNEL);
  DEBUG_PRINTLN("LED PWM configured");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);
  DEBUG_PRINTLN("I2C initialized");
}

void setup() {
  Serial.begin(115200);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
#if ARDUINO_USB_CDC_ON_BOOT
    uint32_t serialTimeout = millis() + 3000;
    while (!Serial && millis() < serialTimeout) {
      delay(10);
    }
#else
    delay(100);
#endif
  }

  DebugSerial::begin();

  DEBUG_PRINTLN("\n=== GripDeck SBC Controller Starting ===");
  DEBUG_PRINTF("Reset reason: %d\n", esp_reset_reason());

  DEBUG_PRINTLN("USB port reserved for HID, debug via external UART");

  esp_task_wdt_init(TASK_WATCHDOG_TIMEOUT, true);

  DEBUG_PRINTLN("Configuring deep sleep wake-up sources...");

  esp_err_t wakeup_result = esp_sleep_enable_ext1_wakeup(WAKE_UP_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  if (wakeup_result == ESP_OK) {
    DEBUG_PRINTLN("EXT1 wake-up configuration successful");
  }
  else {
    DEBUG_PRINTF("ERROR: EXT1 wake-up configuration failed with error: %d\n", wakeup_result);
  }

  DEBUG_PRINTF("Wake-up pin mask: 0x%llX (PIN_POWER_BUTTON=%d, PIN_POWER_INPUT_DETECT=%d)\n",
    WAKE_UP_PIN_MASK, PIN_POWER_BUTTON, PIN_POWER_INPUT_DETECT);
  DEBUG_PRINTLN("Deep sleep wake-up sources configured");

  initializeHardware();
  delay(100);

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT1: {
    DEBUG_PRINTLN("=== WAKE UP FROM DEEP SLEEP (EXT1) ===");
    DEBUG_PRINTLN("Wake-up triggered by EXT1 external pin");

    uint64_t wake_pin_mask = esp_sleep_get_ext1_wakeup_status();
    DEBUG_PRINTF("EXT1 wake pin mask: 0x%llX\n", wake_pin_mask);

    if (wake_pin_mask & (1ULL << PIN_POWER_BUTTON)) {
      DEBUG_PRINTLN("Wake-up caused by POWER BUTTON press");
      wokeUpFromPowerButton = true;
    }
    if (wake_pin_mask & (1ULL << PIN_POWER_INPUT_DETECT)) {
      DEBUG_PRINTLN("Wake-up caused by POWER INPUT detection");
    }
    break;
  }
  case ESP_SLEEP_WAKEUP_GPIO: {
    DEBUG_PRINTLN("=== WAKE UP FROM DEEP SLEEP (GPIO) ===");
    DEBUG_PRINTLN("Wake-up triggered by GPIO");

    if (digitalRead(PIN_POWER_BUTTON) == LOW) {
      DEBUG_PRINTLN("Power button is currently pressed");
      wokeUpFromPowerButton = true;
    }
    if (digitalRead(PIN_POWER_INPUT_DETECT) == LOW) {
      DEBUG_PRINTLN("Power input is currently detected");
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

  for (;;) {
    powerManager->update();

    esp_task_wdt_reset();
    delay(TASK_INTERVAL_POWER);
  }
}

void usbManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  for (;;) {
    usbManager->update();

    esp_task_wdt_reset();
    delay(TASK_INTERVAL_USB);
  }
}

void bleManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  for (;;) {
    bleManager->update();

    esp_task_wdt_reset();
    delay(TASK_INTERVAL_BLE);
  }
}

void systemManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  for (;;) {
    systemManager->update();

    esp_task_wdt_reset();
    delay(TASK_INTERVAL_SYSTEM);
  }
}

void statusManagerTask(void* arg) {
  esp_task_wdt_add(NULL);

  for (;;) {
    statusManager->update();

    esp_task_wdt_reset();
    delay(TASK_INTERVAL_STATUS);
  }
}

void loop() {}