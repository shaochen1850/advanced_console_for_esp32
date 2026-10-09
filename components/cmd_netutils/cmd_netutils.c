/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Commands for network utilities
 * Including: ifconfig nslookup netstat curl ntp netstat curl ...
 */

#include "cmd_netutils.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h" 
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "esp_tls.h"

#include "argtable3/argtable3.h"

#include "lwip/netif.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/stats.h"
#include "lwip/ip_addr.h"

// static const char *TAG = "cmd_netutils";
static const char *my_gai_strerror(int e) {
    switch (e) {
        case EAI_NONAME: return "Name or service not known";
        case EAI_AGAIN:  return "Temporary failure, try again";
        case EAI_FAIL:   return "Name resolution failure";
        case EAI_MEMORY: return "Memory allocation failure";
        default:         return "Unknown error";
    }
}
//For LwIP Linked List of TCP and UDP PCBs
extern struct tcp_pcb *tcp_active_pcbs;
extern struct tcp_pcb *tcp_tw_pcbs;
extern struct udp_pcb *udp_pcbs;

#ifdef CONFIG_LWIP_STATS
extern struct stats_ stats;   // LWIP statics global variable
#endif

static struct {
    struct arg_str *url;
    struct arg_str *data;
    struct arg_str *header;
    struct arg_str *useragent;
    struct arg_str *userpwd;
    struct arg_lit *head;
    struct arg_lit *show_headers;
    struct arg_lit *silent;
    struct arg_lit *verbose;
    struct arg_lit *fail;
    struct arg_lit *version;
    struct arg_lit *help;
    struct arg_end *end;
} curl_args;

/* collect response data */
typedef struct {
    char *buf;
    size_t len;
    size_t capacity;
} resp_buf_t;

/// ============================================================
// ifconfig  — show all network interfaces and their IP/MAC/DNS info
// ============================================================

static void print_netif_info(esp_netif_t *netif)
{
    // 接口名称
    const char *ifname = esp_netif_get_ifkey(netif);  // 形如 "WIFI_STA_DEF"
    printf("%-12s ", ifname);

    // 是否 up
    bool up = esp_netif_is_netif_up(netif);
    printf("status: %s\n", up ? "UP" : "DOWN");
    // IP 信息
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && up) {
        printf("    inet  addr: " IPSTR "  mask: " IPSTR "  gw: " IPSTR "\n",
               IP2STR(&ip.ip),
               IP2STR(&ip.netmask),
               IP2STR(&ip.gw));
    } else {
        printf("    inet  addr: 0.0.0.0\n");
    }

    // MAC address
    uint8_t mac[6];
    if (esp_netif_get_mac(netif, mac) == ESP_OK) {
        printf("    ether " MACSTR "\n", MAC2STR(mac));
    }

    // DNS (May obtain from main interface)
    esp_netif_dns_info_t dns;
    if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
        char dns_str[32];
        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, dns_str, sizeof(dns_str));
        printf("    dns   %s\n", dns_str);
    }

    // MTU (lwip) unavailable, commenting out for now
    /* netif_impl_data_t *impl = esp_netif_get_netif_impl(netif);
    *if (impl && impl->netif) {
    *    printf("    mtu   %d\n", impl->netif->mtu);
    * }
    */

    printf("\n");
}

static int cmd_ifconfig(int argc, char **argv)
{
    // gather all esp_netif interface（STATION / AP / ETH）
    esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t *netif_ap  = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_t *netif_eth = esp_netif_get_handle_from_ifkey("ETH_DEF");

    if (netif_sta && esp_netif_is_netif_up(netif_sta)) {
        print_netif_info(netif_sta);
    }
    if (netif_ap && esp_netif_is_netif_up(netif_ap)) {
        print_netif_info(netif_ap);
    }
    if (netif_eth && esp_netif_is_netif_up(netif_eth)) {
        print_netif_info(netif_eth);
    }

    // if no interfaces are up, print a message
    bool any_up = (netif_sta && esp_netif_is_netif_up(netif_sta)) ||
                  (netif_ap  && esp_netif_is_netif_up(netif_ap))  ||
                  (netif_eth && esp_netif_is_netif_up(netif_eth));
    if (!any_up) {
        printf("No active network interfaces.\n");
    }

    return 0;
}

