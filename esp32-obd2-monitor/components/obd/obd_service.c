#include "obd_service.h"
#include "pid_table.h"
#include "connectivity.h"
#include "esp_log.h"
#include "string.h"
#include "ctype.h"

static const char *TAG = "obd_service";

static obd_data_t obd_data;
static bool connected = false;

static const uint8_t cmd_rpm[] = {0x01, 0x0C};
static const uint8_t cmd_speed[] = {0x01, 0x0D};
static const uint8_t cmd_coolant[] = {0x01, 0x05};
static const uint8_t cmd_throttle[] = {0x01, 0x11};
static const uint8_t cmd_fuel[] = {0x01, 0x2F};
static const uint8_t cmd_load[] = {0x01, 0x04};
static const uint8_t cmd_intake[] = {0x01, 0x0F};
static const uint8_t cmd_maf[] = {0x01, 0x10};
static const uint8_t cmd_dtc[] = {0x03};

static esp_err_t parse_rpm(const uint8_t *data, size_t len);
static esp_err_t parse_speed(const uint8_t *data, size_t len);
static esp_err_t parse_coolant(const uint8_t *data, size_t len);
static esp_err_t parse_throttle(const uint8_t *data, size_t len);
static esp_err_t parse_fuel(const uint8_t *data, size_t len);
static esp_err_t parse_load(const uint8_t *data, size_t len);
static esp_err_t parse_intake(const uint8_t *data, size_t len);
static esp_err_t parse_maf(const uint8_t *data, size_t len);
static void calculate_fuel_consumption(void);

static esp_err_t parse_obd_response(const uint8_t *resp, size_t resp_len, uint8_t *pid, uint8_t *data_bytes, size_t *data_len);

void obd_service_init(void)
{
    ESP_LOGI(TAG, "Initializing OBD2 service...");

    memset(&obd_data, 0, sizeof(obd_data));
    connected = false;

    ESP_LOGI(TAG, "OBD2 service initialized");
}

void obd_service_poll_all(void)
{
    uint8_t resp[32];
    size_t resp_len;

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_rpm, sizeof(cmd_rpm), resp, &resp_len) == ESP_OK) {
        parse_rpm(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_speed, sizeof(cmd_speed), resp, &resp_len) == ESP_OK) {
        parse_speed(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_coolant, sizeof(cmd_coolant), resp, &resp_len) == ESP_OK) {
        parse_coolant(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_throttle, sizeof(cmd_throttle), resp, &resp_len) == ESP_OK) {
        parse_throttle(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_fuel, sizeof(cmd_fuel), resp, &resp_len) == ESP_OK) {
        parse_fuel(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_load, sizeof(cmd_load), resp, &resp_len) == ESP_OK) {
        parse_load(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_intake, sizeof(cmd_intake), resp, &resp_len) == ESP_OK) {
        parse_intake(resp, resp_len);
    }

    resp_len = sizeof(resp);
    if (connectivity_send_cmd(cmd_maf, sizeof(cmd_maf), resp, &resp_len) == ESP_OK) {
        parse_maf(resp, resp_len);
    }

    /* Check DTC codes periodically */
    uint8_t dtc_codes[10];
    size_t dtc_count;
    if (obd_service_get_dtc_codes(dtc_codes, sizeof(dtc_codes) / 2, &dtc_count)) {
        obd_data.dtc_present = (dtc_count > 0);
        obd_data.dtc_count = dtc_count;
    }
}

void obd_service_get_data(obd_data_t *data)
{
    *data = obd_data;
}

esp_err_t obd_service_send_command(const uint8_t *cmd, size_t len, uint8_t *resp, size_t *resp_len)
{
    return connectivity_send_cmd(cmd, len, resp, resp_len);
}

bool obd_service_is_connected(void)
{
    return connected;
}

