#include "wifi_manager.h"
#include "elm327_wifi_profiles.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include "app.h"
#include "settings.h"

static const char *TAG = "wifi";

static bool connected = false;
static int sock = -1;
static bool wifi_ap_connected = false;
static bool wifi_stack_ready = false;

static EventGroupHandle_t wifi_event_group;
static const int WIFI_GOT_IP_BIT = BIT0;
static const int WIFI_DISCONNECTED_BIT = BIT1;

extern app_settings_t g_settings;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "WiFi associated with AP");
            wifi_ap_connected = true;
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "WiFi disconnected from AP");
            wifi_ap_connected = false;
            connected = false;
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_ap_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_GOT_IP_BIT);
    }
}

static bool str_case_contains(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }

    size_t hay_len = strlen(haystack);
    size_t needle_len = strlen(needle);
    if (needle_len > hay_len) {
        return false;
    }

    for (size_t i = 0; i <= hay_len - needle_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static esp_err_t wifi_ensure_init(void)
{
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
    }

    if (!wifi_stack_ready) {
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&cfg);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
            return err;
        }

        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL, &instance_any_id);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler, NULL, &instance_got_ip);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_stack_ready = true;
    }

    return ESP_OK;
}

static void wifi_prepare_connect_attempt(void)
{
    if (wifi_event_group != NULL) {
        xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT | WIFI_DISCONNECTED_BIT);
    }
}

static wifi_auth_mode_t wifi_normalize_authmode(wifi_auth_mode_t authmode)
{
    if (authmode == WIFI_AUTH_WPA2_WPA3_PSK) {
        return WIFI_AUTH_WPA2_PSK;
    }
    return authmode;
}

static bool wifi_auth_mode_in_list(wifi_auth_mode_t mode, const wifi_auth_mode_t *list, int count)
{
    for (int i = 0; i < count; i++) {
        if (list[i] == mode) {
            return true;
        }
    }
    return false;
}

static void wifi_append_auth_mode(wifi_auth_mode_t mode, wifi_auth_mode_t *list, int *count, int max)
{
    mode = wifi_normalize_authmode(mode);
    if (*count >= max || wifi_auth_mode_in_list(mode, list, *count)) {
        return;
    }
    list[*count] = mode;
    (*count)++;
}

bool wifi_is_elm327_ssid(const char *ssid)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < ELM327_SSID_EXACT_COUNT; i++) {
        if (strcmp(ssid, elm327_ssid_exact[i]) == 0) {
            return true;
        }
    }

    for (size_t i = 0; i < ELM327_SSID_KEYWORD_COUNT; i++) {
        if (str_case_contains(ssid, elm327_ssid_keywords[i])) {
            return true;
        }
    }

    return false;
}

static bool ap_list_contains(const wifi_ap_info_t *ap_list, int count, const char *ssid)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(ap_list[i].ssid, ssid) == 0) {
            return true;
        }
    }
    return false;
}

static void sort_ap_list_by_rssi(wifi_ap_info_t *ap_list, int count)
{
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (ap_list[j].rssi < ap_list[j + 1].rssi) {
                wifi_ap_info_t temp = ap_list[j];
                ap_list[j] = ap_list[j + 1];
                ap_list[j + 1] = temp;
            }
        }
    }
}

