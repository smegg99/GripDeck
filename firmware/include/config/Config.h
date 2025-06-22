// include/config/Config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>

// Hardware Pin Definitions
#define PIN_SBC_POWER_MOSFET    6   // GP6 - SBC power mosfet (5V relay control)
#define PIN_LED_POWER_MOSFET    7   // GP7 - LEDs power mosfet (brightness control)
#define PIN_I2C_SDA             8   // GP8 - I2C SDA
#define PIN_I2C_SCL             9   // GP9 - I2C SCL
#define PIN_POWER_BUTTON        11  // GP11 - Power button with pull-up, active low, wake up source
#define PIN_POWER_INPUT_DETECT  12  // GP12 - Power input detection with pull-up, active low, wake up source

// Debug UART Pins (for external UART-to-USB converter)
#define PIN_DEBUG_UART_TX       1  // GP1 - Debug UART TX (to external UART converter RX)
#define PIN_DEBUG_UART_RX       2  // GP2 - Debug UART RX (from external UART converter TX)

// INA3221 Configuration
#define INA3221_I2C_ADDRESS     0x40
#define INA3221_CHANNEL_BATTERY 3   // Channel 3: LiPo battery voltage/current
#define INA3221_CHANNEL_CHARGER 1   // Channel 1: 5V input voltage/current

#define INA3221_SHUNT_RESISTANCE 0.1 // Shunt resistance in MOhms, make sure to adjust this based on your hardware

#define INA3221_CHANNEL_1_SHUNT_REGISTER 0x01 // Channel 1 shunt voltage register
#define INA3221_CHANNEL_2_SHUNT_REGISTER 0x03 // Channel 2 shunt voltage register
#define INA3221_CHANNEL_3_SHUNT_REGISTER 0x05 // Channel 3 shunt voltage register

#define INA3221_CHANNEL_1_BUS_REGISTER   0x02 // Channel 1 bus voltage register
#define INA3221_CHANNEL_2_BUS_REGISTER   0x04 // Channel 2 bus voltage register  
#define INA3221_CHANNEL_3_BUS_REGISTER   0x06 // Channel 3 bus voltage register

// Power Management Configuration
#define BATTERY_MIN_PERCENTAGE  5       // Minimum battery percentage to allow SBC startup
#define BATTERY_CAPACITY_MAH    3000    // Battery capacity in mAh
#define BATTERY_SAVING_MODE     15      // Battery percentage for low power mode
#define MIN_BATTERY_CHARGING_VOLTAGE 4.6

// SBC Communication Configuration
#define SBC_PING_INTERVAL       5000    // SBC ping interval in ms
#define SBC_TIMEOUT_DURATION    30000   // SBC timeout in ms before shutdown
#define SBC_STARTUP_DELAY       2000    // Delay after SBC power on after pressing the power button in ms

// USB HID Configuration
#define USB_MY_VID              0x1209
#define USB_MY_PID              0x2077
#define USB_MANUFACTURER        "GripDeck"
#define USB_PRODUCT             "SBC Controller"
#define USB_PRODUCT_VERSION     0x0100
#define USB_SERIAL_NUMBER       "GD001"
#define USB_HID_TIMEOUT         15000   // USB HID timeout in ms - shutdown SBC if no activity (reduced for faster response)
#define USB_HID_PING_INTERVAL   5000    // USB HID ping interval in ms (reduced for faster detection)
#define USB_HID_KEYBOARD_PRESS_DELAY 50 // Delay after pressing a key before releasing it (ms)

// Power Button Configuration
#define POWER_BUTTON_SHORT_PRESS_MIN    50      // Minimum time for valid button press (ms)
#define POWER_BUTTON_SHORT_PRESS_MAX    2000    // Maximum time for soft shutdown (ms)
#define POWER_BUTTON_LONG_PRESS_MIN     3000    // Minimum time for hard shutdown (ms) - configurable
#define POWER_BUTTON_HARD_SHUTDOWN      5000    // Default hard shutdown time (ms)

// LED Configuration (mine work with voltages between 3V and 6V, so you might need to adjust the pwm min and max values)
#define LED_PWM_FREQUENCY       1000    // PWM frequency for LED control
#define LED_PWM_RESOLUTION      8       // PWM resolution (8-bit = 0-255)
#define LED_FADE_STEPS          100     // Number of steps in fade animation
#define LED_ANIMATION_SPEED     50      // Animation speed in ms per step

// Primary LED Configuration (MOSFET controlled single-color LEDs)
#define LED_MOSFET_ENABLED      true    // Enable MOSFET-controlled LEDs (default)
#define LED_PWM_CHANNEL         0       // PWM channel for MOSFET control

// FreeRTOS Task Configuration
#define TASK_STACK_SIZE_SMALL   3072    // Increased from 2048
#define TASK_STACK_SIZE_MEDIUM  6144    // Increased from 4096  
#define TASK_STACK_SIZE_LARGE   10240   // Increased from 8192

// Task Priorities (0 = lowest, 25 = highest, avoid priority 1 which is used by idle task)
#define TASK_PRIORITY_IDLE      0
#define TASK_PRIORITY_LOW       2
#define TASK_PRIORITY_NORMAL    5
#define TASK_PRIORITY_HIGH      10
#define TASK_PRIORITY_CRITICAL  15

// Task Update Intervals
#define TASK_INTERVAL_POWER     1000     // Power management task
#define TASK_INTERVAL_SYSTEM    100      // Communication tasks
#define TASK_INTERVAL_USB       100      // USB HID task

// Power Management Timeouts
#define SLEEP_TIMEOUT_MS        300000  // 5 minutes idle before sleep
#define POWER_BUTTON_DEBOUNCE   50      // Power button debounce
#define POWER_BUTTON_LONG_PRESS 5000    // Long press duration for hard shutdown
#define USB_CONNECTION_TIMEOUT  15000   // Max time without USB activity before SBC shutdown

// Queue Sizes
#define QUEUE_SIZE_COMMANDS     10      // BLE command queue size

// BLE Configuration
#define BLE_DEVICE_NAME         "GripDeck-Controller"
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX characteristic (device to client)
#define BLE_CHARACTERISTIC_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // RX characteristic (client to device)

// Wake-up pin configuration
#define WAKE_UP_PIN_MASK        (1ULL << PIN_POWER_BUTTON) | (1ULL << PIN_POWER_INPUT_DETECT)

// Debug Configuration
#define DEBUG_ENABLED           true
#define SERIAL_BAUD_RATE        115200
#define DEBUG_SERIAL_ENABLED    true    // Enable secondary serial for debug output
#define DEBUG_SERIAL_BAUD_RATE  115200  // Debug serial baud rate
#define DEBUG_VERBOSE_LOGGING   true    // Enable verbose logging for debugging

#endif // CONFIG_H