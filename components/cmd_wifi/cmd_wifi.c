/*
 * Copyright 2026 shaochen1850
 *
 * github.com/shaochen1850
 * 
 * SPDX-License-Identifier: Apache-2.0
 */


#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "cmd_wifi.h"
#include "esp_mac.h"   // for MAC2STR

#define JOIN_TIMEOUT_MS 10000

// forward declarations
static int handle_wlan_ap_start(int argc, char **argv);
static int handle_wlan_ap_stop(int argc, char **argv);
static int handle_wlan_ap_list(int argc, char **argv);
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
const int SCAN_DONE_BIT = BIT1;
static bool auto_reconnect = true;

static const char *auth_mode_str(wifi_auth_mode_t auth)
{
    switch (auth) {
        case WIFI_AUTH_OPEN:        return "OPEN";
        case WIFI_AUTH_WEP:         return "WEP";
        case WIFI_AUTH_WPA_PSK:     return "WPA";
        case WIFI_AUTH_WPA2_PSK:    return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        default:                    return "OTHER";
    }
}

/* channel → Frequency(simplified) */
static uint16_t chan_to_freq(uint8_t chan)
{
    if (chan == 14) return 2484;
    if (chan >= 1 && chan <= 13) return 2407 + chan * 5;
    if (chan >= 36 && chan <= 64) return 5000 + chan * 5;
    if (chan >= 100 && chan <= 144) return 5000 + chan * 5;
    if (chan >= 149 && chan <= 161) return 5000 + chan * 5;
    if (chan >= 165 && chan <= 177) return 5000 + chan * 5;
    return 0;
}

/* standard description */
static const char *phy_mode_str(const wifi_ap_record_t *ap)
{
    if (ap->phy_11ax) return "802.11ax";
    if (ap->phy_11ac) return "802.11ac";
    if (ap->phy_11n)  return "802.11n";
    if (ap->phy_11g)  return "802.11g";
    if (ap->phy_11b)  return "802.11b";
    return "?";
}

/* ---------- Wifi Initialization ---------- */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (auto_reconnect) {
            esp_wifi_connect();
        }
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(wifi_event_group, SCAN_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;
    if (initialized) {
        return;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                                &event_handler, NULL));                                            
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    initialized = true;
}
static bool wifi_join(const char *ssid, const char *pass, int timeout_ms)
{
    // 1. Forced to the clean state
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(200));

    // make wifi STA mode
    int ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        printf("Failed to set STA mode: %s (0x%x)\n", esp_err_to_name(ret), ret);
        return false;
    }

    // 2. Setup
    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        printf("Failed to set WiFi config: %s (0x%x)\n", esp_err_to_name(ret), ret);
        return false;
    }

    // 3. Connect
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        printf("Failed to connect: %s (0x%x)\n", esp_err_to_name(ret), ret);
        return false;
    }

    printf("Connecting to '%s'...\n", ssid);

    // 4. Wait for result
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                                           pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
    bool success = (bits & CONNECTED_BIT) != 0;
    if (!success) {
        printf("Connection failed or timed out.\n");
    } else {
        printf("Connected!\n");
    }
    return success;
}

/* ---------- status ---------- */
static int handle_wlan_status(void)
{
    initialise_wifi();

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    printf("WiFi mode: ");
    switch (mode) {
        case WIFI_MODE_STA: printf("STA"); break;
        case WIFI_MODE_AP:  printf("AP");  break;
        case WIFI_MODE_APSTA: printf("AP+STA"); break;
        default: printf("None");
    }
    printf("\n");

    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
                printf("IP: " IPSTR "\n", IP2STR(&ip.ip));
            }
        }

        wifi_config_t cfg;
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
            printf("SSID: %s\n", cfg.sta.ssid);
        }
    }

    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap) {
            esp_netif_ip_info_t ip;
            if (esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
                printf("AP IP: " IPSTR "\n", IP2STR(&ip.ip));
            }
        }
    }

    return 0;
}


