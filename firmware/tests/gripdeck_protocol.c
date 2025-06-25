// tests/gripdeck_protocol.c
#define _GNU_SOURCE

#include "gripdeck_protocol.h"
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <libudev.h>

static char* find_gripdeck_hidraw_device(void) {
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices, *dev_list_entry;
    struct udev_device *dev, *parent_dev;
    char *device_path = NULL;
    
    udev = udev_new();
    if (!udev) {
        fprintf(stderr, "Cannot create udev context\n");
        return NULL;
    }
    
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);
    
    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);

        parent_dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
        if (parent_dev) {
            const char *vid = udev_device_get_sysattr_value(parent_dev, "idVendor");
            const char *pid = udev_device_get_sysattr_value(parent_dev, "idProduct");
            
            if (vid && pid) {
                unsigned int vendor_id = strtoul(vid, NULL, 16);
                unsigned int product_id = strtoul(pid, NULL, 16);
                
                if (vendor_id == GRIPDECK_VID && product_id == GRIPDECK_PID) {
                    const char *devnode = udev_device_get_devnode(dev);
                    if (devnode) {
                        device_path = strdup(devnode);
                        udev_device_unref(dev);
                        break;
                    }
                }
            }
        }
        udev_device_unref(dev);
    }
    
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
    
    return device_path;
}

