// tests/gripdeck_protocol.h
#ifndef GRIPDECK_PROTOCOL_H
#define GRIPDECK_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define VENDOR_REPORT_ID          6
#define VENDOR_REPORT_SIZE        32
#define PROTOCOL_VERSION          0x01
#define PROTOCOL_MAGIC            0x4744

#define GRIPDECK_VID              0x1209
#define GRIPDECK_PID              0x2078

typedef enum {
  CMD_PING = 0x01,
  CMD_GET_STATUS = 0x02,
  CMD_GET_INFO = 0x03,
  CMD_RESERVED = 0xFF
} vendor_command_t;

typedef enum {
  RESP_PONG = 0x81,
  RESP_STATUS = 0x82,
  RESP_INFO = 0x83,
  RESP_ERROR = 0xFF
} vendor_response_t;

typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint8_t protocol_version;
  uint8_t command;
  uint32_t sequence;
  uint8_t payload[24];
} vendor_packet_t;

typedef struct __attribute__((packed)) {
  uint16_t battery_voltage_mv;
  int16_t battery_current_ma;
  uint32_t to_fully_discharge_s;
  uint16_t charger_voltage_mv;
  int16_t charger_current_ma;
  uint32_t to_fully_charge_s;
  uint16_t charger_power_mw;
  uint8_t charger_connected;
  uint8_t battery_percentage;
  uint32_t uptime_seconds;
} status_payload_t;

typedef struct __attribute__((packed)) {
  uint16_t firmware_version;
  char serial_number[12];
  uint8_t reserved[8];
} info_payload_t;

int gripdeck_open_device(void);
void gripdeck_close_device(int fd);
int gripdeck_send_command(int fd, vendor_command_t cmd, uint32_t sequence);
int gripdeck_receive_response(int fd, vendor_packet_t* response);
int gripdeck_ping(int fd, uint32_t sequence);
int gripdeck_get_status(int fd, status_payload_t* status, uint32_t sequence);
int gripdeck_get_info(int fd, info_payload_t* info, uint32_t sequence);
void gripdeck_print_status(const status_payload_t* status);
void gripdeck_print_info(const info_payload_t* info);

#endif // GRIPDECK_PROTOCOL_H