static int handle_wlan_scan(int argc, char **argv)
{
    initialise_wifi();
    // Make sure that WiFi is started before scanning
    (void)esp_wifi_start();
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
        ESP_ERROR_CHECK(err);
    }
    // Make sure STA mode is set
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.passive = 0,
    };

    // if defined SSID argument, set it
    uint8_t ssid_buf[32];
    if (argc >= 2) {
        memset(ssid_buf, 0, sizeof(ssid_buf));
        strlcpy((char *)ssid_buf, argv[1], sizeof(ssid_buf));
        scan_config.ssid = ssid_buf;
    }

    xEventGroupClearBits(wifi_event_group, SCAN_DONE_BIT);
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));  // non blocking

    int bits = xEventGroupWaitBits(wifi_event_group, SCAN_DONE_BIT,
                                   pdTRUE, pdTRUE, 10000 / portTICK_PERIOD_MS);
    if (!(bits & SCAN_DONE_BIT)) {
        printf("Scan timed out.\n");
        return 1;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        printf("No APs found.\n");
        return 0;
    }

    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_records) {
        printf("Memory allocation failed.\n");
        return 1;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

    printf("Found %d APs:\n", ap_count);
    printf("%-18s  %-32s  %4s  %4s  %2s  %6s  %-8s  %s\n",
           "BSSID", "SSID", "RSSI", "Ch", "Bw", "Freq", "Std", "Auth");

    for (int i = 0; i < ap_count; i++) {
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), MACSTR, MAC2STR(ap_records[i].bssid));

        const char *bw;
        if (ap_records[i].second != WIFI_SECOND_CHAN_NONE)
            bw = "40";
        else
            bw = "20";

        uint16_t freq = chan_to_freq(ap_records[i].primary);

        printf("%-18s  %-32s  %4d  %2u  %2s  %5u  %-8s  %s\n",
               bssid_str,
               (char *)ap_records[i].ssid,
               ap_records[i].rssi,
               ap_records[i].primary,
               bw,
               (unsigned)freq,
               phy_mode_str(&ap_records[i]),
               auth_mode_str(ap_records[i].authmode));
    }

    free(ap_records);
    return 0;
}

static int handle_wlan_connect(int argc, char **argv)
{
    auto_reconnect = true;
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        printf("Usage: wlan connect <ssid> [pass] [--timeout <ms>]\n");
        return 1;
    }

    const char *ssid = argv[1];
    const char *pass = NULL;
    int timeout = JOIN_TIMEOUT_MS;

    // Simple manual parsing: argv[2] if it is --timeout then...
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            timeout = atoi(argv[++i]);
        } else if (!pass) {
            pass = argv[i];          // First non-option argument as password
        }
    }

    printf("Connecting to '%s'...\n", ssid);
    bool ok = wifi_join(ssid, pass, timeout);
    if (!ok) {
        printf("Connection failed or timed out.\n");
        return 1;
    }
    printf("Connected.\n");
    return 0;
}

static int handle_wlan_disconnect(int argc, char **argv)
{
    auto_reconnect = false;
    initialise_wifi();

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        esp_wifi_disconnect();
        printf("Disconnected.\n");
    } else {
        printf("Not in STA mode, nothing to disconnect.\n");
    }
    return 0;
}

static int handle_wlan_reconnect(int argc, char **argv)
{
    if (argc < 2) {
        // No parameter → Display current status
        printf("Auto-reconnect: %s\n", auto_reconnect ? "ON" : "OFF");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        auto_reconnect = true;
        printf("Auto-reconnect enabled.\n");
    } else if (strcmp(argv[1], "off") == 0) {
        auto_reconnect = false;
        printf("Auto-reconnect disabled.\n");
    } else {
        printf("Usage: wlan reconnect [on|off]\n");
        return 1;
    }
    return 0;
}

static int handle_wlan_up(int argc, char **argv)
{
    initialise_wifi();
    (void)esp_wifi_start();  // No matter if already started
    printf("WiFi is UP.\n");
    return 0;
}

static int handle_wlan_down(int argc, char **argv)
{
    initialise_wifi();
    (void)esp_wifi_stop();   // No matter if already stopped
    printf("WiFi is DOWN.\n");
    return 0;
}

static int handle_wlan_ap(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n"
               "  wlan ap <ssid> <password> [channel]  Start SoftAP\n"
               "  wlan ap stop                         Stop SoftAP\n"
               "  wlan ap list                         List connected clients\n");
        return 1;
    }

    if (strcmp(argv[1], "stop") == 0) {
        return handle_wlan_ap_stop(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "list") == 0) {
        return handle_wlan_ap_list(argc - 1, argv + 1);
    } else {
        // Start AP: argv[1]=ssid, argv[2]=pass, [argv[3]=channel]
        return handle_wlan_ap_start(argc - 1, argv + 1);
    }
}

