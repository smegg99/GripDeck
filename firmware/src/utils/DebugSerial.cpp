// src/utils/DebugSerial.cpp
#include "utils/DebugSerial.h"
#include <stdarg.h>

HardwareSerial* DebugSerial::debugSerial = nullptr;

void DebugSerial::begin() {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  // Use Serial1 for debug output on the external UART pins
  debugSerial = &Serial1;

  // Initialize the UART with explicit pin configuration
  debugSerial->begin(DEBUG_SERIAL_BAUD_RATE, SERIAL_8N1, PIN_DEBUG_UART_RX, PIN_DEBUG_UART_TX);

  // Small delay to let the serial initialize
  delay(100);

  // Send initialization messages
  debugSerial->println("\n=== GripDeck Debug Serial Started ===");
  debugSerial->printf("Debug UART: TX=GPIO%d, RX=GPIO%d, Baud=%d\n",
    PIN_DEBUG_UART_TX, PIN_DEBUG_UART_RX, DEBUG_SERIAL_BAUD_RATE);
  debugSerial->println("UART initialization complete");
  debugSerial->println("USB port is reserved for HID functionality");
  debugSerial->println("========================================\n");

  // Send a test pattern to verify UART is working
  for (int i = 0; i < 5; i++) {
    debugSerial->printf("Test pattern %d - UART functional\n", i + 1);
    delay(100);
  }
  debugSerial->println("Debug UART test complete\n");
#endif
}

void DebugSerial::print(const char* message) {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    debugSerial->print(message);
  }
#endif
}

void DebugSerial::print(const String& message) {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    debugSerial->print(message);
  }
#endif
}

void DebugSerial::println(const char* message) {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    debugSerial->println(message);
  }
#endif
}

void DebugSerial::println(const String& message) {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    debugSerial->println(message);
  }
#endif
}

void DebugSerial::printf(const char* format, ...) {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    va_list args;
    va_start(args, format);

    // Create buffer for formatted string
    char buffer[512];  // Adjust size as needed
    vsnprintf(buffer, sizeof(buffer), format, args);

    debugSerial->print(buffer);

    va_end(args);
  }
#endif
}

void DebugSerial::flush() {
#if DEBUG_ENABLED && DEBUG_SERIAL_ENABLED
  if (debugSerial) {
    debugSerial->flush();
  }
#endif
}
