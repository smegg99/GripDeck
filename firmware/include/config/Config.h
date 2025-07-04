// include/config/Config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>

// ====================================================================
// FIRMWARE VERSION
// ====================================================================
#define FIRMWARE_VERSION 0x0100

// ====================================================================
// HARDWARE PIN DEFINITIONS
// ====================================================================
#define PIN_SBC_POWER_MOSFET    6   // GP6 - SBC power mosfet (5V relay control)
#define PIN_LED_POWER_MOSFET    7   // GP7 - LEDs power mosfet (brightness control)
#define PIN_I2C_SDA             8   // GP8 - I2C SDA
#define PIN_I2C_SCL             9   // GP9 - I2C SCL
#define PIN_POWER_BUTTON        11  // GP11 - Power button with pull-up, active low, wake up source
#define PIN_POWER_INPUT_DETECT  12  // GP12 - Power input detection with pull-up, active low, wake up source

// Debug UART Pins (for external UART-to-USB converter)
#define PIN_DEBUG_UART_TX       1   // GP1 - Debug UART TX (to external UART converter RX)
#define PIN_DEBUG_UART_RX       2   // GP2 - Debug UART RX (from external UART converter TX)

// ====================================================================
// INA3221 POWER MONITORING CONFIGURATION
// ====================================================================
#define INA3221_I2C_ADDRESS                 0x40
#define INA3221_CHANNEL_CHARGER             1   // Channel 1: 5V input voltage/current
#define INA3221_CHANNEL_BATTERY             2   // Channel 2: LiPo battery voltage/current
#define INA3221_SHUNT_RESISTANCE            0.1 // Shunt resistance in MOhms, make sure to match your hardware

// INA3221 Register Addresses
#define INA3221_CHANNEL_1_SHUNT_REGISTER    0x01
#define INA3221_CHANNEL_2_SHUNT_REGISTER    0x03
#define INA3221_CHANNEL_3_SHUNT_REGISTER    0x05
#define INA3221_CHANNEL_1_BUS_REGISTER      0x02
#define INA3221_CHANNEL_2_BUS_REGISTER      0x04
#define INA3221_CHANNEL_3_BUS_REGISTER      0x06

// ====================================================================
// POWER MANAGEMENT CONFIGURATION
// ====================================================================
#define BATTERY_MIN_PERCENTAGE              5        // Minimum battery percentage to allow SBC startup
#define BATTERY_CAPACITY_MAH                4500     // Battery capacity in mAh
#define BATTERY_TECH_LIPO // Uncomment this line if using LiPo battery technology
#define BATTERY_TECH_LI_ION 
#define BATTERY_SAVING_MODE                 15       // Battery percentage for low power mode
#define MIN_BATTERY_CHARGING_VOLTAGE        4.0      // Minimum voltage to consider charging, I have perhaps lowered it too much but thats how much my front panel USB port provides kek

// ====================================================================
// POWER BUTTON CONFIGURATION
// ====================================================================
#define POWER_BUTTON_DEBOUNCE               50      // Power button debounce time (ms)
#define POWER_BUTTON_SHORT_PRESS_MIN        50      // Minimum time for valid button press (ms)
#define POWER_BUTTON_SHORT_PRESS_MAX        2000    // Maximum time for soft shutdown (ms)
#define POWER_BUTTON_LONG_PRESS_MIN         3000    // Minimum time for hard shutdown (ms)

// ====================================================================
// LED CONFIGURATION
// ====================================================================
#define LED_PWM_FREQUENCY                   1000    // PWM frequency for LED control
#define LED_PWM_RESOLUTION                  8       // PWM resolution (8-bit = 0-255)
#define LED_PWM_CHANNEL                     0       // PWM channel for MOSFET control

// LED Status Brightness Levels
#define LED_BRIGHTNESS_MAX                  255     // Maximum brightness (normal mode)
#define LED_BRIGHTNESS_POWER_SAVE           64      // Reduced brightness (power save mode)
#define LED_BRIGHTNESS_OFF                  0       // LEDs off

// LED Blink Patterns (ms)
#define LED_BLINK_FAST                      200     // Fast blink for connections
#define LED_BLINK_SLOW                      1000    // Slow blink for status changes
#define LED_BLINK_DURATION                  3000    // How long to blink before returning to steady state
#define LED_PULSE_CYCLE                     2000    // Complete pulse cycle duration for charging effect

// ====================================================================
// USB HID CONFIGURATION
// ====================================================================
#define USB_MY_VID                          0x1209
#define USB_MY_PID                          0x2078
#define USB_MANUFACTURER                    "Smuggr"
#define USB_PRODUCT                         "GripDeck Controller"
#define USB_PRODUCT_VERSION                 0x0100
#define USB_SERIAL_NUMBER                   "GD001"
#define USB_CONNECTION_TIMEOUT              15000   // Max time without USB activity before SBC shutdown
#define DISABLE_USB_HID                     false   // Set to true to disable USB HID functionality

