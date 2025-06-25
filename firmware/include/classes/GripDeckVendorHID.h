// include/classes/GripDeckVendorHID.h
#ifndef GRIP_DECK_VENDOR_HID_H
#define GRIP_DECK_VENDOR_HID_H

#include <USBHID.h>
#include <cstdint>
#include <config/Config.h>

class USBManager;

enum VendorCommand : uint8_t {
  CMD_PING = 0x01,
  CMD_GET_STATUS = 0x02,
  CMD_GET_INFO = 0x03,
  CMD_RESERVED = 0xFF
};

enum VendorResponse : uint8_t {
  RESP_PONG = 0x81,
  RESP_STATUS = 0x82,
  RESP_INFO = 0x83,
  RESP_ERROR = 0xFF
};

struct __attribute__((packed)) VendorPacket {
  uint16_t magic;
  uint8_t protocol_version;
  uint8_t command;
  uint32_t sequence;
  uint8_t payload[24];
};

extern const uint8_t vendorReportDescriptor[];
extern const size_t vendorReportDescriptorSize;

class GripDeckVendorHID : public USBHIDDevice {
private:
  USBManager* manager;
  USBHID* hid;

public:
  GripDeckVendorHID(USBManager* mgr, USBHID* hidInstance);

  void begin();
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  uint16_t _onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len) override;
  void _onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;
};

#endif // GRIP_DECK_VENDOR_HID_H