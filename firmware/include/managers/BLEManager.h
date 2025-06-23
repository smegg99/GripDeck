// include/managers/BLEManager.h
#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <cstdint>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "../config/Config.h"
#include <utils/DebugSerial.h>

// CMD:DATA|DATA... format
enum BLECommand {
  BLE_CMD_STATUS,                   // STATUS -> BATTERY_VOLTAGE|BATTERY_CURRENT|CHARGER_VOLTAGE|CHARGER_CURRENT|BATTERY_PERCENTAGE
  BLE_CMD_POWER_ON,                 // POWER_ON -> WAS_SUCCESSFUL
  BLE_CMD_POWER_OFF,                // POWER_OFF -> WAS_SUCCESSFUL
  BLE_CMD_SHUTDOWN,                 // SHUTDOWN -> WAS_SUCCESSFUL

  BLE_CMD_HID_KEYBOARD_PRESS,       // HID_KEYBOARD_PRESS:KEY -> WAS_SUCCESSFUL
  BLE_CMD_HID_KEYBOARD_HOLD,        // HID_KEYBOARD_HOLD:KEY -> WAS_SUCCESSFUL
  BLE_CMD_HID_KEYBOARD_RELEASE,     // HID_KEYBOARD_RELEASE:KEY -> WAS_SUCCESSFUL
  BLE_CMD_HID_KEYBOARD_TYPE,        // HID_KEYBOARD_TYPE:TEXT -> WAS_SUCCESSFUL

  BLE_CMD_HID_MOUSE_MOVE,           // HID_MOUSE_MOVE:X:Y -> WAS_SUCCESSFUL
  BLE_CMD_HID_MOUSE_PRESS,          // HID_MOUSE_PRESS:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_MOUSE_HOLD,           // HID_MOUSE_HOLD:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_MOUSE_RELEASE,        // HID_MOUSE_RELEASE:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_MOUSE_SCROLL,         // HID_MOUSE_SCROLL:X:Y -> WAS_SUCCESSFUL

  BLE_CMD_HID_GAMEPAD_PRESS,        // HID_GAMEPAD_PRESS:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_GAMEPAD_HOLD,         // HID_GAMEPAD_HOLD:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_GAMEPAD_RELEASE,      // HID_GAMEPAD_RELEASE:BUTTON -> WAS_SUCCESSFUL
  BLE_CMD_HID_GAMEPAD_RIGHT_AXIS,   // HID_GAMEPAD_RIGHT_AXIS:AXIS:VALUE -> WAS_SUCCESSFUL
  BLE_CMD_HID_GAMEPAD_LEFT_AXIS,    // HID_GAMEPAD_LEFT_AXIS:AXIS:VALUE -> WAS_SUCCESSFUL

  BLE_CMD_HID_SYSTEM_POWER,         // HID_SYSTEM_POWER -> WAS_SUCCESSFUL
  BLE_CMD_GET_SYSTEM_INFO,          // GET_SYSTEM_INFO -> SYSTEM_INFO|WIFI_MAC|BLUETOOTH_MAC|FIRMWARE_VERSION|UPTIME
  BLE_CMD_HELP,                     // HELP -> COMMAND_LIST
  BLE_CMD_SYNTAX_ERROR,             // Syntax error in command
  BLE_CMD_UNKNOWN                   // Unknown command
};

static const struct {
  const char* name;
  BLECommand command;
} commandMap[] = {
  {"STATUS", BLE_CMD_STATUS},
  {"POWER_ON", BLE_CMD_POWER_ON},
  {"POWER_OFF", BLE_CMD_POWER_OFF},
  {"SHUTDOWN", BLE_CMD_SHUTDOWN},
  {"HID_KEYBOARD_PRESS", BLE_CMD_HID_KEYBOARD_PRESS},
  {"HID_KEYBOARD_HOLD", BLE_CMD_HID_KEYBOARD_HOLD},
  {"HID_KEYBOARD_RELEASE", BLE_CMD_HID_KEYBOARD_RELEASE},
  {"HID_KEYBOARD_TYPE", BLE_CMD_HID_KEYBOARD_TYPE},
  {"HID_MOUSE_MOVE", BLE_CMD_HID_MOUSE_MOVE},
  {"HID_MOUSE_PRESS", BLE_CMD_HID_MOUSE_PRESS},
  {"HID_MOUSE_HOLD", BLE_CMD_HID_MOUSE_HOLD},
  {"HID_MOUSE_RELEASE", BLE_CMD_HID_MOUSE_RELEASE},
  {"HID_MOUSE_SCROLL", BLE_CMD_HID_MOUSE_SCROLL},
  {"HID_GAMEPAD_PRESS", BLE_CMD_HID_GAMEPAD_PRESS},
  {"HID_GAMEPAD_HOLD", BLE_CMD_HID_GAMEPAD_HOLD},
  {"HID_GAMEPAD_RELEASE", BLE_CMD_HID_GAMEPAD_RELEASE},
  {"HID_GAMEPAD_RIGHT_AXIS", BLE_CMD_HID_GAMEPAD_RIGHT_AXIS},
  {"HID_GAMEPAD_LEFT_AXIS", BLE_CMD_HID_GAMEPAD_LEFT_AXIS},
  {"HID_SYSTEM_POWER", BLE_CMD_HID_SYSTEM_POWER},
  {"GET_SYSTEM_INFO", BLE_CMD_GET_SYSTEM_INFO},
  {"HELP", BLE_CMD_HELP}
};