int wifi_scan_elm327_networks(wifi_ap_info_t *ap_list, int max_count)
{
    ESP_LOGI(TAG, "Scanning for ELM327 WiFi networks...");

    if (wifi_ensure_init() != ESP_OK) {
        return 0;
    }

    if (!wifi_ap_connected) {
        ESP_ERROR_CHECK(esp_wifi_start());
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d networks", ap_count);

    if (ap_count == 0) {
        return 0;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return 0;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    int found_count = 0;
    for (int i = 0; i < ap_count && found_count < max_count; i++) {
        const char *ssid = (const char *)ap_records[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        if (!wifi_is_elm327_ssid(ssid)) {
            continue;
        }
        if (ap_list_contains(ap_list, found_count, ssid)) {
            continue;
        }

        strncpy(ap_list[found_count].ssid, ssid, sizeof(ap_list[found_count].ssid) - 1);
        ap_list[found_count].ssid[sizeof(ap_list[found_count].ssid) - 1] = '\0';
        ap_list[found_count].rssi = ap_records[i].rssi;
        ap_list[found_count].authmode = ap_records[i].authmode;
        ESP_LOGI(TAG, "Found ELM327 candidate: %s (RSSI: %d, auth: %d)",
                 ap_list[found_count].ssid, ap_list[found_count].rssi, ap_list[found_count].authmode);
        found_count++;
    }

    free(ap_records);

    if (found_count == 0) {
        ESP_LOGW(TAG, "No ELM327 WiFi networks found");
    } else {
        ESP_LOGI(TAG, "Found %d ELM327 candidate(s)", found_count);
    }

    return found_count;
}

int wifi_scan_all_networks(wifi_ap_info_t *ap_list, int max_count)
{
    ESP_LOGI(TAG, "Scanning all WiFi networks...");

    if (wifi_ensure_init() != ESP_OK) {
        return 0;
    }

    if (!wifi_ap_connected) {
        ESP_ERROR_CHECK(esp_wifi_start());
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        return 0;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        return 0;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    int found_count = 0;
    for (int i = 0; i < ap_count && found_count < max_count; i++) {
        const char *ssid = (const char *)ap_records[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        if (ap_list_contains(ap_list, found_count, ssid)) {
            continue;
        }

        strncpy(ap_list[found_count].ssid, ssid, sizeof(ap_list[found_count].ssid) - 1);
        ap_list[found_count].ssid[sizeof(ap_list[found_count].ssid) - 1] = '\0';
        ap_list[found_count].rssi = ap_records[i].rssi;
        ap_list[found_count].authmode = ap_records[i].authmode;
        found_count++;
    }

    free(ap_records);
    sort_ap_list_by_rssi(ap_list, found_count);

    ESP_LOGI(TAG, "Scan complete: %d network(s)", found_count);
    return found_count;
}

bool wifi_connect_with_auth(const char *ssid, const char *password, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s (auth=%d)", ssid, authmode);

    if (wifi_ensure_init() != ESP_OK) {
        return false;
    }

    wifi_prepare_connect_attempt();

    authmode = wifi_normalize_authmode(authmode);

    wifi_config_t wifi_config = {0};
    wifi_config.sta.threshold.authmode = authmode;
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);

    if (authmode == WIFI_AUTH_OPEN || authmode == WIFI_AUTH_WEP) {
        wifi_config.sta.password[0] = '\0';
    } else if (password != NULL) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_GOT_IP_BIT | WIFI_DISCONNECTED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_GOT_IP_BIT) {
        ESP_LOGI(TAG, "WiFi associated with IP: %s", ssid);
        wifi_ap_connected = true;
        return true;
    }

    ESP_LOGW(TAG, "WiFi association failed: %s", ssid);
    return false;
}

bool wifi_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGW(TAG, "Empty SSID, attempting ELM327 auto-detection");
        return wifi_connect_to_elm327();
    }

    return wifi_connect_with_auth(ssid, password, WIFI_AUTH_WPA2_PSK);
}

static bool wifi_remember_password(const char *password)
{
    if (password == NULL) {
        return false;
    }

    if (strcmp(g_settings.wifi_password, password) != 0) {
        strncpy(g_settings.wifi_password, password, sizeof(g_settings.wifi_password) - 1);
        g_settings.wifi_password[sizeof(g_settings.wifi_password) - 1] = '\0';
    }
    return true;
}

static bool wifi_associate_obd_ap(const char *ssid, wifi_auth_mode_t scan_authmode)
{
    wifi_auth_mode_t auth_modes[6];
    int auth_count = 0;

    wifi_append_auth_mode(scan_authmode, auth_modes, &auth_count, 6);
    wifi_append_auth_mode(WIFI_AUTH_WPA2_PSK, auth_modes, &auth_count, 6);
    wifi_append_auth_mode(WIFI_AUTH_WPA_WPA2_PSK, auth_modes, &auth_count, 6);
    wifi_append_auth_mode(WIFI_AUTH_WPA_PSK, auth_modes, &auth_count, 6);
    wifi_append_auth_mode(WIFI_AUTH_OPEN, auth_modes, &auth_count, 6);

    const bool same_saved_ssid =
        (g_settings.wifi_ssid[0] != '\0' && strcmp(ssid, g_settings.wifi_ssid) == 0);

    ESP_LOGI(TAG, "Trying universal OBD WiFi credentials for: %s", ssid);

    for (int a = 0; a < auth_count; a++) {
        const wifi_auth_mode_t auth = auth_modes[a];

        if (auth == WIFI_AUTH_OPEN) {
            if (wifi_connect_with_auth(ssid, "", WIFI_AUTH_OPEN)) {
                g_settings.wifi_password[0] = '\0';
                return true;
            }
            continue;
        }

        if (same_saved_ssid && g_settings.wifi_password[0] != '\0') {
            ESP_LOGI(TAG, "Trying saved password for %s", ssid);
            if (wifi_connect_with_auth(ssid, g_settings.wifi_password, auth)) {
                return true;
            }
        }

        for (size_t i = 0; i < ELM327_PASSWORD_COUNT; i++) {
            const char *password = elm327_passwords[i];
            if (password[0] == '\0') {
                continue;
            }

            ESP_LOGI(TAG, "Trying OBD password \"%s\" (auth=%d)", password, auth);
            if (wifi_connect_with_auth(ssid, password, auth)) {
                wifi_remember_password(password);
                return true;
            }
        }
    }

    return false;
}

static bool wifi_try_password_list(const char *ssid, wifi_auth_mode_t authmode)
{
    return wifi_associate_obd_ap(ssid, authmode);
}

bool wifi_connect_to_elm327(void)
{
    ESP_LOGI(TAG, "Auto-connecting to ELM327 WiFi adapter...");

    wifi_ap_info_t ap_list[16];
    int found = wifi_scan_elm327_networks(ap_list, 16);

    if (found > 0) {
        sort_ap_list_by_rssi(ap_list, found);

        for (int i = 0; i < found; i++) {
            ESP_LOGI(TAG, "Trying AP %d/%d: %s (RSSI %d)",
                     i + 1, found, ap_list[i].ssid, ap_list[i].rssi);
            if (wifi_try_password_list(ap_list[i].ssid, ap_list[i].authmode)) {
                return true;
            }
        }
    }

    if (g_settings.wifi_ssid[0] != '\0') {
        ESP_LOGW(TAG, "Scan miss, trying configured SSID: %s", g_settings.wifi_ssid);
        if (wifi_try_password_list(g_settings.wifi_ssid, WIFI_AUTH_WPA2_PSK)) {
            return true;
        }
    }

    ESP_LOGW(TAG, "Trying common default SSIDs...");
    static const char *fallback_ssids[] = {"OBDII", "OBD2", "WiFi-OBD", "ELM327", "WiFi-ELM327"};
    for (size_t i = 0; i < sizeof(fallback_ssids) / sizeof(fallback_ssids[0]); i++) {
        if (wifi_try_password_list(fallback_ssids[i], WIFI_AUTH_WPA2_PSK)) {
            return true;
        }
    }

    ESP_LOGE(TAG, "All ELM327 WiFi association attempts failed");
    return false;
}

static bool wifi_get_gateway_ip(char *ip_str, size_t len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }

    if (ip_info.gw.addr == 0) {
        return false;
    }

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.gw));
    return true;
}