static int handle_wlan_ap_start(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wlan ap <ssid> <password> [channel]\n");
        return 1;
    }

    const char *ssid = argv[0];
    const char *pass = argv[1];
    int channel = 1;
    if (argc >= 3) {
        channel = atoi(argv[2]);
        if (channel < 1 || channel > 14) {
            printf("Channel must be 1–14\n");
            return 1;
        }
    }

    initialise_wifi();

    // Stop current WiFi for mode switching
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_ERROR_CHECK(err);
    }

    wifi_config_t ap_config = { 0 };
    strlcpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid));
    if (strlen(pass) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char *)ap_config.ap.password, pass, sizeof(ap_config.ap.password));
    }
    ap_config.ap.channel = channel;
    ap_config.ap.max_connection = 4;
    ap_config.ap.ssid_hidden = 0;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // show AP IP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
            printf("AP started, IP: " IPSTR "\n", IP2STR(&ip_info.ip));
        }
    }
    printf("SSID: %s, channel: %d\n", ssid, channel);
    return 0;
}

static int handle_wlan_ap_stop(int argc, char **argv)
{
    initialise_wifi();

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_start();   // return to NULL mode
        printf("AP stopped.\n");
    } else {
        printf("AP is not running.\n");
    }
    return 0;
}

static int handle_wlan_ap_list(int argc, char **argv)
{
    initialise_wifi();

    wifi_sta_list_t sta_list;
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);
    if (err != ESP_OK) {
        printf("Failed to get station list (AP not active?)\n");
        return 1;
    }

    printf("Connected clients (%d):\n", sta_list.num);
    if (sta_list.num == 0) return 0;

    // get AP netif's LwIP netif
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        printf("AP netif not found.\n");
        return 1;
    }
    int idx = esp_netif_get_netif_impl_index(ap_netif);
    if (idx < 0) {
        printf("Failed to get netif index.\n");
        return 1;
    }
    struct netif *lwip_netif = netif_get_by_index((u8_t)idx);
    if (!lwip_netif) {
        printf("LwIP netif not found.\n");
        return 1;
    }

    for (int i = 0; i < sta_list.num; i++) {
        uint8_t *mac = sta_list.sta[i].mac;
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(mac));

        ip4_addr_t ip = {0};
        bool found = false;

        // walk through ARP table
        for (int j = 0; j < 128; j++) {
            ip4_addr_t *pip = NULL;
            struct netif *pnetif = NULL;
            struct eth_addr *peth = NULL;
            int ret = etharp_get_entry(j, &pip, &pnetif, &peth);
            if (ret == 0) break;                     // No more entries
            if (pnetif == lwip_netif && peth && pip) { // ap interfaces
                if (memcmp(peth->addr, mac, 6) == 0) {
                    ip = *pip;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            printf("  %s  IP: " IPSTR "  RSSI: %d\n",
                   mac_str, IP2STR(&ip), sta_list.sta[i].rssi);
        } else {
            printf("  %s  (IP not found)  RSSI: %d\n",
                   mac_str, sta_list.sta[i].rssi);
        }
    }
    return 0;
}

/* ---------- help ---------- */
static void print_wlan_help(void)
{
    printf("Usage:\n");
    printf("  wlan status              Show WiFi status\n");
    printf("  wlan up                   Start WiFi (init + start)\n");
    printf("  wlan down                 Stop WiFi\n");
    printf("  wlan scan [ssid]         Scan for APs\n");
    printf("  wlan connect <ssid> [pass] [--timeout <ms>]  Connect to AP\n");
    printf("  wlan disconnect          Disconnect from AP\n");
    printf("  wlan reconnect [on|off]  Get/set auto-reconnect\n");
    printf("  wlan ap <ssid> <pass> [ch]  Start SoftAP\n");
    printf("  wlan ap stop                Stop SoftAP\n");
    printf("  wlan ap list                List connected clients\n");
    printf("  wlan help                Show this help\n");
}

/* ---------- main handles ---------- */
static int handle_wlan(int argc, char **argv)
{
    if (argc < 2) {
        return handle_wlan_status();
    }

    if (strcmp(argv[1], "status") == 0) {
        return handle_wlan_status();
    } else if (strcmp(argv[1], "scan") == 0) {
        return handle_wlan_scan(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "connect") == 0) {
        return handle_wlan_connect(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "disconnect") == 0) {
        return handle_wlan_disconnect(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "up") == 0) {
        return handle_wlan_up(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "down") == 0) {
        return handle_wlan_down(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "ap") == 0) {
        return handle_wlan_ap(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "reconnect") == 0) {
        return handle_wlan_reconnect(argc - 1, argv + 1);
    } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_wlan_help();
        return 0;
    } else {
        printf("Unknown subcommand: %s\n", argv[1]);
        print_wlan_help();
        return 1;
    }
}

/* ---------- register ---------- */
void register_wifi(void)
{
    const esp_console_cmd_t cmd = {
        .command = "wlan",
        .help = "WiFi control (status, scan, connect, disconnect, reconnect, up, down, ap, help)",
        .hint = NULL,
        .func = &handle_wlan,
        .argtable = NULL,          // subcommand self parsing
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}