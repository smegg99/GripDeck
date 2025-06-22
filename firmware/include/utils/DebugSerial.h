// include/utils/DebugSerial.h
#ifndef DEBUG_SERIAL_H
#define DEBUG_SERIAL_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "../config/Config.h"

class DebugSerial {
public:
  static void begin();
  static void print(const char* message);
  static void print(const String& message);
  static void println(const char* message);
  static void println(const String& message);
  static void printf(const char* format, ...);
  static void flush();

  template<typename T>
  static void print(T value) {
    if (debugSerial) debugSerial->print(value);
  }

  template<typename T>
  static void println(T value) {
    if (debugSerial) debugSerial->println(value);
  }

private:
  static HardwareSerial* debugSerial;
};

#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
#define DEBUG_PRINT(x)      do { DebugSerial::print(x); } while(0)
#define DEBUG_PRINTLN(x)    do { DebugSerial::println(x); } while(0) 
#define DEBUG_PRINTF(...)   do { DebugSerial::printf(__VA_ARGS__); } while(0)
#define DEBUG_FLUSH()       do { DebugSerial::flush(); } while(0)
#else
#define DEBUG_PRINT(x)      do { } while(0)
#define DEBUG_PRINTLN(x)    do { } while(0)
#define DEBUG_PRINTF(...)   do { } while(0)
#define DEBUG_FLUSH()       do { } while(0)
#endif

#if DEBUG_ENABLED && DEBUG_VERBOSE_LOGGING && DEBUG_SERIAL_ENABLED
#define DEBUG_VERBOSE_PRINT(x)      do { DebugSerial::print(x); } while(0)
#define DEBUG_VERBOSE_PRINTLN(x)    do { DebugSerial::println(x); } while(0)
#define DEBUG_VERBOSE_PRINTF(...)   do { DebugSerial::printf(__VA_ARGS__); } while(0)
#else
#define DEBUG_VERBOSE_PRINT(x)      do { } while(0)
#define DEBUG_VERBOSE_PRINTLN(x)    do { } while(0)
#define DEBUG_VERBOSE_PRINTF(...)   do { } while(0)
#endif

#endif // DEBUG_SERIAL_H