static void register_ifconfig(void)
{
    const esp_console_cmd_t cmd = {
        .command = "ifconfig",
        .help = "Show network interface information",
        .hint = NULL,
        .func = &cmd_ifconfig,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================
// nslookup
// ============================================================
static struct {
    struct arg_str *host;
    struct arg_end *end;
} nslookup_args;

// ============================================================
// nslookup  — lookup address → IP（v4 / v6）
// ============================================================
static int cmd_nslookup(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&nslookup_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, nslookup_args.end, argv[0]);
        return 1;
    }

    const char *hostname = nslookup_args.host->sval[0];
    printf("Looking up: %s\n", hostname);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;   // 同时请求 v4 和 v6
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int64_t t0 = esp_timer_get_time();
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    int64_t t1 = esp_timer_get_time();

    if (err != 0 || res == NULL) {
        printf("Resolution failed: %s\n", my_gai_strerror(err));
        return 1;
    }

    int count = 0;
    for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next) {
        char addr_str[INET6_ADDRSTRLEN] = {0};

        if (rp->ai_family == AF_INET) {
            // IPv4
            struct sockaddr_in *addr4 = (struct sockaddr_in *)rp->ai_addr;
            inet_ntop(AF_INET, &addr4->sin_addr, addr_str, sizeof(addr_str));
            printf("  IPv4: %s\n", addr_str);
            count++;
        }
#if LWIP_IPV6   // compiled when CONFIG_LWIP_IPV6 is enabled
        else if (rp->ai_family == AF_INET6) {
            // IPv6
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)rp->ai_addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr, addr_str, sizeof(addr_str));
            printf("  IPv6: %s\n", addr_str);
            count++;
        }
#endif
    }

    freeaddrinfo(res);

    printf("Found %d address(es), took %lld μs\n",
           count, (long long)(t1 - t0));
    return 0;
}