// USB HID Press Delays
#define USB_HID_KEYBOARD_PRESS_DELAY        50      // Delay after pressing a key before releasing it (ms)
#define USB_HID_MOUSE_PRESS_DELAY           50      // Delay after pressing a mouse button before releasing it (ms)
#define USB_HID_GAMEPAD_PRESS_DELAY         50      // Delay after pressing a gamepad button before releasing it (ms)

// ====================================================================
// USB VENDOR HID CONFIGURATION
// ====================================================================
#define VENDOR_REPORT_ID          6
#define VENDOR_REPORT_SIZE        32
#define PROTOCOL_VERSION          0x01
#define PROTOCOL_MAGIC            0x4744  // "GD" in ASCII

// ====================================================================
// BLE CONFIGURATION
// ====================================================================
#define BLE_DEVICE_NAME                     "GripDeck-Controller"
#define BLE_SERVICE_UUID                    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_TX_UUID          "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX (device to client)
#define BLE_CHARACTERISTIC_RX_UUID          "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // RX (client to device)

// BLE Command Separators
#define BLE_CMD_PART_SEPARATOR              ":"
#define BLE_CMD_DATA_SEPARATOR              "|"
#define BLE_CMD_WAS_SUCCESSFUL              "1"
#define BLE_CMD_WAS_FAILURE                 "0"

// ====================================================================
// FREERTOS TASK CONFIGURATION
// ====================================================================
// Stack Sizes
#define TASK_STACK_SIZE_SMALL               3072
#define TASK_STACK_SIZE_MEDIUM              6144
#define TASK_STACK_SIZE_LARGE               10240
#define TASK_STACK_SIZE_EXTRA_LARGE         16384

// Task Priorities (0 = lowest, 25 = highest, avoid priority 1 used by idle task)
#define TASK_PRIORITY_IDLE                  0
#define TASK_PRIORITY_LOW                   2
#define TASK_PRIORITY_NORMAL                5
#define TASK_PRIORITY_HIGH                  10
#define TASK_PRIORITY_CRITICAL              15

// Task Update Intervals (ms)
#define TASK_INTERVAL_POWER                 1500    // Power management task
#define TASK_INTERVAL_SYSTEM                1000     // System management task
#define TASK_INTERVAL_USB                   10     // USB HID task
#define TASK_INTERVAL_BLE                   10     // BLE task
#define TASK_INTERVAL_STATUS                5      // Status manager task

// Task Watchdog
#define TASK_WATCHDOG_TIMEOUT               30      // Seconds

// ====================================================================
// QUEUE CONFIGURATION
// ====================================================================
#define QUEUE_SIZE_COMMANDS                 10      // BLE command queue size

// ====================================================================
// DEEP SLEEP CONFIGURATION
// ====================================================================
#define DEEP_SLEEP_WATCHDOG_TIMEOUT_MS      30000   // Seconds of inactivity before deep sleep
#define DEEP_SLEEP_ACTIVITY_RESET_INTERVAL_MS 1000  // Check for activity every second
#define WAKE_UP_PIN_MASK                    ((1ULL << PIN_POWER_BUTTON) | (1ULL << PIN_POWER_INPUT_DETECT))

// ====================================================================
// DEBUG CONFIGURATION
// ====================================================================
#define DEBUG_ENABLED                       false
#define DEBUG_SERIAL_ENABLED                false     // Enable secondary serial for debug output
#define DEBUG_SERIAL_BAUD_RATE              115200    // Debug serial baud rate
#define DEBUG_VERBOSE_LOGGING               false     // Enable verbose logging for debugging

// ====================================================================
// BATTERY CONFIGURATION
// ====================================================================
#if defined(BATTERY_TECH_LIPO)

static constexpr float kMinVoltage = 3.0f;
static constexpr float kMaxVoltage = 4.2f;
static constexpr float kInternalR = 0.04f;

static constexpr float kVoltagePoints[] = {
    3.0f, 3.3f, 3.5f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.1f, 4.2f
};
static constexpr float kPercentagePoints[] = {
    0.0f, 5.0f,15.0f,25.0f,40.0f,60.0f,75.0f,85.0f,95.0f,100.0f
};

#elif defined(BATTERY_TECH_LI_ION)

static constexpr float kMinVoltage = 2.5f;
static constexpr float kMaxVoltage = 4.2f;
static constexpr float kInternalR = 0.08f;

static constexpr float kVoltagePoints[] = {
    2.5f, 2.9f, 3.2f, 3.4f, 3.6f, 3.7f, 3.8f, 3.9f, 4.0f, 4.2f
};
static constexpr float kPercentagePoints[] = {
    0.0f, 5.0f,15.0f,25.0f,40.0f,60.0f,75.0f,85.0f,95.0f,100.0f
};

#else
#  error "You must define either BATTERY_TECH_LIPO or BATTERY_TECH_LI_ION"
#endif

static constexpr int kNumPoints =
sizeof(kVoltagePoints) / sizeof(kVoltagePoints[0]);

#endif // CONFIG_H