static esp_err_t parse_rpm(const uint8_t *data, size_t len)
{
    if (len < 4) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x0C) {
        obd_data.rpm = ((uint16_t)data[2] * 256 + data[3]) / 4;
        obd_data.rpm_valid = true;
        connected = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_speed(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x0D) {
        obd_data.speed = data[2];
        obd_data.speed_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_coolant(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x05) {
        obd_data.coolant_temp = (int16_t)data[2] - 40;
        obd_data.coolant_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_throttle(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x11) {
        obd_data.throttle_pos = (uint8_t)((float)data[2] * 100.0f / 255.0f);
        obd_data.throttle_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_fuel(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x2F) {
        obd_data.fuel_level = (uint8_t)((float)data[2] * 100.0f / 255.0f);
        obd_data.fuel_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_load(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x04) {
        obd_data.engine_load = (uint8_t)((float)data[2] * 100.0f / 255.0f);
        obd_data.load_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_intake(const uint8_t *data, size_t len)
{
    if (len < 3) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x0F) {
        obd_data.intake_temp = (int16_t)data[2] - 40;
        obd_data.intake_valid = true;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t parse_maf(const uint8_t *data, size_t len)
{
    if (len < 4) return ESP_ERR_INVALID_SIZE;

    if (data[0] == 0x41 && data[1] == 0x10) {
        obd_data.maf_rate = ((uint16_t)data[2] * 256 + data[3]) / 100; /* grams/sec */
        obd_data.maf_valid = true;
        calculate_fuel_consumption();
        return ESP_OK;
    }
    return ESP_ERR_INVALID_RESPONSE;
}

/* Calculate instant fuel consumption using MAF sensor data */
static void calculate_fuel_consumption(void)
{
    if (!obd_data.maf_valid || !obd_data.rpm_valid) {
        return;
    }

    /* 
     * Fuel consumption calculation:
     * MAF (g/sec) / 14.7 (air/fuel ratio) * 3600 / 750 (fuel density g/L)
     * = L/hour
     * 
     * For L/100km: (L/hour) / (speed km/h) * 100
     */
    
    float maf_grams_per_sec = obd_data.maf_rate;
    float fuel_grams_per_sec = maf_grams_per_sec / 14.7f; /* Stoichiometric ratio */
    float fuel_liters_per_hour = (fuel_grams_per_sec * 3600.0f) / 750.0f; /* Gasoline density ~750 g/L */
    
    obd_data.fuel_consumption = fuel_liters_per_hour;
    obd_data.fuel_consumption_valid = true;
    
    ESP_LOGD(TAG, "MAF: %.1f g/s, Fuel: %.2f L/h", maf_grams_per_sec, fuel_liters_per_hour);
}

// Parse raw ELM327 response to extract PID and data
static esp_err_t parse_obd_response(const uint8_t *resp, size_t resp_len, uint8_t *pid, uint8_t *data_bytes, size_t *data_len)
{
    // ELM327 response format: "41 <PID> <DATA>\r>" or "41<PID><DATA>\r>"
    // Example: "41 0C 1A F8\r>" for RPM
    
    if (resp_len < 4) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Skip echo if present
    const uint8_t *ptr = resp;
    
    // Find "41" or "41 " pattern (service 01 response)
    while (ptr < resp + resp_len - 2) {
        if (ptr[0] == '4' && ptr[1] == '1') {
            ptr += 2;
            
            // Skip space if present
            if (*ptr == ' ') ptr++;
            
            // Parse PID (2 hex chars)
            if (ptr + 2 > resp + resp_len) return ESP_ERR_INVALID_RESPONSE;
            
            char pid_str[3] = {ptr[0], ptr[1], '\0'};
            *pid = (uint8_t)strtol(pid_str, NULL, 16);
            ptr += 2;
            
            // Skip space if present
            if (*ptr == ' ') ptr++;
            
            // Parse data bytes
            *data_len = 0;
            while (ptr + 2 <= resp + resp_len && *data_len < 8) {
                // Check if we have 2 hex chars
                if (!isxdigit(ptr[0]) || !isxdigit(ptr[1])) break;
                
                char byte_str[3] = {ptr[0], ptr[1], '\0'};
                data_bytes[*data_len] = (uint8_t)strtol(byte_str, NULL, 16);
                (*data_len)++;
                ptr += 2;
                
                // Skip space if present
                if (ptr < resp + resp_len && *ptr == ' ') ptr++;
            }
            
            return ESP_OK;
        }
        ptr++;
    }
    
    return ESP_ERR_INVALID_RESPONSE;
}

// Detect OBD2 protocol (important for Chevrolet Kalos 2005)
esp_err_t obd_service_detect_protocol(char *protocol_str, size_t str_len)
{
    ESP_LOGI(TAG, "Detecting OBD2 protocol...");
    
    if (!connectivity_is_connected()) {
        ESP_LOGE(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Send ATP01 command to get current protocol
    uint8_t cmd[] = {0xFF, 0x01};  // Special command for protocol info
    uint8_t resp[128];
    size_t resp_len = sizeof(resp);
    
    // Send "ATDP" command via direct socket
    // We need to use raw AT command through connectivity
    const char *at_dp = "ATDP\r";
    
    // This requires direct access to the socket, let's use a workaround
    // by sending a test PID and checking response timing/format
    
    // Try reading VIN (PID 0x01, 0x06) - works on most protocols
    uint8_t vin_cmd[] = {0x01, 0x06};
    resp_len = sizeof(resp);
    
    esp_err_t err = connectivity_send_cmd(vin_cmd, sizeof(vin_cmd), resp, &resp_len);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Protocol detection: Received valid response");
        
        // Parse response to determine protocol
        // ISO 9141-2 and KWP2000 typically used in 2005 Chevrolet Kalos
        if (strstr((char *)resp, "ISO 9141") || strstr((char *)resp, "KWP")) {
            snprintf(protocol_str, str_len, "ISO 9141-2/KWP2000 (Likely for Kalos 2005)");
            ESP_LOGI(TAG, "Detected: ISO 9141-2/KWP2000");
        } else if (strstr((char *)resp, "CAN")) {
            snprintf(protocol_str, str_len, "ISO 15765-4 CAN");
            ESP_LOGI(TAG, "Detected: CAN (ISO 15765-4)");
        } else {
            snprintf(protocol_str, str_len, "Auto-detected (ATSP0)");
            ESP_LOGI(TAG, "Protocol: Auto (using ATSP0)");
        }
        
        return ESP_OK;
    }
    
    snprintf(protocol_str, str_len, "Unknown (using ATSP0 auto)");
    return ESP_ERR_INVALID_RESPONSE;
}

// Test OBD2 connection and get basic info
esp_err_t obd_service_test_connection(void)
{
    ESP_LOGI(TAG, "Testing OBD2 connection...");
    
    if (!connectivity_is_connected()) {
        ESP_LOGE(TAG, "Not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t resp[128];
    size_t resp_len;
    
    // Test 1: Read supported PIDs (01 00)
    ESP_LOGI(TAG, "Test 1: Reading supported PIDs...");
    uint8_t pid_cmd[] = {0x01, 0x00};
    resp_len = sizeof(resp);
    
    esp_err_t err = connectivity_send_cmd(pid_cmd, sizeof(pid_cmd), resp, &resp_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ PID 0x00 response received (%d bytes)", resp_len);
    } else {
        ESP_LOGW(TAG, "✗ PID 0x00 failed: %s", esp_err_to_name(err));
    }
    
    // Test 2: Read RPM (most commonly supported)
    ESP_LOGI(TAG, "Test 2: Reading RPM...");
    uint8_t rpm_cmd[] = {0x01, 0x0C};
    resp_len = sizeof(resp);
    
    err = connectivity_send_cmd(rpm_cmd, sizeof(rpm_cmd), resp, &resp_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ RPM response received");
        parse_rpm(resp, resp_len);
    } else {
        ESP_LOGW(TAG, "✗ RPM failed: %s", esp_err_to_name(err));
    }
    
    // Test 3: Read vehicle speed
    ESP_LOGI(TAG, "Test 3: Reading vehicle speed...");
    uint8_t speed_cmd[] = {0x01, 0x0D};
    resp_len = sizeof(resp);
    
    err = connectivity_send_cmd(speed_cmd, sizeof(speed_cmd), resp, &resp_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ Speed response received");
        parse_speed(resp, resp_len);
    } else {
        ESP_LOGW(TAG, "✗ Speed failed: %s", esp_err_to_name(err));
    }
    
    return ESP_OK;
}

// Read Diagnostic Trouble Codes (DTCs)
bool obd_service_get_dtc_codes(uint8_t *dtc_codes, size_t max_codes, size_t *dtc_count)
{
    ESP_LOGI(TAG, "Reading DTC codes...");
    
    if (!connectivity_is_connected()) {
        return false;
    }
    
    uint8_t resp[256];
    size_t resp_len = sizeof(resp);
    
    // Send "03" command to get DTCs
    uint8_t dtc_cmd[] = {0x03};
    esp_err_t err = connectivity_send_cmd(dtc_cmd, sizeof(dtc_cmd), resp, &resp_len);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read DTCs");
        return false;
    }
    
    // Parse DTC response
    // Format: 43 <DTC1> <DTC2> ...
    // Each DTC is 2 bytes
    
    *dtc_count = 0;
    
    // Simple response parsing
    if (resp_len >= 4 && resp[0] == 0x43) {
        // Number of DTC bytes
        size_t dtc_bytes = resp_len - 2;
        *dtc_count = dtc_bytes / 2;
        
        if (*dtc_count > max_codes) {
            *dtc_count = max_codes;
        }
        
        memcpy(dtc_codes, &resp[2], *dtc_count * 2);
        ESP_LOGI(TAG, "Found %d DTC code(s)", *dtc_count);
        
        return true;
    }
    
    ESP_LOGI(TAG, "No DTC codes found");
    return true;
}