static void register_nslookup(void)
{
    nslookup_args.host = arg_str1(NULL, NULL, "<hostname>", "Hostname to resolve");
    nslookup_args.end  = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "nslookup",
        .help = "Resolve a hostname to IP address",
        .hint = "<hostname>",
        .func = &cmd_nslookup,
        .argtable = &nslookup_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================
// ping  — already implemented in console_ping dependency, so we don't implement it here
// ============================================================
/*static struct {
    struct arg_str *host;
    struct arg_int *count;
    struct arg_end *end;
} ping_args;

static int cmd_ping(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ping_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ping_args.end, argv[0]);
        return 1;
    }
    int count = ping_args.count->ival[0];
    printf("ping: not implemented yet (host=%s, count=%d)\n",
           ping_args.host->sval[0], count);
    return 0;
}

static void register_ping(void)
{
    ping_args.host  = arg_str1(NULL, NULL, "<host>", "Target hostname or IP");
    ping_args.count = arg_int0("c", "count", "<n>", "Number of pings (default 4)");
    ping_args.count->ival[0] = 4;  // default
    ping_args.end   = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "ping",
        .help = "Send ICMP echo requests to a host",
        .hint = "<host> [-c <count>]",
        .func = &cmd_ping,
        .argtable = &ping_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
*/

// ============================================================
// netstat  — show network connections and statistics
// ============================================================
static int cmd_netstat(int argc, char **argv)
{
    printf("=== Network interfaces ===\n");
    struct netif *n;
    for (n = netif_list; n != NULL; n = n->next) {
        printf("%c%c: IP %s, mask %s, gw %s\n",
               n->name[0], n->name[1],
               ipaddr_ntoa(&n->ip_addr),
               ipaddr_ntoa(&n->netmask),
               ipaddr_ntoa(&n->gw));
    }

    printf("\n=== Routing table ===\n");
    // Display default route (if any)
    n = netif_list;
    while (n) {
        if (!ip_addr_isany(&n->gw)) {
            printf("default via %s dev %c%c\n", ipaddr_ntoa(&n->gw), n->name[0], n->name[1]);
            break;
        }
        n = n->next;
    }

    printf("\n=== Active TCP connections ===\n");
    struct tcp_pcb *pcb = tcp_active_pcbs;
    while (pcb) {
        printf("TCP  %s:%d -> %s:%d state %d\n",
               ipaddr_ntoa(&pcb->local_ip), pcb->local_port,
               ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port,
               pcb->state);
        pcb = pcb->next;
    }

    printf("\n=== TIME_WAIT TCP ===\n");
    pcb = tcp_tw_pcbs;
    while (pcb) {
        printf("TCP  %s:%d -> %s:%d (TIME_WAIT)\n",
               ipaddr_ntoa(&pcb->local_ip), pcb->local_port,
               ipaddr_ntoa(&pcb->remote_ip), pcb->remote_port);
        pcb = pcb->next;
    }

    printf("\n=== Active UDP connections ===\n");
    struct udp_pcb *upcb = udp_pcbs;
    while (upcb) {
        printf("UDP  %s:%d\n",
               ipaddr_ntoa(&upcb->local_ip), upcb->local_port);
        upcb = upcb->next;
    }

    /*Disable stats display for a compile error*/
#if 0
#ifdef CONFIG_LWIP_STATS
    printf("\n=== Network statistics ===\n");
    printf("IP:   %"PRIu16" packets received, %"PRIu16" sent, %"PRIu16" forwarded, %"PRIu16" dropped\n",
           stats.ip.recv, stats.ip.xmit, stats.ip.fw, stats.ip.drop);
    printf("TCP:  %"PRIu16" segments received, %"PRIu16" sent\n",
           stats.tcp.recv, stats.tcp.xmit);
    printf("UDP:  %"PRIu16" datagrams received, %"PRIu16" sent\n",
           stats.udp.recv, stats.udp.xmit);
#endif
#endif

    return 0;
}

static void register_netstat(void)
{
    const esp_console_cmd_t netstat_cmd = {
        .command = "netstat",
        .help = "Show network statistics and connections",
        .func = &cmd_netstat,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&netstat_cmd));
}

// ============================================================
// curl
// ============================================================
static void init_resp(resp_buf_t *rb)
{
    rb->buf = NULL;
    rb->len = 0;
    rb->capacity = 0;
}

static void append_resp(resp_buf_t *rb, const char *data, int len)
{
    if (len <= 0 || data == NULL) return;
    if (rb->capacity - rb->len < (size_t)len + 1) {
        rb->capacity = rb->len + len + 1024;
        char *new_buf = realloc(rb->buf, rb->capacity);
        if (!new_buf) {
            // 内存不足时，保留已有数据，放弃本次追加
            rb->capacity = rb->len;  // 回滚容量
            return;
        }
        rb->buf = new_buf;
    }
    memcpy(rb->buf + rb->len, data, len);
    rb->len += len;
    rb->buf[rb->len] = 0;
}

