/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * cmd_ble.c - Bluetooth LE control and debug commands for the console.
 */

#include "cmd_ble.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_console.h"
#include "esp_bt.h"
#include "esp_nimble_hci.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* ---------- Global state ---------- */
static bool s_ble_initialized = false;
static bool s_ble_scanning    = false;
static bool s_ble_advertising = false;
static bool s_nvs_ready       = false; // Defined global variable to track NVS initialization status

/* ---------- Forward declarations ---------- */
static void ble_adv_stop(void);
static void ble_scan_stop(void);
static int  ble_gap_event(struct ble_gap_event *event, void *arg);
static void nimble_host_task(void *param);

/* ---------- NVS Initialization with confirm ---------- */
static esp_err_t nvs_init_with_user_confirm(void) {
    printf("Initializing NVS...\n");
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        printf("\n\n====================================================\n");
        printf("WARNING: NVS partition corrupt or version mismatch!\n");
        printf("Error: %s\n", esp_err_to_name(ret));
        printf("Erasing NVS will clear all saved data (WiFi, BLE keys, etc).\n");
        printf("====================================================\n");

        while (1) {
            printf("\nErase NVS and continue? [y/N]: ");
            fflush(stdout);

            int c = getchar();

            // Ignore Enter
            if (c == '\r' || c == '\n') {
                continue;
            }

            printf("%c\n", c); // echo user input

            if (c == 'y' || c == 'Y') {
                printf("User accepted. Erasing NVS...\n");
                nvs_flash_erase();
                ret = nvs_flash_init();
                if (ret == ESP_OK) {
                    printf("NVS erase and re-init successful!\n");
                    s_nvs_ready = true;
                    return ESP_OK;
                } else {
                    printf("NVS re-init still failing: %s\n", esp_err_to_name(ret));
                    s_nvs_ready = false;
                    return ret;
                }
            } else {
                printf("User canceled. NVS remains intact, related features will be unavailable.\n");
                s_nvs_ready = false;
                return ESP_ERR_INVALID_STATE; // Return error indicating NVS is not ready
            }
        }
    } else if (ret == ESP_OK) {
        printf("NVS initialized successfully.\n");
        s_nvs_ready = true;
        return ESP_OK;
    } else {
        printf("NVS init failed with unexpected error: %s\n", esp_err_to_name(ret));
        s_nvs_ready = false;
        return ret;
    }
}

/* ---------- Initialization / Deinitialization ---------- */

static void ble_on_sync(void) {
    printf("BLE host synced\n");
}

static void ble_on_reset(int reason) {
    printf("BLE host reset; reason=%d\n", reason);
}

static int ble_store_status_cb(struct ble_store_status_event *event, void *arg) {
    return 0;
}

static esp_err_t ble_init(void) {
    if (s_ble_initialized) {
        printf("BLE already initialized\n");
        return ESP_OK;
    }

    // Important: Ensure NVS is initialized after user confirmation
    esp_err_t nvs_ret = nvs_init_with_user_confirm();
    if (nvs_ret != ESP_OK) {
        printf("NVS is not ready. Cannot initialize BLE.\n");
        return ESP_ERR_INVALID_STATE;
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        printf("Failed to init NimBLE port: %d\n", rc);
        return rc;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_status_cb;

    ble_svc_gap_init();
    ble_svc_gap_device_name_set("ESP32-S3");

    nimble_port_freertos_init(nimble_host_task);

    s_ble_initialized = true;
    printf("BLE initialized\n");
    return ESP_OK;
}

static void nimble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

static esp_err_t ble_deinit(void) {
    if (!s_ble_initialized) {
        printf("BLE not initialized\n");
        return ESP_ERR_INVALID_STATE;
    }

    ble_adv_stop();
    ble_scan_stop();

    nimble_port_stop();
    nimble_port_deinit();

    s_ble_initialized = false;
    printf("BLE deinitialized\n");
    return ESP_OK;
}

/* ---------- scan ---------- */

static void ble_scan_stop(void) {
    if (!s_ble_scanning) return;
    ble_gap_disc_cancel();
    s_ble_scanning = false;
    printf("Scan stopped\n");
}

static void ble_scan_start(int duration_ms) {
    if (!s_ble_initialized) {
        printf("BLE not initialized\n");
        return;
    }
    if (s_ble_scanning) {
        printf("Already scanning\n");
        return;
    }

    struct ble_gap_disc_params disc_params = {
        .itvl     = 100,
        .window   = 50,
        .filter_policy = 0,
        .limited  = 0,
        .passive  = 0,
        .filter_duplicates = 1,
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, duration_ms, &disc_params,
                          ble_gap_event, NULL);
    if (rc == 0) {
        s_ble_scanning = true;
        printf("Scanning for %d ms...\n", duration_ms);
    } else {
        printf("Failed to start scan (rc=%d)\n", rc);
    }
}

/* ---------- advertising ---------- */

static void ble_adv_stop(void) {
    if (!s_ble_advertising) return;
    ble_gap_adv_stop();
    s_ble_advertising = false;
    printf("Advertising stopped\n");
}

static void ble_adv_start(const char *name) {
    if (!s_ble_initialized) {
        printf("BLE not initialized\n");
        return;
    }
    if (s_ble_advertising) {
        printf("Already advertising\n");
        return;
    }

    if (name) ble_svc_gap_device_name_set(name);

    struct ble_hs_adv_fields adv_fields = {0};
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *dev_name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)dev_name;
    adv_fields.name_len = strlen(dev_name);
    adv_fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        printf("Failed to set adv fields (rc=%d)\n", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc == 0) {
        s_ble_advertising = true;
        printf("Advertising as \"%s\"\n", dev_name);
    } else {
        printf("Failed to start advertising (rc=%d)\n", rc);
    }
}

