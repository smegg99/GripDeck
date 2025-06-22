// include/managers/SystemManager.h
#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

class SystemManager
{
private:
  void checkPowerButton();
public:
  SystemManager();
  ~SystemManager();

  bool begin();
  void update();
};

#endif // SYSTEM_MANAGER_H