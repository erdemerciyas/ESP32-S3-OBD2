#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include "app.h"

static const char *TAG = "wifi";

static bool connected = false;
static int sock = -1;
static bool wifi_ap_connected = false;

extern app_settings_t g_settings;

// Common ELM327 SSIDs to search for
static const char *elm327_ssids[] = {"OBDII", "WiFi-ELM327", "ELM327", "OBD2", "ELM-327"};
static const int elm327_ssid_count = sizeof(elm327_ssids) / sizeof(elm327_ssids[0]);

// Event group for WiFi connection synchronization
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_DISCONNECTED_BIT = BIT1;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "WiFi connected to AP");
            wifi_ap_connected = true;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "WiFi disconnected from AP");
            wifi_ap_connected = false;
            connected = false;
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool wifi_is_elm327_ssid(const char *ssid)
{
    if (ssid == NULL) return false;

    for (int i = 0; i < elm327_ssid_count; i++) {
        if (strcmp(ssid, elm327_ssids[i]) == 0) {
            return true;
        }
    }
    return false;
}

int wifi_scan_elm327_networks(wifi_ap_info_t *ap_list, int max_count)
{
    ESP_LOGI(TAG, "Scanning for ELM327 WiFi networks...");

    if (!wifi_ap_connected) {
        // Initialize WiFi in station mode for scanning
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
        esp_event_handler_instance_t instance;
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    // Get scan results
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Found %d networks", ap_count);

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP records");
        return 0;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    // Filter for ELM327 networks
    int found_count = 0;
    for (int i = 0; i < ap_count && found_count < max_count; i++) {
        if (wifi_is_elm327_ssid((char *)ap_records[i].ssid)) {
            strncpy(ap_list[found_count].ssid, (char *)ap_records[i].ssid, sizeof(ap_list[found_count].ssid) - 1);
            ap_list[found_count].ssid[sizeof(ap_list[found_count].ssid) - 1] = '\0';
            ap_list[found_count].rssi = ap_records[i].rssi;
            ESP_LOGI(TAG, "Found ELM327: %s (RSSI: %d)", ap_list[found_count].ssid, ap_list[found_count].rssi);
            found_count++;
        }
    }

    free(ap_records);

    if (found_count == 0) {
        ESP_LOGW(TAG, "No ELM327 WiFi networks found");
    } else {
        ESP_LOGI(TAG, "Found %d ELM327 network(s)", found_count);
    }

    return found_count;
}

bool wifi_connect_to_elm327(void)
{
    ESP_LOGI(TAG, "Attempting to connect to ELM327 WiFi adapter...");

    // First, try to scan and find ELM327 networks
    wifi_ap_info_t ap_list[10];
    int found = wifi_scan_elm327_networks(ap_list, 10);

    if (found == 0) {
        ESP_LOGW(TAG, "No ELM327 networks found, trying configured SSID...");
        
        // Fall back to configured SSID if provided
        if (strlen(g_settings.wifi_ssid) > 0) {
            return wifi_connect(g_settings.wifi_ssid, g_settings.wifi_password);
        }
        
        // Try default ELM327 SSID
        return wifi_connect("OBDII", ELM327_DEFAULT_PASSWORD);
    }

    // Sort by RSSI (strongest signal first) - simple bubble sort
    for (int i = 0; i < found - 1; i++) {
        for (int j = 0; j < found - i - 1; j++) {
            if (ap_list[j].rssi < ap_list[j + 1].rssi) {
                wifi_ap_info_t temp = ap_list[j];
                ap_list[j] = ap_list[j + 1];
                ap_list[j + 1] = temp;
            }
        }
    }

    // Try to connect to the strongest network
    ESP_LOGI(TAG, "Connecting to strongest ELM327: %s (RSSI: %d)", ap_list[0].ssid, ap_list[0].rssi);
    return wifi_connect(ap_list[0].ssid, ELM327_DEFAULT_PASSWORD);
}

bool wifi_connect(const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    if (strlen(ssid) == 0) {
        ESP_LOGW(TAG, "Empty SSID, attempting ELM327 auto-detection");
        return wifi_connect_to_elm327();
    }

    // Create event group if not created
    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
    }

    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection with timeout
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Successfully connected to WiFi: %s", ssid);
        wifi_ap_connected = true;
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s (timeout)", ssid);
        return false;
    }
}

