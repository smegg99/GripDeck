// src/managers/BLEManager.cpp

#include "managers/BLEManager.h"
#include "managers/StatusManager.h"
#include "config/Config.h"
#include "utils/DebugSerial.h"
#include "managers/USBManager.h"
#include "managers/PowerManager.h"
#include "managers/SystemManager.h"

extern USBManager* usbManager;
extern PowerManager* powerManager;
extern SystemManager* systemManager;
extern StatusManager* statusManager;

BLEManager::BLEManager() {}

BLEManager::~BLEManager() {
  if (commandQueue) {
    vQueueDelete(commandQueue);
  }
  if (bleMutex) {
    vSemaphoreDelete(bleMutex);
  }
  if (serverCallbacks) {
    delete serverCallbacks;
  }
  if (rxCallbacks) {
    delete rxCallbacks;
  }
}

bool BLEManager::begin() {
  DEBUG_PRINTLN("Initializing BLE Manager...");

  commandQueue = xQueueCreate(QUEUE_SIZE_COMMANDS, sizeof(BLEMessage));
  if (!commandQueue) {
    DEBUG_PRINTLN("ERROR: Failed to create command queue");
    return false;
  }

  bleMutex = xSemaphoreCreateMutex();
  if (!bleMutex) {
    DEBUG_PRINTLN("ERROR: Failed to create BLE mutex");
    return false;
  }

  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  serverCallbacks = new ServerCallbacks(this);
  pServer->setCallbacks(serverCallbacks);

  pService = pServer->createService(BLE_SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pRxCharacteristic = pService->createCharacteristic(
    BLE_CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxCallbacks = new CharacteristicCallbacks(this);
  pRxCharacteristic->setCallbacks(rxCallbacks);

  pService->start();

  // Request larger MTU size to handle bigger responses
  // This will be negotiated with the client when they connect
  BLEDevice::setMTU(512); // Request up to 512 bytes MTU

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);

  BLEDevice::startAdvertising();

  DEBUG_PRINTLN("BLE Manager initialized - waiting for connections");
  return true;
}

void BLEManager::update() {
  if (deviceConnected != oldDeviceConnected) {
    if (deviceConnected) {
      DEBUG_PRINTLN("BLE client connected");
    }
    else {
      DEBUG_PRINTLN("BLE client disconnected");
      BLEDevice::startAdvertising();
    }
    oldDeviceConnected = deviceConnected;
  }

  processCommands();
}

bool BLEManager::sendResponse(const char* response) {
  if (!deviceConnected || !pTxCharacteristic) {
    DEBUG_PRINTLN("ERROR: Cannot send BLE response - not connected or no TX characteristic");
    return false;
  }

  BLEDescriptor* pDescriptor = pTxCharacteristic->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
  if (pDescriptor) {
    uint8_t* descriptorData = pDescriptor->getValue();
    size_t descriptorLength = pDescriptor->getLength();
    if (descriptorLength > 0) {
      DEBUG_PRINTF("Client notification descriptor: 0x%02X\n", descriptorData[0]);
      if (descriptorData[0] == 0x01) {
        DEBUG_PRINTLN("Notifications are ENABLED");
      }
      else {
        DEBUG_PRINTLN("WARNING: Notifications are NOT enabled by client");
      }
    }
    else {
      DEBUG_PRINTLN("WARNING: No notification descriptor value found - client may not have enabled notifications");
    }
  }
  else {
    DEBUG_PRINTLN("WARNING: No notification descriptor found");
  }

  uint16_t mtu = 23; // Default BLE MTU
  if (deviceConnected && pServer) {
    mtu = pServer->getPeerMTU(pServer->getConnId());
    DEBUG_PRINTF("Negotiated BLE MTU: %d bytes\n", mtu);
  }

  // Calculate usable payload size (MTU - ATT overhead)
  // ATT Write/Notification overhead is typically 3 bytes
  size_t maxPayloadSize = (mtu > 3) ? (mtu - 3) : 20;

  const size_t MAX_BLE_PACKET_SIZE = (maxPayloadSize > 160) ? 160 : maxPayloadSize;

  size_t responseLen = strlen(response);
  DEBUG_PRINTF("Sending BLE response (%d bytes), Max packet size: %d bytes\n", responseLen, MAX_BLE_PACKET_SIZE);
  DEBUG_PRINTF("Response content: '%s'\n", response);

  if (responseLen <= MAX_BLE_PACKET_SIZE) {
    pTxCharacteristic->setValue(response);
    pTxCharacteristic->notify();
    DEBUG_PRINTLN("BLE response sent successfully (single packet)");
  }
  else {
    size_t offset = 0;
    int packetNum = 1;

    while (offset < responseLen) {
      size_t remainingBytes = responseLen - offset;
      size_t chunkSize = (remainingBytes > MAX_BLE_PACKET_SIZE) ? MAX_BLE_PACKET_SIZE : remainingBytes;

      char packet[MAX_BLE_PACKET_SIZE + 1];
      strncpy(packet, response + offset, chunkSize);
      packet[chunkSize] = '\0';

      DEBUG_PRINTF("Sending BLE packet %d (%d bytes): '%s'\n", packetNum, strlen(packet), packet);

      pTxCharacteristic->setValue(packet);
      pTxCharacteristic->notify();

      offset += chunkSize;
      packetNum++;

      // NOTE: Will this cause issues with UX?
      delay(10);
    }

    DEBUG_PRINTF("BLE response sent successfully (%d packets)\n", packetNum - 1);
  }

  return true;
}

void BLEManager::processCommands() {
  BLEMessage message;

  while (xQueueReceive(commandQueue, &message, 0) == pdTRUE) {
    DEBUG_PRINTF("=== Processing BLE command from queue ===\n");
    DEBUG_PRINTF("Raw data: '%s'\n", message.rawData);

    parseCommand(message.rawData, message);

    DEBUG_PRINTF("Parsed command: %d\n", message.command);
    handleCommand(message);
  }
}

void BLEManager::parseCommand(const char* data, BLEMessage& message) {
  DEBUG_PRINTF("BLE parseCommand called with data: '%s'\n", data);
  // TODO: Add gamepad triggers HID

  message.timestamp = millis();
  message.dataCount = 0;

  char cleanData[128];
  strncpy(cleanData, data, sizeof(cleanData) - 1);
  cleanData[sizeof(cleanData) - 1] = '\0';

  int len = strlen(cleanData);
  while (len > 0 && (cleanData[len - 1] == '\n' || cleanData[len - 1] == '\r' || cleanData[len - 1] == ' ')) {
    cleanData[--len] = '\0';
  }

  DEBUG_PRINTF("Cleaned command data: '%s'\n", cleanData);

  strncpy(message.rawData, data, sizeof(message.rawData) - 1);
  message.rawData[sizeof(message.rawData) - 1] = '\0';

  if (!parseDataComponents(cleanData, message)) {
    DEBUG_PRINTLN("ERROR: Failed to parse command data components");
    message.command = BLE_CMD_SYNTAX_ERROR;
    if (statusManager) {
      statusManager->setStatus(STATUS_BLE_CMD_ERROR, LED_BLINK_DURATION);
    }
    return;
  }

  const char* commandPart = message.parsedData[0];
  message.command = BLE_CMD_UNKNOWN;

  for (const auto& entry : commandMap) {
    if (strcmp(commandPart, entry.name) == 0) {
      message.command = entry.command;
      break;
    }
  }

  if (message.command == BLE_CMD_HELP && strcmp(commandPart, "HELP") != 0) {
    DEBUG_PRINTF("Unknown command: '%s', defaulting to HELP\n", commandPart);
  }

  DEBUG_PRINTF("Final parsed command ID: %d\n", message.command);
}

bool BLEManager::parseDataComponents(const char* data, BLEMessage& message) {
  for (int i = 0; i < 8; i++) {
    message.parsedData[i][0] = '\0';
  }
  message.dataCount = 0;

  char buffer[128];
  strncpy(buffer, data, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char* commandPart = strtok(buffer, BLE_CMD_PART_SEPARATOR);
  if (commandPart) {
    strncpy(message.parsedData[0], commandPart, sizeof(message.parsedData[0]) - 1);
    message.parsedData[0][sizeof(message.parsedData[0]) - 1] = '\0';
    message.dataCount = 1;

    char* dataPart = strtok(NULL, "");
    if (dataPart) {
      char* token = strtok(dataPart, BLE_CMD_DATA_SEPARATOR);
      while (token && message.dataCount < 8) {
        strncpy(message.parsedData[message.dataCount], token, sizeof(message.parsedData[message.dataCount]) - 1);
        message.parsedData[message.dataCount][sizeof(message.parsedData[message.dataCount]) - 1] = '\0';
        message.dataCount++;
        token = strtok(NULL, BLE_CMD_DATA_SEPARATOR);
      }
    }
  }
  else {
    strncpy(message.parsedData[0], data, sizeof(message.parsedData[0]) - 1);
    message.parsedData[0][sizeof(message.parsedData[0]) - 1] = '\0';
    message.dataCount = 1;
  }

  return true;
}

void BLEManager::handleCommand(const BLEMessage& message) {
  DEBUG_PRINTF("BLE Command: %d, Raw Data: %s, Parsed Count: %d\n", message.command, message.rawData, message.dataCount);

  for (uint8_t i = 0; i < message.dataCount; i++) {
    DEBUG_PRINTF("  Data[%d]: %s\n", i, message.parsedData[i]);
  }

  switch (message.command) {
  case BLE_CMD_POWER_INFO:
    DEBUG_PRINTLN("Getting power info");
    sendResponse(powerManager->getPowerInfo());
    break;

  case BLE_CMD_POWER_ON:
    powerManager->trySetSBCPower(true);
    if (statusManager) {
      statusManager->setStatus(STATUS_POWER_ON, LED_BLINK_DURATION);
    }
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    break;

  case BLE_CMD_POWER_OFF:
    powerManager->trySetSBCPower(false);
    if (statusManager) {
      statusManager->setStatus(STATUS_POWER_OFF, LED_BLINK_DURATION);
    }
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    break;

  case BLE_CMD_SHUTDOWN:
    powerManager->trySetSBCPower(false);
    if (statusManager) {
      statusManager->setStatus(STATUS_SHUTDOWN, 0);
    }
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    break;

  case BLE_CMD_HID_KEYBOARD_PRESS:
    if (message.dataCount >= 2) {
      uint8_t key = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Keyboard Press: %d (from string '%s')\n", key, message.parsedData[1]);
      bool result = usbManager->sendKeyPress(key);
      DEBUG_PRINTF("sendKeyPress result: %s\n", result ? "SUCCESS" : "FAILURE");
      sendResponse(result ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      DEBUG_PRINTF("HID Keyboard Press: FAILED - insufficient data count: %d\n", message.dataCount);
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_KEYBOARD_HOLD:
    if (message.dataCount >= 2) {
      uint8_t key = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Keyboard Hold: %d\n", key);
      sendResponse(usbManager->sendKeyHold(key) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_KEYBOARD_RELEASE:
    if (message.dataCount >= 2) {
      uint8_t key = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Keyboard Release: %d\n", key);
      sendResponse(usbManager->sendKeyRelease(key) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_KEYBOARD_TYPE:
    if (message.dataCount >= 2) {
      DEBUG_PRINTF("HID Keyboard Type: %s\n", message.parsedData[1]);
      sendResponse(usbManager->typeText(message.parsedData[1]) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_MOUSE_MOVE:
    if (message.dataCount >= 3) {
      int16_t x = (int16_t)atoi(message.parsedData[1]);
      int16_t y = (int16_t)atoi(message.parsedData[2]);
      DEBUG_PRINTF("HID Mouse Move: %d, %d\n", x, y);
      sendResponse(usbManager->sendMouseMove(x, y) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_MOUSE_PRESS:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Mouse Press: %d\n", button);
      sendResponse(usbManager->sendMousePress(button) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_MOUSE_HOLD:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Mouse Hold: %d\n", button);
      sendResponse(usbManager->sendMouseHold(button) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_MOUSE_RELEASE:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Mouse Release: %d\n", button);
      sendResponse(usbManager->sendMouseRelease(button) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_MOUSE_SCROLL:
    if (message.dataCount >= 3) {
      int8_t scrollX = (int8_t)atoi(message.parsedData[1]);
      int8_t scrollY = (int8_t)atoi(message.parsedData[2]);
      DEBUG_PRINTF("HID Mouse Scroll: %d, %d\n", scrollX, scrollY);
      sendResponse(usbManager->sendMouseScroll(scrollX, scrollY) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_GAMEPAD_PRESS:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Gamepad Press: %d\n", button);
      sendResponse(usbManager->sendGamepadButton(button, true) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_GAMEPAD_HOLD:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Gamepad Hold: %d\n", button);
      sendResponse(usbManager->sendGamepadButton(button, false) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_GAMEPAD_RELEASE:
    if (message.dataCount >= 2) {
      uint8_t button = (uint8_t)atoi(message.parsedData[1]);
      DEBUG_PRINTF("HID Gamepad Release: %d\n", button);
      sendResponse(usbManager->sendGamepadButton(button, false) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_GAMEPAD_RIGHT_AXIS:
    if (message.dataCount >= 3) {
      int16_t x = (int16_t)atoi(message.parsedData[1]);
      int16_t y = (int16_t)atoi(message.parsedData[2]);
      DEBUG_PRINTF("HID Gamepad Axis: %d, %d\n", x, y);
      sendResponse(usbManager->sendGamepadRightAxis(x, y) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_GAMEPAD_LEFT_AXIS:
    if (message.dataCount >= 3) {
      int16_t x = (int16_t)atoi(message.parsedData[1]);
      int16_t y = (int16_t)atoi(message.parsedData[2]);
      DEBUG_PRINTF("HID Gamepad Left Axis: %d, %d\n", x, y);
      sendResponse(usbManager->sendGamepadLeftAxis(x, y) ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    }
    else {
      sendResponse(BLE_CMD_WAS_FAILURE);
    }
    break;

  case BLE_CMD_HID_SYSTEM_POWER:
    DEBUG_PRINTLN("HID System Power");
    sendResponse(usbManager->sendSystemPowerKey() ? BLE_CMD_WAS_SUCCESSFUL : BLE_CMD_WAS_FAILURE);
    break;

  case BLE_CMD_SYSTEM_INFO:
    DEBUG_PRINTLN("Getting system info");
    sendResponse(systemManager->getSystemInfo());
    break;

  case BLE_CMD_SYSTEM_RESTART:
    DEBUG_PRINTLN("Restarting system");
    if (statusManager) {
      statusManager->setStatus(STATUS_SHUTDOWN, LED_BLINK_DURATION);
    }
    systemManager->notifyActivity();
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    delay(1000);
    esp_restart();
    break;

  case BLE_CMD_DEEP_SLEEP_INFO: {
    DEBUG_PRINTLN("Getting deep sleep info");
    sendResponse(systemManager->getDeepSleepInfo());
    break;
  }

  case BLE_CMD_DEEP_SLEEP_ENABLE:
    DEBUG_PRINTLN("Enabling deep sleep watchdog");
    systemManager->enableDeepSleep();
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    break;

  case BLE_CMD_DEEP_SLEEP_DISABLE:
    DEBUG_PRINTLN("Disabling deep sleep watchdog");
    systemManager->disableDeepSleep();
    sendResponse(BLE_CMD_WAS_SUCCESSFUL);
    break;

  case BLE_CMD_HELP:
    sendResponse(BLE_HELP_STRING);
    break;

  default:
    sendResponse(BLE_CMD_UNKNOWN_STRING);
    if (statusManager) {
      statusManager->setStatus(STATUS_BLE_CMD_ERROR, LED_BLINK_DURATION);
    }
    break;
  }
}

void BLEManager::ServerCallbacks::onConnect(BLEServer* server) {
  manager->deviceConnected = true;
}

void BLEManager::ServerCallbacks::onDisconnect(BLEServer* server) {
  manager->deviceConnected = false;
}

void BLEManager::CharacteristicCallbacks::onWrite(BLECharacteristic* characteristic) {
  std::string stdValue = characteristic->getValue();
  if (stdValue.length() > 0 && stdValue.length() < 128) {
    if (systemManager) {
      systemManager->notifyActivity();
    }

    BLEMessage message; // Will be filled with data later
    strncpy(message.rawData, stdValue.c_str(), sizeof(message.rawData) - 1);
    message.rawData[sizeof(message.rawData) - 1] = '\0';
    message.timestamp = millis();
    message.dataCount = 0;
    message.command = BLE_CMD_HELP;

    BaseType_t result = xQueueSendFromISR(manager->commandQueue, &message, NULL);
  }
}