static void free_resp(resp_buf_t *rb)
{
    free(rb->buf);
    rb->buf = NULL;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = (resp_buf_t *)evt->user_data;

    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
        ESP_LOGD("cmd_curl", "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD("cmd_curl", "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD("cmd_curl", "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        if (!curl_args.silent->count && curl_args.verbose->count) {
            printf("%s: %s\n", evt->header_key, evt->header_value);
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (evt->data_len && !curl_args.silent->count) {
            if (rb) {
                append_resp(rb, evt->data, evt->data_len);
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD("cmd_curl", "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD("cmd_curl", "HTTP_EVENT_DISCONNECTED");
        break;
    default:
        ESP_LOGD("cmd_curl", "Unhandled event");
        break;   /* hide all unhandled events including REDIRECT  */
}
    return ESP_OK;
}

static int version_info(void)
{
    printf("curl (esp-http-client) libcurl/7.0.0\n");
    printf("Protocols: http https\n");
    printf("Features: IPv6\n");
    return 0;
}

static int cmd_curl(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&curl_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, curl_args.end, argv[0]);
        return 1;
    }

    if (curl_args.help->count) {
        printf("Usage: curl [options...] <url>\n"
               " -d, --data <data>            HTTP POST data\n"
               " -f, --fail                   Fail fast with no output on HTTP errors\n"
               " -I, --head                   Show document info only\n"
               " -H, --header <header/@file>  Pass custom header(s) to server\n"
               " -h, --help                   Get help for commands\n"
               " -i, --show-headers           Show response headers in output\n"
               " -s, --silent                 Silent mode\n"
               " -u, --user <user:password>   Server user and password\n"
               " -A, --user-agent <name>      Send User-Agent <name> to server\n"
               " -v, --verbose                Make the operation more talkative\n"
               " -V, --version                Show version number and quit\n"
               );
        return 0;
    }

    if (curl_args.version->count) {
        return version_info();
    }

    if (curl_args.url->count == 0) {
        printf("Error: url is required\n");
        return 1;
    }
    
    const char *url = curl_args.url->sval[0];

    /*Important: Check whether the network is initialized*/
    if (esp_netif_get_nr_of_ifs() == 0) {
        printf("Error: network is not initialized. "
               "Please connect to WiFi/Ethernet first.\n");
        return 1;
    }

    /* If no protocol is specified, auto-append http:// */
    char *fixed_url = NULL;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        size_t len = strlen(url) + 7 + 1;
        fixed_url = malloc(len);
        if (!fixed_url) {
            printf("Error: out of memory\n");
            return 1;
        }
        snprintf(fixed_url, len, "http://%s", url);
        url = fixed_url;
    }

    

    /* ── 1. Configure basic parameter ── */
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 10000, //Timeout on 10 secs
    };

    resp_buf_t rb;
    init_resp(&rb);
    config.user_data = &rb;

    if (curl_args.useragent->count) {
        config.user_agent = curl_args.useragent->sval[0];
    }
    if (curl_args.userpwd->count) {
        config.username = curl_args.userpwd->sval[0];
    }

    /* ── 2. Initialize client ── */
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        printf("Error: failed to init HTTP client\n");
        if (fixed_url) free(fixed_url);
        free_resp(&rb);
        return 1;
    }
    /* ── 3. Set method based on options ── */
    esp_http_client_method_t method = HTTP_METHOD_GET;   // for verbose printing
    if (curl_args.head->count) {
        esp_http_client_set_method(client, HTTP_METHOD_HEAD);
        method = HTTP_METHOD_HEAD;
    } else if (curl_args.data->count) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_post_field(client,
                                       curl_args.data->sval[0],
                                       strlen(curl_args.data->sval[0]));
        method = HTTP_METHOD_POST;
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
        method = HTTP_METHOD_GET;
    }

    /* ── 4. Custom header ── */
    if (curl_args.header->count) {
        for (int i = 0; i < curl_args.header->count; i++) {
            const char *h = curl_args.header->sval[i];
            const char *colon = strchr(h, ':');
            if (colon) {
                char key[128];
                size_t klen = colon - h;
                if (klen > 127) klen = 127;
                memcpy(key, h, klen);
                key[klen] = 0;
                const char *val = colon + 1;
                while (*val == ' ') val++;
                esp_http_client_set_header(client, key, val);
            } else {
                esp_http_client_set_header(client, h, "");
            }
        }
    }

    /* ── 5. verbose output request line ── */
    if (curl_args.verbose->count && !curl_args.silent->count) {
        const char *method_str =
            (method == HTTP_METHOD_HEAD) ? "HEAD" :
            (method == HTTP_METHOD_POST) ? "POST" : "GET";
        printf("> %s %s HTTP/1.1\n", method_str, url);
    }

    /* ── 6. execute ── */
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);

        if (curl_args.fail->count && status >= 400) {
            printf("curl: (%d) error\n", status);
            free_resp(&rb);
            esp_http_client_cleanup(client);
            if (fixed_url) free(fixed_url);
            return status;
        }

        if (!curl_args.silent->count && rb.buf) {
            printf("%s", rb.buf);
        }
    } else {
        ESP_LOGE("cmd_curl", "HTTP request failed: %s", esp_err_to_name(err));
        free_resp(&rb);
        esp_http_client_cleanup(client);
        if (fixed_url) free(fixed_url);
        return 1;
    }

    free_resp(&rb);
    esp_http_client_cleanup(client);
    if (fixed_url) free(fixed_url);
    return 0;
}

