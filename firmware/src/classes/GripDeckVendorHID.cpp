// src/classes/GripDeckVendorHID.cpp
#include <classes/GripDeckVendorHID.h>
#include <managers/USBManager.h>
#include <utils/DebugSerial.h>

// HID Report Descriptor for vendor interface
const uint8_t vendorReportDescriptor[] = {
  0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
  0x09, 0x01,        // Usage (0x01)
  0xA1, 0x01,        // Collection (Application)
  0x85, VENDOR_REPORT_ID,  // Report ID
  0x09, 0x01,        // Usage (0x01)
  0x15, 0x00,        // Logical Minimum (0)
  0x25, 0xFF,        // Logical Maximum (255)
  0x75, 0x08,        // Report Size (8)
  0x95, VENDOR_REPORT_SIZE,  // Report Count
  0xB1, 0x02,        // Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
  0xC0,              // End Collection
};

const size_t vendorReportDescriptorSize = sizeof(vendorReportDescriptor);

GripDeckVendorHID::GripDeckVendorHID(USBManager* mgr, USBHID* hidInstance) : manager(mgr), hid(hidInstance) {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    DEBUG_PRINTF("Adding vendor HID device with descriptor size: %d\n", vendorReportDescriptorSize);
    if (USBHID::addDevice(this, vendorReportDescriptorSize)) {
      DEBUG_PRINTLN("Vendor HID device added successfully");
    }
    else {
      DEBUG_PRINTLN("ERROR: Failed to add vendor HID device");
    }
  }
}

void GripDeckVendorHID::begin() {
  if (hid) {
    DEBUG_PRINTLN("Starting vendor HID device...");
    hid->begin();
  }
}

uint16_t GripDeckVendorHID::_onGetDescriptor(uint8_t* buffer) {
  DEBUG_PRINTLN("Vendor HID: Descriptor requested");
  memcpy(buffer, vendorReportDescriptor, sizeof(vendorReportDescriptor));
  return sizeof(vendorReportDescriptor);
}

uint16_t GripDeckVendorHID::_onGetFeature(uint8_t report_id, uint8_t* buffer, uint16_t len) {
  DEBUG_PRINTF("Vendor HID: Get feature report ID=%d, len=%d\n", report_id, len);

  if (report_id != VENDOR_REPORT_ID || len < sizeof(VendorPacket)) {
    DEBUG_PRINTF("Invalid get feature request: ID=%d, len=%d\n", report_id, len);
    return 0;
  }

  VendorPacket response = {};

  // Try to get response from USBManager
  if (manager && manager->getVendorResponse(&response)) {
    memcpy(buffer, &response, sizeof(response));
    return sizeof(response);
  }

  // Fallback to error response if no manager or no response available
  response.magic = PROTOCOL_MAGIC;
  response.protocol_version = PROTOCOL_VERSION;
  response.command = static_cast<uint8_t>(RESP_ERROR);
  response.sequence = 0;

  memcpy(buffer, &response, sizeof(response));
  return sizeof(response);
}

void GripDeckVendorHID::_onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  DEBUG_PRINTF("Vendor HID: Set feature report ID=%d, len=%d\n", report_id, len);

  if (manager && report_id == VENDOR_REPORT_ID && len >= sizeof(VendorPacket)) {
    manager->handleVendorReport(report_id, buffer, len);
  }
}

void GripDeckVendorHID::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) {
  DEBUG_PRINTF("Vendor HID: Output report ID=%d, len=%d\n", report_id, len);
}