static bool endpoint_already_tried(const char *ip, uint16_t port,
                                   const char tried[][32], const uint16_t *ports, int count)
{
    for (int i = 0; i < count; i++) {
        if (ports[i] == port && strcmp(tried[i], ip) == 0) {
            return true;
        }
    }
    return false;
}

static int wifi_open_tcp_socket(const char *ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    struct timeval timeout = {
        .tv_sec = ELM327_TCP_CONNECT_TIMEOUT_S,
        .tv_usec = 0,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, ip, &dest_addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static bool wifi_response_looks_like_elm327(const char *resp, int len)
{
    if (resp == NULL || len <= 0) {
        return false;
    }

    if (str_case_contains(resp, "ELM327") ||
        str_case_contains(resp, "OBDII") ||
        str_case_contains(resp, "STN") ||
        str_case_contains(resp, "ICAR") ||
        str_case_contains(resp, "OBDLink") ||
        str_case_contains(resp, "VLINK") ||
        str_case_contains(resp, "OK")) {
        return true;
    }

    for (int i = 0; i < len; i++) {
        if (resp[i] == '>') {
            return true;
        }
    }

    return false;
}

static bool wifi_probe_elm327_tcp(int fd)
{
    const char *probe_cmd = "ATI\r";
    if (send(fd, probe_cmd, strlen(probe_cmd), 0) < 0) {
        return false;
    }

    char resp[128] = {0};
    int total = 0;

    for (int attempt = 0; attempt < 4; attempt++) {
        int received = recv(fd, resp + total, (int)sizeof(resp) - total - 1, 0);
        if (received > 0) {
            total += received;
            resp[total] = '\0';
            if (wifi_response_looks_like_elm327(resp, total)) {
                ESP_LOGI(TAG, "ELM327 probe OK: %.48s%s", resp, total > 48 ? "..." : "");
                return true;
            }
        } else if (received == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    ESP_LOGD(TAG, "ELM327 probe failed on this endpoint");
    return false;
}

static bool wifi_init_elm327_session(int fd)
{
    const char *init_cmds[] = {
        "ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r",
        "ATAT1\r", "ATST32\r", "ATSP0\r",
    };
    const int delays_ms[] = {800, 100, 100, 100, 100, 100, 100, 400};

    for (size_t i = 0; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        send(fd, init_cmds[i], strlen(init_cmds[i]), 0);
        vTaskDelay(pdMS_TO_TICKS(delays_ms[i]));
    }

    char drain[128];
    for (int i = 0; i < 4; i++) {
        int n = recv(fd, drain, sizeof(drain) - 1, 0);
        if (n <= 0) {
            break;
        }
    }

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    struct timeval timeout = {.tv_sec = 0, .tv_usec = 800000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    return true;
}

static void wifi_flush_rx(void)
{
    if (sock < 0) {
        return;
    }

    char dump[64];
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    while (recv(sock, dump, sizeof(dump), 0) > 0) {
    }

    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags);
    }
}

static bool wifi_response_matches_pid(const char *resp, uint8_t pid)
{
    char pattern_a[8];
    char pattern_b[8];

    if (resp == NULL) {
        return false;
    }

    snprintf(pattern_a, sizeof(pattern_a), "41%02X", pid);
    snprintf(pattern_b, sizeof(pattern_b), "41 %02X", pid);

    return strstr(resp, pattern_a) != NULL || strstr(resp, pattern_b) != NULL;
}

static bool wifi_try_tcp_endpoint(const char *ip, uint16_t port)
{
    ESP_LOGI(TAG, "Probing TCP endpoint %s:%u", ip, (unsigned)port);

    int fd = wifi_open_tcp_socket(ip, port);
    if (fd < 0) {
        ESP_LOGD(TAG, "TCP connect failed for %s:%u", ip, (unsigned)port);
        return false;
    }

    if (!wifi_probe_elm327_tcp(fd)) {
        close(fd);
        return false;
    }

    if (!wifi_init_elm327_session(fd)) {
        close(fd);
        return false;
    }

    if (sock >= 0) {
        close(sock);
    }

    sock = fd;
    connected = true;
    strncpy(g_settings.obd_adapter_ip, ip, sizeof(g_settings.obd_adapter_ip) - 1);
    g_settings.obd_adapter_ip[sizeof(g_settings.obd_adapter_ip) - 1] = '\0';
    g_settings.obd_adapter_port = port;
    ESP_LOGI(TAG, "Connected to ELM327 at %s:%u", ip, (unsigned)port);
    return true;
}

static bool wifi_discover_tcp_endpoint(void)
{
    char tried_ips[24][32];
    uint16_t tried_ports[24];
    int tried_count = 0;

    if (g_settings.obd_adapter_ip[0] != '\0' && g_settings.obd_adapter_port != 0) {
        if (wifi_try_tcp_endpoint(g_settings.obd_adapter_ip, g_settings.obd_adapter_port)) {
            return true;
        }
        tried_ips[tried_count][0] = '\0';
        strncpy(tried_ips[tried_count], g_settings.obd_adapter_ip, sizeof(tried_ips[tried_count]) - 1);
        tried_ports[tried_count] = g_settings.obd_adapter_port;
        tried_count++;
    }

    char gateway_ip[16] = {0};
    if (wifi_get_gateway_ip(gateway_ip, sizeof(gateway_ip))) {
        ESP_LOGI(TAG, "DHCP gateway: %s", gateway_ip);
        static const uint16_t gateway_ports[] = {35000, 35001, 8080, 23};
        for (size_t i = 0; i < sizeof(gateway_ports) / sizeof(gateway_ports[0]); i++) {
            uint16_t port = gateway_ports[i];
            if (endpoint_already_tried(gateway_ip, port, tried_ips, tried_ports, tried_count)) {
                continue;
            }
            if (wifi_try_tcp_endpoint(gateway_ip, port)) {
                return true;
            }
            strncpy(tried_ips[tried_count], gateway_ip, sizeof(tried_ips[tried_count]) - 1);
            tried_ports[tried_count] = port;
            tried_count++;
        }
    }

    for (size_t i = 0; i < ELM327_TCP_PROFILE_COUNT; i++) {
        const char *ip = elm327_tcp_profiles[i].ip;
        uint16_t port = elm327_tcp_profiles[i].port;

        if (endpoint_already_tried(ip, port, tried_ips, tried_ports, tried_count)) {
            continue;
        }

        if (wifi_try_tcp_endpoint(ip, port)) {
            return true;
        }

        if (tried_count < (int)(sizeof(tried_ips) / sizeof(tried_ips[0]))) {
            strncpy(tried_ips[tried_count], ip, sizeof(tried_ips[tried_count]) - 1);
            tried_ports[tried_count] = port;
            tried_count++;
        }
    }

    ESP_LOGE(TAG, "No ELM327 TCP endpoint responded");
    return false;
}

bool wifi_connect_to_obd_adapter(void)
{
    ESP_LOGI(TAG, "Starting ELM327 WiFi connect (manual=%d)...", g_settings.wifi_manual_mode);

    if (!wifi_ap_connected) {
        bool wifi_ok = false;
        wifi_auth_mode_t saved_auth = g_settings.wifi_authmode;
        if (saved_auth == 0) {
            saved_auth = WIFI_AUTH_WPA2_PSK;
        }

        if (g_settings.wifi_manual_mode && g_settings.wifi_ssid[0] != '\0') {
            ESP_LOGI(TAG, "Using saved manual network: %s", g_settings.wifi_ssid);
            wifi_ok = wifi_try_password_list(g_settings.wifi_ssid, saved_auth);
        } else {
            if (g_settings.wifi_ssid[0] != '\0') {
                wifi_ok = wifi_try_password_list(g_settings.wifi_ssid, saved_auth);
            }
            if (!wifi_ok) {
                wifi_ok = wifi_connect_to_elm327();
            }
        }

        if (!wifi_ok) {
            ESP_LOGE(TAG, "Failed to join ELM327 WiFi network");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(1200));
    }

    if (wifi_discover_tcp_endpoint()) {
        return true;
    }

    vTaskDelay(pdMS_TO_TICKS(800));
    return wifi_discover_tcp_endpoint();
}

bool wifi_connect_manual_network(const char *ssid, wifi_auth_mode_t authmode)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    ESP_LOGI(TAG, "Manual connect to: %s", ssid);

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    connected = false;

    if (wifi_ap_connected) {
        esp_wifi_disconnect();
        wifi_ap_connected = false;
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    authmode = wifi_normalize_authmode(authmode);
    if (!wifi_associate_obd_ap(ssid, authmode)) {
        ESP_LOGE(TAG, "Manual WiFi association failed: %s", ssid);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));

    if (!wifi_discover_tcp_endpoint()) {
        vTaskDelay(pdMS_TO_TICKS(800));
        if (!wifi_discover_tcp_endpoint()) {
            ESP_LOGE(TAG, "Manual TCP/ELM327 connect failed: %s", ssid);
            esp_wifi_disconnect();
            wifi_ap_connected = false;
            return false;
        }
    }

    strncpy(g_settings.wifi_ssid, ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    g_settings.wifi_manual_mode = true;
    g_settings.wifi_authmode = (uint8_t)authmode;
    g_settings.preferred_connection = CONN_TYPE_WIFI;
    settings_save(&g_settings);

    ESP_LOGI(TAG, "Manual network saved: %s", ssid);
    return true;
}

void wifi_clear_manual_network(void)
{
    g_settings.wifi_manual_mode = false;
    g_settings.wifi_ssid[0] = '\0';
    g_settings.wifi_authmode = (uint8_t)WIFI_AUTH_WPA2_PSK;
    settings_save(&g_settings);
    ESP_LOGI(TAG, "Manual WiFi selection cleared, auto-scan enabled");
}

bool wifi_is_ap_connected(void)
{
    return wifi_ap_connected;
}

void wifi_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting WiFi...");

    if (sock >= 0) {
        close(sock);
        sock = -1;
    }

    connected = false;
    wifi_ap_connected = false;

    if (wifi_stack_ready) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }

    if (wifi_event_group != NULL) {
        xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT | WIFI_DISCONNECTED_BIT);
    }

    ESP_LOGI(TAG, "WiFi disconnected");
}

