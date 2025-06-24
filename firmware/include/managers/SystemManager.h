// include/managers/SystemManager.h
#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <cstdio>

class SystemManager {
private:
  void checkPowerButton();
  void updateDeepSleepWatchdog();
  void resetActivityTimer();
  void enterDeepSleep();

  uint32_t lastButtonTime;
  bool lastButtonState;
  bool buttonPressed;
  uint32_t buttonPressStartTime;

  // Deep sleep watchdog timer
  uint32_t lastActivityTime;
  uint32_t lastActivityCheck;
  bool deepSleepEnabled;
  bool deepSleepRequested;

public:
  SystemManager();
  ~SystemManager();

  bool begin();
  void update();

  bool shouldEnterDeepSleep();
  void notifyActivity();
  void notifyWakeFromDeepSleep();

  void enableDeepSleep();
  void disableDeepSleep();
  bool isDeepSleepEnabled() const;
  uint32_t getTimeUntilDeepSleep() const;

  const char* getSystemInfo() const;
  const char* getDeepSleepInfo() const;
};

#endif // SYSTEM_MANAGER_H