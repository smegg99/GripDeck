// include/managers/SystemManager.h
#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <cstdio>

class SystemManager {
private:
  void checkPowerButton();

  uint32_t lastButtonTime;
  bool lastButtonState;
  bool buttonPressed;
  uint32_t buttonPressStartTime;
public:
  SystemManager();
  ~SystemManager();

  bool begin();
  void update();

  const char* getSystemInfo() const;
};

#endif // SYSTEM_MANAGER_H