esp_err_t wifi_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (sock < 0 || !connected) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool single_byte_mode = (len == 1);
    const uint8_t expected_pid = single_byte_mode ? 0 : cmd[1];
    char cmd_str[64];
    int cmd_len;

    if (single_byte_mode) {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X\r", cmd[0]);
    } else {
        cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X%02X\r", cmd[0], cmd[1]);
    }

    wifi_flush_rx();

    if (send(sock, cmd_str, cmd_len, 0) < 0) {
        ESP_LOGE(TAG, "Failed to send command");
        connected = false;
        return ESP_FAIL;
    }

    int total_received = 0;
    int retries = 8;

    while (retries > 0 && total_received < (int)(*resp_len) - 1) {
        int received = recv(sock, (char *)resp + total_received,
                            (int)(*resp_len) - total_received - 1, 0);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                retries--;
                vTaskDelay(pdMS_TO_TICKS(15));
                continue;
            }
            connected = false;
            return ESP_FAIL;
        }
        if (received == 0) {
            connected = false;
            return ESP_ERR_INVALID_STATE;
        }

        total_received += received;
        resp[total_received] = '\0';

        if (total_received > 0 && resp[total_received - 1] == '>') {
            if (single_byte_mode) {
                break;
            }
            if (wifi_response_matches_pid((char *)resp, expected_pid)) {
                break;
            }
        }

        retries--;
    }

    if (total_received == 0) {
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }

    if (!single_byte_mode && !wifi_response_matches_pid((char *)resp, expected_pid)) {
        *resp_len = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }

    resp[total_received] = '\0';
    *resp_len = (size_t)total_received;
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return connected && wifi_ap_connected && sock >= 0;
}

esp_err_t wifi_get_status(void)
{
    if (!wifi_ap_connected) {
        return ESP_ERR_WIFI_NOT_STARTED;
    }
    if (!connected || sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t wifi_test_elm327_connection(uint8_t *version, size_t version_len)
{
    if (sock < 0 || !connected) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *version_cmd = "ATI\r";
    send(sock, version_cmd, strlen(version_cmd), 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t resp[128];
    int received = recv(sock, (char *)resp, sizeof(resp) - 1, 0);
    if (received <= 0) {
        return ESP_ERR_TIMEOUT;
    }

    resp[received] = '\0';
    ESP_LOGI(TAG, "ELM327 response: %s", (char *)resp);

    if (version != NULL && version_len > 0) {
        strncpy((char *)version, (char *)resp, version_len - 1);
        version[version_len - 1] = '\0';
    }

    if (wifi_response_looks_like_elm327((char *)resp, received)) {
        return ESP_OK;
    }

    return ESP_ERR_INVALID_RESPONSE;
}