bool wifi_connect_to_obd_adapter(void)
{
    ESP_LOGI(TAG, "Connecting to OBD2 WiFi adapter at %s:%d",
             g_settings.obd_adapter_ip, g_settings.obd_adapter_port);

    // First ensure we're connected to the WiFi network
    if (!wifi_ap_connected) {
        ESP_LOGI(TAG, "Not connected to WiFi network, attempting connection...");
        
        // Try to connect to ELM327 WiFi
        if (!wifi_connect_to_elm327()) {
            ESP_LOGE(TAG, "Failed to connect to ELM327 WiFi network");
            return false;
        }
        
        // Wait a bit for network to stabilize
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed: %d", sock);
        return false;
    }

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(g_settings.obd_adapter_port),
    };

    if (inet_pton(AF_INET, g_settings.obd_adapter_ip, &dest_addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid address: %s", g_settings.obd_adapter_ip);
        close(sock);
        sock = -1;
        return false;
    }

    // Set connection timeout
    struct timeval timeout = {
        .tv_sec = 5,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect to ELM327 adapter
    ESP_LOGI(TAG, "Connecting to %s:%d...", g_settings.obd_adapter_ip, g_settings.obd_adapter_port);
    if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "TCP connection failed: %d", errno);
        close(sock);
        sock = -1;
        return false;
    }

    connected = true;
    ESP_LOGI(TAG, "TCP connected to OBD2 WiFi adapter");

    // Initialize ELM327 with AT commands
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Sending AT commands to initialize ELM327...");
    
    // Reset ELM327
    uint8_t reset_cmd[] = "ATZ\r";
    send(sock, reset_cmd, sizeof(reset_cmd) - 1, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Echo off
    uint8_t echo_cmd[] = "ATE0\r";
    send(sock, echo_cmd, sizeof(echo_cmd) - 1, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Linefeeds off
    uint8_t lf_cmd[] = "ATL0\r";
    send(sock, lf_cmd, sizeof(lf_cmd) - 1, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Set protocol to auto-search (important for Chevrolet Kalos 2005)
    uint8_t proto_cmd[] = "ATSP0\r";
    send(sock, proto_cmd, sizeof(proto_cmd) - 1, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "ELM327 initialization complete");

    return true;
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

    esp_wifi_disconnect();
    esp_wifi_stop();
    
    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    ESP_LOGI(TAG, "WiFi disconnected");
}

esp_err_t wifi_send_cmd(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    if (sock < 0 || !connected) {
        return ESP_ERR_INVALID_STATE;
    }

    // Convert OBD2 command to ELM327 format
    // cmd[0] = service (e.g., 0x01), cmd[1] = PID (e.g., 0x0C)
    char cmd_str[64];
    int cmd_len = snprintf(cmd_str, sizeof(cmd_str), "%02X%02X\r", cmd[0], cmd[1]);

    ESP_LOGD(TAG, "Sending OBD2 cmd: %s", cmd_str);

    // Send command
    int sent = send(sock, cmd_str, cmd_len, 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send command");
        return ESP_FAIL;
    }

    // Receive response with retry
    int total_received = 0;
    int retries = 3;
    
    while (retries > 0) {
        int received = recv(sock, (char *)resp + total_received, *resp_len - total_received - 1, 0);
        
        if (received < 0) {
            // Timeout, retry
            retries--;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        } else if (received == 0) {
            // Connection closed
            ESP_LOGW(TAG, "Connection closed by adapter");
            connected = false;
            return ESP_ERR_INVALID_STATE;
        }
        
        total_received += received;
        resp[total_received] = '\0';
        
        // Check if we got a complete response (ends with '>')
        if (resp[total_received - 1] == '>') {
            break;
        }
        
        retries--;
    }

    if (total_received == 0) {
        ESP_LOGW(TAG, "No response received");
        *resp_len = 0;
        return ESP_ERR_TIMEOUT;
    }

    resp[total_received] = '\0';
    *resp_len = total_received;

    ESP_LOGD(TAG, "Received %d bytes: %s", total_received, resp);

    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return connected && wifi_ap_connected;
}

esp_err_t wifi_get_status(void)
{
    if (!wifi_ap_connected) {
        return ESP_ERR_WIFI_NOT_STARTED;
    }
    
    if (!connected) {
        return ESP_ERR_NOT_CONNECTED;
    }
    
    if (sock < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

esp_err_t wifi_test_elm327_connection(uint8_t *version, size_t version_len)
{
    ESP_LOGI(TAG, "Testing ELM327 connection...");

    if (sock < 0 || !connected) {
        ESP_LOGE(TAG, "Not connected to ELM327");
        return ESP_ERR_INVALID_STATE;
    }

    // Send ATI command to get ELM327 version
    const char *version_cmd = "ATI\r";
    send(sock, version_cmd, strlen(version_cmd), 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint8_t resp[128];
    int received = recv(sock, (char *)resp, sizeof(resp) - 1, 0);
    
    if (received <= 0) {
        ESP_LOGE(TAG, "No response from ELM327");
        return ESP_ERR_TIMEOUT;
    }

    resp[received] = '\0';
    ESP_LOGI(TAG, "ELM327 Response: %s", resp);

    // Copy version string
    if (version != NULL && version_len > 0) {
        strncpy((char *)version, (char *)resp, version_len - 1);
        version[version_len - 1] = '\0';
    }

    // Check if response contains "ELM327"
    if (strstr((char *)resp, "ELM327") != NULL) {
        ESP_LOGI(TAG, "✓ ELM327 adapter verified");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "✗ Unexpected response, may not be ELM327");
    return ESP_ERR_INVALID_RESPONSE;
}