int gripdeck_open_device(void) {
    char *device_path = find_gripdeck_hidraw_device();
    if (!device_path) {
        fprintf(stderr, "GripDeck device not found\n");
        return -1;
    }
    
    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s: %s\n", device_path, strerror(errno));
        free(device_path);
        return -1;
    }
    
    printf("Opened GripDeck device: %s\n", device_path);
    free(device_path);

    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) < 0) {
        fprintf(stderr, "Failed to get report descriptor size: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    printf("HID report descriptor size: %d bytes\n", desc_size);
    return fd;
}

void gripdeck_close_device(int fd) {
    if (fd >= 0) {
        close(fd);
    }
}

int gripdeck_send_command(int fd, vendor_command_t cmd, uint32_t sequence) {
    vendor_packet_t packet = {0};
    
    packet.magic = PROTOCOL_MAGIC;
    packet.protocol_version = PROTOCOL_VERSION;
    packet.command = (uint8_t)cmd;
    packet.sequence = sequence;
    
    uint8_t buffer[VENDOR_REPORT_SIZE + 1];
    buffer[0] = VENDOR_REPORT_ID;
    memcpy(&buffer[1], &packet, sizeof(packet));

    int result = ioctl(fd, HIDIOCSFEATURE(sizeof(buffer)), buffer);
    if (result < 0) {
        fprintf(stderr, "Failed to send command 0x%02X: %s\n", cmd, strerror(errno));
        return -1;
    }
    
    return 0;
}

int gripdeck_receive_response(int fd, vendor_packet_t *response) {
    if (!response) {
        return -1;
    }
    
    uint8_t buffer[VENDOR_REPORT_SIZE + 1];
    buffer[0] = VENDOR_REPORT_ID;
    
    int result = ioctl(fd, HIDIOCGFEATURE(sizeof(buffer)), buffer);
    if (result < 0) {
        fprintf(stderr, "Failed to receive response: %s\n", strerror(errno));
        return -1;
    }

    memcpy(response, &buffer[1], sizeof(*response));

    if (response->magic != PROTOCOL_MAGIC) {
        fprintf(stderr, "Invalid response magic: 0x%04X (expected 0x%04X)\n", 
                response->magic, PROTOCOL_MAGIC);
        return -1;
    }
    
    if (response->protocol_version != PROTOCOL_VERSION) {
        fprintf(stderr, "Invalid protocol version: %d (expected %d)\n", 
                response->protocol_version, PROTOCOL_VERSION);
        return -1;
    }
    
    return 0;
}

int gripdeck_ping(int fd, uint32_t sequence) {
    if (gripdeck_send_command(fd, CMD_PING, sequence) < 0) {
        return -1;
    }

    usleep(50000);
    
    vendor_packet_t response;
    if (gripdeck_receive_response(fd, &response) < 0) {
        return -1;
    }
    
    if (response.command != RESP_PONG) {
        fprintf(stderr, "Expected PONG response, got 0x%02X\n", response.command);
        return -1;
    }
    
    if (response.sequence != sequence) {
        fprintf(stderr, "Sequence mismatch: sent %u, received %u\n", 
                sequence, response.sequence);
        return -1;
    }
    
    printf("PING successful!\n");
    return 0;
}

int gripdeck_get_status(int fd, status_payload_t *status, uint32_t sequence) {
    if (!status) {
        return -1;
    }
    
    if (gripdeck_send_command(fd, CMD_GET_STATUS, sequence) < 0) {
        return -1;
    }
    
    vendor_packet_t response;
    if (gripdeck_receive_response(fd, &response) < 0) {
        return -1;
    }
    
    if (response.command != RESP_STATUS) {
        fprintf(stderr, "Expected STATUS response, got 0x%02X\n", response.command);
        return -1;
    }
    
    if (response.sequence != sequence) {
        fprintf(stderr, "Sequence mismatch: sent %u, received %u\n", 
                sequence, response.sequence);
        return -1;
    }
    
    memcpy(status, response.payload, sizeof(*status));
    return 0;
}

int gripdeck_get_info(int fd, info_payload_t *info, uint32_t sequence) {
    if (!info) {
        return -1;
    }
    
    if (gripdeck_send_command(fd, CMD_GET_INFO, sequence) < 0) {
        return -1;
    }
    
    vendor_packet_t response;
    if (gripdeck_receive_response(fd, &response) < 0) {
        return -1;
    }
    
    if (response.command != RESP_INFO) {
        fprintf(stderr, "Expected INFO response, got 0x%02X\n", response.command);
        return -1;
    }
    
    if (response.sequence != sequence) {
        fprintf(stderr, "Sequence mismatch: sent %u, received %u\n", 
                sequence, response.sequence);
        return -1;
    }
    
    memcpy(info, response.payload, sizeof(*info));
    return 0;
}

void gripdeck_print_status(const status_payload_t *status) {
  if (!status) return;
  
  printf("\n=== GripDeck Status ===\n");
  printf("Battery Voltage:       %u mV\n", status->battery_voltage_mv);
  printf("Battery Current:       %d mA\n", status->battery_current_ma);
  printf("Battery Percentage:    %u%%\n", status->battery_percentage);
  
  if (status->to_fully_discharge_s > 0) {
    int hours = status->to_fully_discharge_s / 3600;
    int minutes = (status->to_fully_discharge_s % 3600) / 60;
    printf("Time to Discharge:     %uh %um (%u seconds)\n", hours, minutes, status->to_fully_discharge_s);
  } else {
    printf("Time to Discharge:     N/A\n");
  }
  
  printf("Charger Voltage:       %u mV\n", status->charger_voltage_mv);
  printf("Charger Current:       %d mA\n", status->charger_current_ma);
  
  if (status->to_fully_charge_s > 0) {
    int hours = status->to_fully_charge_s / 3600;
    int minutes = (status->to_fully_charge_s % 3600) / 60;
    printf("Time to Full Charge:   %uh %um (%u seconds)\n", hours, minutes, status->to_fully_charge_s);
  } else {
    printf("Time to Full Charge:   N/A\n");
  }
  
  printf("Uptime:                %u seconds\n", status->uptime_seconds);
  printf("=======================\n\n");
}

void gripdeck_print_info(const info_payload_t *info) {
    if (!info) return;
    
    printf("\n=== GripDeck Info ===\n");
    printf("Firmware Version: 0x%04X\n", info->firmware_version);
    printf("Serial Number:    %.12s\n", info->serial_number);
    printf("====================\n\n");
}
