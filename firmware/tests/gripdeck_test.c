// tests/gripdeck_test.c
#include "gripdeck_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nShutting down...\n");
}

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -p, --ping     Send ping command\n");
    printf("  -s, --status   Get device status\n");
    printf("  -i, --info     Get device info\n");
    printf("  -m, --monitor  Monitor device status (updates every 2 seconds)\n");
    printf("  -a, --all      Run all commands once\n");
}

int main(int argc, char *argv[]) {
    int opt_ping = 0, opt_status = 0, opt_info = 0, opt_monitor = 0, opt_all = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--ping") == 0) {
            opt_ping = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--status") == 0) {
            opt_status = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--info") == 0) {
            opt_info = 1;
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--monitor") == 0) {
            opt_monitor = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) {
            opt_all = 1;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (!opt_ping && !opt_status && !opt_info && !opt_monitor && !opt_all) {
        print_usage(argv[0]);
        return 0;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int fd = gripdeck_open_device();
    if (fd < 0) {
        fprintf(stderr, "Failed to open GripDeck device\n");
        return 1;
    }
    
    uint32_t sequence = 1;
    
    if (opt_all || opt_ping) {
        printf("\n--- Testing PING command ---\n");
        if (gripdeck_ping(fd, sequence++) == 0) {
            printf("PING test passed\n");
        } else {
            printf("PING test failed\n");
        }
    }
    
    if (opt_all || opt_info) {
        printf("\n--- Getting device info ---\n");
        info_payload_t info;
        if (gripdeck_get_info(fd, &info, sequence++) == 0) {
            gripdeck_print_info(&info);
        } else {
            printf("Failed to get device info\n");
        }
    }
    
    if (opt_all || opt_status) {
        printf("\n--- Getting device status ---\n");
        status_payload_t status;
        if (gripdeck_get_status(fd, &status, sequence++) == 0) {
            gripdeck_print_status(&status);
        } else {
            printf("Failed to get device status\n");
        }
    }
    
    if (opt_monitor) {
        printf("\n--- Monitoring device status (Ctrl+C to stop) ---\n");
        while (running) {
            status_payload_t status;
            if (gripdeck_get_status(fd, &status, sequence++) == 0) {
                printf("\033[2J\033[H");
                time_t now = time(NULL);
                printf("Last update: %s", ctime(&now));
                gripdeck_print_status(&status);
            } else {
                printf("Failed to get device status\n");
            }
            
            sleep(2);
        }
    }
    
    gripdeck_close_device(fd);
    printf("Device closed successfully\n");
    return 0;
}