static void register_curl(void)
{
    curl_args.url        = arg_str1(NULL, NULL, "<url>", "URL to access");
    curl_args.data       = arg_str0("d", "data", "<data>", "HTTP POST data");
    curl_args.header     = arg_strn("H", "header", "<header/@file>", 0, 10, "Pass custom header(s)");
    curl_args.useragent  = arg_str0("A", "user-agent", "<name>", "User-Agent");
    curl_args.userpwd    = arg_str0("u", "user", "<user:password>", "Server user and password");
    curl_args.head       = arg_lit0("I", "head", "Show document info only");
    curl_args.show_headers = arg_lit0("i", "show-headers", "Show response headers");
    curl_args.silent     = arg_lit0("s", "silent", "Silent mode");
    curl_args.verbose    = arg_lit0("v", "verbose", "Make the operation more talkative");
    curl_args.fail       = arg_lit0("f", "fail", "Fail fast on HTTP errors");
    curl_args.version    = arg_lit0("V", "version", "Show version number");
    curl_args.help       = arg_lit0("h", "help", "Show help");
    curl_args.end        = arg_end(20);
    const esp_console_cmd_t cmd = {
        .command = "curl",
        .help = "Transfer a URL",
        .hint = NULL,
        .func = &cmd_curl,
        .argtable = &curl_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// ============================================================
// ntp  — Synchronize time with NTP server
// ============================================================

static int cmd_ntp(int argc, char **argv)
{
    // Default NTP server
    const char *server = "pool.ntp.org";

    // If args were provided, use the first one as the server
    if (argc >= 2) {
        server = argv[1];
    }

    printf("Syncing time with NTP server: %s ...\n", server);

    // Configure sntp
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    esp_netif_sntp_init(&config);

    // Wait for synchronization to complete (up to 10 seconds)
    int retry = 0;
    const int max_retry = 20;  // Check every 500ms, total 10s
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < max_retry) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Cleanup
    esp_netif_sntp_deinit();

    if (retry >= max_retry) {
        printf("NTP sync failed (timeout)\n");
        return 1;
    }

    // Print current time
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    printf("Time updated: %s\n", strftime_buf);

    return 0;
}

static void register_ntp(void)
{
    const esp_console_cmd_t ntp_cmd = {
        .command = "ntp",
        .help = "Sync time with NTP server (default: pool.ntp.org)",
        .hint = "[<server>]",
        .func = &cmd_ntp,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ntp_cmd));
}

// ============================================================
// Register all network utility commands at the same time
// ============================================================
void register_netutils(void)
{
    register_ifconfig();
    register_nslookup();
    register_netstat();
    register_curl();
    register_ntp();
    register_netstat();
    register_curl();
}