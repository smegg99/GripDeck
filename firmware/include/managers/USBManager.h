// include/managers/USBManager.h
#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <USBHIDGamepad.h>

class USBManager {
private:
  USBHIDKeyboard keyboard;
  USBHIDMouse mouse;
  USBHIDGamepad gamepad;

  bool usbConnected = false;

  QueueHandle_t hidQueue;
  SemaphoreHandle_t hidMutex;
public:
  USBManager();
  ~USBManager();

  bool begin();
  void update();

  bool isUSBConnected() const { return usbConnected; }
};

#endif // USB_MANAGER_H