/* ---------- GAP event callback ---------- */

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
        struct ble_gap_disc_desc *d = &event->disc;
        
        // Print basic information
        printf("DEVICE: addr=%02x:%02x:%02x:%02x:%02x:%02x  rssi=%d  adv_len=%d\n",
               d->addr.val[5], d->addr.val[4], d->addr.val[3],
               d->addr.val[2], d->addr.val[1], d->addr.val[0],
               d->rssi, d->length_data);

        // Parse advertisement data to extract device name
        bool name_found = false;
        for (int i = 0; i < d->length_data; ) {
            uint8_t field_len = d->data[i];      // Current field length
            if (field_len == 0) break;           // Defensive programming, if length is 0 then exit
            
            uint8_t field_type = d->data[i + 1]; // Current field type

            // 0x08: Complete Local Name (Complete device name)
            // 0x09: Shortened Local Name (Shortened device name)
            if (field_type == 0x08 || field_type == 0x09) {
                uint8_t name_len = field_len - 1; // remove the 1 byte of field_type
                char name[32] = {0};              // Temporary buffer, BLE name should not exceed 31 bytes
                if (name_len >= sizeof(name)) {
                    name_len = sizeof(name) - 1;  // Avoid buffer overflow
                }
                memcpy(name, &d->data[i + 2], name_len);
                printf("  Name: %s\n", name);
                name_found = true;
                break; // Found the name, so we can break out of the loop
            }
            
            i += field_len + 1; // Jump to the next field (Length byte itself occupies 1 byte)
        }

        if (!name_found) {
            printf("  Name: (Not in ADV)\n");
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        printf("Scan completed\n");
        s_ble_scanning = false;
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        printf("Advertising stopped (completed)\n");
        s_ble_advertising = false;
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            printf("Connected (conn_handle=%d)\n", event->connect.conn_handle);
        } else {
            printf("Connection failed, status=%d\n", event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        printf("Disconnected (reason=%d)\n", event->disconnect.reason);
        s_ble_advertising = false;
        break;
    default:
        break;
    }
    return 0;
}

/* ---------- command handlers ---------- */

static int cmd_ble_init(int argc, char **argv) {
    return (ble_init() == ESP_OK) ? 0 : 1;
}

static int cmd_ble_deinit(int argc, char **argv) {
    return (ble_deinit() == ESP_OK) ? 0 : 1;
}

static int cmd_ble_status(int argc, char **argv) {
    printf("BLE initialized: %s\n", s_ble_initialized ? "Yes" : "No");
    printf("Scanning:        %s\n", s_ble_scanning ? "Yes" : "No");
    printf("Advertising:     %s\n", s_ble_advertising ? "Yes" : "No");
    return 0;
}

static int cmd_ble_scan(int argc, char **argv) {
    int duration = 5000;
    if (argc >= 3) {
        duration = atoi(argv[2]);
        if (duration < 0) duration = 0;
    }
    ble_scan_start(duration);
    return 0;
}

static int cmd_ble_scan_stop(int argc, char **argv) {
    ble_scan_stop();
    return 0;
}

static int cmd_ble_adv_start(int argc, char **argv) {
    const char *name = (argc >= 3) ? argv[2] : NULL;
    ble_adv_start(name);
    return 0;
}

static int cmd_ble_adv_stop(int argc, char **argv) {
    ble_adv_stop();
    return 0;
}

/* ---------- ble main command distribution ---------- */

static int cmd_ble_help(void) {
    printf("Usage: ble <init|deinit|status|scan|scan_stop|adv_start|adv_stop> [args]\n");
    printf("Example:\n");
    printf("  ble init\n");
    printf("  ble adv_start MyDevice\n");
    printf("  ble scan 3000\n");
    return 0;
}

static int cmd_ble(int argc, char **argv) {
    if (argc < 2) {
        return cmd_ble_help();
    }

    const char *sub = argv[1];

    if (strcmp(sub, "init") == 0)           return cmd_ble_init(argc, argv);
    if (strcmp(sub, "deinit") == 0)         return cmd_ble_deinit(argc, argv);
    if (strcmp(sub, "status") == 0)         return cmd_ble_status(argc, argv);
    if (strcmp(sub, "scan") == 0)           return cmd_ble_scan(argc, argv);
    if (strcmp(sub, "scan_stop") == 0)      return cmd_ble_scan_stop(argc, argv);
    if (strcmp(sub, "adv_start") == 0)      return cmd_ble_adv_start(argc, argv);
    if (strcmp(sub, "adv_stop") == 0)       return cmd_ble_adv_stop(argc, argv);

    printf("Unknown sub-command: %s\n", sub);
    return cmd_ble_help();
}

/* ---------- register commands ---------- */

void register_ble(void) {
    const esp_console_cmd_t cmd = {
        .command = "ble",
        .help    = "BLE control: init, deinit, status, scan, adv_start, adv_stop",
        .func    = cmd_ble,
    };
    esp_console_cmd_register(&cmd);
}