static const char* BLE_HELP_STRING =
"Available Commands:\n"
"\n"
"=== System Commands ===\n"
"STATUS - Get battery/power status\n"
"POWER_ON - Turn on SBC power\n"
"POWER_OFF - Turn off SBC power\n"
"SHUTDOWN - Shutdown system\n"
"GET_SYSTEM_INFO - Get system information\n"
"\n"
"=== HID Keyboard Commands ===\n"
"HID_KEYBOARD_PRESS:KEY - Press and release key (ASCII code)\n"
"HID_KEYBOARD_HOLD:KEY - Hold key down (ASCII code)\n"
"HID_KEYBOARD_RELEASE:KEY - Release held key (ASCII code)\n"
"HID_KEYBOARD_TYPE:TEXT - Type text string\n"
"\n"
"=== HID Mouse Commands ===\n"
"HID_MOUSE_MOVE:X:Y - Move mouse by X,Y pixels\n"
"HID_MOUSE_PRESS:BTN - Press and release mouse button\n"
"HID_MOUSE_HOLD:BTN - Hold mouse button down\n"
"HID_MOUSE_RELEASE:BTN - Release held mouse button\n"
"HID_MOUSE_SCROLL:X:Y - Scroll mouse wheel X,Y units\n"
"\n"
"=== HID Gamepad Commands ===\n"
"HID_GAMEPAD_PRESS:BTN - Press and release gamepad button\n"
"HID_GAMEPAD_HOLD:BTN - Hold gamepad button down\n"
"HID_GAMEPAD_RELEASE:BTN - Release held gamepad button\n"
"HID_GAMEPAD_RIGHT_AXIS:AXIS:VALUE - Set right stick axis\n"
"HID_GAMEPAD_LEFT_AXIS:AXIS:VALUE - Set left stick axis\n"
"\n"
"=== HID System Commands ===\n"
"HID_SYSTEM_POWER - Send system power key\n"
"\n"
"=== Help ===\n"
"HELP - Show this command list\n"
"\n"
"Format: CMD:DATA|DATA... (use : for command data, | for separators)";

static const char* BLE_CMD_UNKNOWN_STRING =
"Unknown command, type 'HELP' for a list of available commands.";

static const char* BLE_CONNECTED_STRING = "Device connected";

struct BLEMessage {
  BLECommand command;
  char rawData[128];
  char parsedData[8][32];
  uint8_t dataCount;
  uint32_t timestamp;
};

class BLEManager {
private:
  BLEServer* pServer;
  BLEService* pService;
  BLECharacteristic* pTxCharacteristic;
  BLECharacteristic* pRxCharacteristic;

  QueueHandle_t commandQueue;
  SemaphoreHandle_t bleMutex;

  bool deviceConnected;
  bool oldDeviceConnected;

  void processCommands();
  void handleCommand(const BLEMessage& message);
  void parseCommand(const char* data, BLEMessage& message);
  bool parseDataComponents(const char* data, BLEMessage& message);

  class ServerCallbacks : public BLEServerCallbacks {
  public:
    ServerCallbacks(BLEManager* manager) : manager(manager) {}
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;
  private:
    BLEManager* manager;
  };

  class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  public:
    CharacteristicCallbacks(BLEManager* manager) : manager(manager) {}
    void onWrite(BLECharacteristic* characteristic) override;
  private:
    BLEManager* manager;
  };

  ServerCallbacks* serverCallbacks;
  CharacteristicCallbacks* rxCallbacks;
public:
  BLEManager();
  ~BLEManager();

  bool begin();
  void update();

  bool sendResponse(const char* response);

  bool isConnected() const { return deviceConnected; }
  void disconnect();
};

#endif // BLE_MANAGER_H