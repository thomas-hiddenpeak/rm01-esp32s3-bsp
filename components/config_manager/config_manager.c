/**
 * @file config_manager.c
 * @brief ESP32S3 Configuration Manager Component Implementation
 */

#include "config_manager.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "string.h"
#include "hardware_config.h"

// Internal includes for applying configuration
#include "device_interface.h"
#include "ethernet_interface.h"
#include "hardware_control.h"

static const char *TAG = "CONFIG_MANAGER";

// NVS configuration
#define CONFIG_NVS_NAMESPACE    "device_config"
#define CONFIG_NVS_KEY          "complete_cfg"
#define CONFIG_VERSION          1

// Internal state
static complete_config_t s_current_config;
static bool s_initialized = false;
static config_event_callback_t s_event_callback = NULL;

// Internal function prototypes
static uint32_t calculate_config_checksum(const complete_config_t *config);
static void trigger_config_event(config_event_t event, const char *message);
static esp_err_t apply_fan_config(const fan_config_t *config);
static esp_err_t apply_led_config(const led_config_t *config);
static esp_err_t apply_ethernet_config(const ethernet_config_t *config);
static esp_err_t apply_usb_mux_config(const usb_mux_config_t *config);
static esp_err_t apply_web_server_config(const web_server_config_t *config);

// ==================== Main API Functions ====================

esp_err_t config_manager_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Configuration manager already initialized");
        return ESP_OK;
    }

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Load default configuration
    memset(&s_current_config, 0, sizeof(complete_config_t));
    
    // Initialize each component with default values
    s_current_config.fan = (fan_config_t)DEFAULT_FAN_CONFIG();
    s_current_config.led = (led_config_t)DEFAULT_LED_CONFIG();
    s_current_config.ethernet = (ethernet_config_t)DEFAULT_ETHERNET_CONFIG();
    s_current_config.dhcp = (dhcp_config_t)DEFAULT_DHCP_CONFIG();
    memset(&s_current_config.dhcp_reservations, 0, sizeof(dhcp_reservation_config_t));
    s_current_config.gateway = (gateway_config_t)DEFAULT_GATEWAY_CONFIG();
    s_current_config.usb_mux = (usb_mux_config_t)DEFAULT_USB_MUX_CONFIG();
    s_current_config.web = (web_server_config_t)DEFAULT_WEB_SERVER_CONFIG();
    s_current_config.system = (system_config_t)DEFAULT_SYSTEM_CONFIG();
    s_current_config.config_version = 1;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);

    s_initialized = true;
    ESP_LOGI(TAG, "Configuration manager initialized with defaults");
    
    trigger_config_event(CONFIG_EVENT_RESET, "Initialized with default configuration");
    return ESP_OK;
}

esp_err_t config_manager_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_initialized = false;
    s_event_callback = NULL;
    ESP_LOGI(TAG, "Configuration manager deinitialized");
    return ESP_OK;
}

esp_err_t config_manager_save(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting configuration save to NVS...");
    
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", CONFIG_NVS_NAMESPACE, esp_err_to_name(ret));
        return ret;
    }

    // Update checksum and version before saving
    s_current_config.config_version = CONFIG_VERSION;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);

    ESP_LOGI(TAG, "Saving config blob: namespace='%s', key='%s', size=%zu, version=%lu", 
             CONFIG_NVS_NAMESPACE, CONFIG_NVS_KEY, sizeof(complete_config_t), s_current_config.config_version);

    // Save complete configuration as blob
    ret = nvs_set_blob(nvs_handle, CONFIG_NVS_KEY, &s_current_config, sizeof(complete_config_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Configuration saved to NVS successfully (size=%zu, checksum=0x%08lx)", 
                     sizeof(complete_config_t), s_current_config.checksum);
            trigger_config_event(CONFIG_EVENT_SAVED, "Configuration saved to NVS");
        } else {
            ESP_LOGE(TAG, "Failed to commit configuration to NVS: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGE(TAG, "Failed to save configuration to NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t config_manager_load(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting configuration load from NVS...");
    ESP_LOGI(TAG, "Looking for namespace='%s', key='%s'", CONFIG_NVS_NAMESPACE, CONFIG_NVS_KEY);

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS namespace '%s' not found - first time run, will use defaults", CONFIG_NVS_NAMESPACE);
        } else {
            ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", CONFIG_NVS_NAMESPACE, esp_err_to_name(ret));
        }
        return ret;
    }

    size_t required_size = sizeof(complete_config_t);
    complete_config_t loaded_config;
    
    ESP_LOGI(TAG, "Attempting to load config blob of size %zu", required_size);
    ret = nvs_get_blob(nvs_handle, CONFIG_NVS_KEY, &loaded_config, &required_size);
    nvs_close(nvs_handle);

    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "No saved configuration found in key '%s', using defaults", CONFIG_NVS_KEY);
            return ESP_ERR_NOT_FOUND;
        } else {
            ESP_LOGE(TAG, "Failed to load configuration from NVS key '%s': %s", CONFIG_NVS_KEY, esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "Config blob loaded successfully, size=%zu, version=%lu", required_size, loaded_config.config_version);

    // Validate loaded configuration
    if (loaded_config.config_version != CONFIG_VERSION) {
        ESP_LOGW(TAG, "Configuration version mismatch (loaded: %lu, expected: %d), using defaults", 
                 loaded_config.config_version, CONFIG_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }

    uint32_t calculated_checksum = calculate_config_checksum(&loaded_config);
    if (calculated_checksum != loaded_config.checksum) {
        ESP_LOGE(TAG, "Configuration checksum mismatch, data may be corrupted");
        trigger_config_event(CONFIG_EVENT_ERROR, "Configuration data corrupted");
        return ESP_ERR_INVALID_CRC;
    }

    // Configuration is valid, update current config
    s_current_config = loaded_config;
    ESP_LOGI(TAG, "Configuration loaded from NVS successfully");
    trigger_config_event(CONFIG_EVENT_LOADED, "Configuration loaded from NVS");
    
    return ESP_OK;
}

esp_err_t config_manager_reset_to_defaults(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_current_config, 0, sizeof(complete_config_t));
    
    // Initialize each component with default values
    s_current_config.fan = (fan_config_t)DEFAULT_FAN_CONFIG();
    s_current_config.led = (led_config_t)DEFAULT_LED_CONFIG();
    s_current_config.ethernet = (ethernet_config_t)DEFAULT_ETHERNET_CONFIG();
    s_current_config.dhcp = (dhcp_config_t)DEFAULT_DHCP_CONFIG();
    memset(&s_current_config.dhcp_reservations, 0, sizeof(dhcp_reservation_config_t));
    s_current_config.gateway = (gateway_config_t)DEFAULT_GATEWAY_CONFIG();
    s_current_config.usb_mux = (usb_mux_config_t)DEFAULT_USB_MUX_CONFIG();
    s_current_config.web = (web_server_config_t)DEFAULT_WEB_SERVER_CONFIG();
    s_current_config.system = (system_config_t)DEFAULT_SYSTEM_CONFIG();
    s_current_config.config_version = 1;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);

    ESP_LOGI(TAG, "Configuration reset to factory defaults");
    trigger_config_event(CONFIG_EVENT_RESET, "Configuration reset to defaults");
    
    return ESP_OK;
}

esp_err_t config_manager_clear(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_key(nvs_handle, CONFIG_NVS_KEY);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Configuration cleared from NVS");
        }
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No configuration found in NVS to clear");
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to clear configuration from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t config_manager_apply_config(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    // Apply fan configuration
    ret = apply_fan_config(&s_current_config.fan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply fan configuration: %s", esp_err_to_name(ret));
    }

    // Apply LED configuration
    ret = apply_led_config(&s_current_config.led);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply LED configuration: %s", esp_err_to_name(ret));
    }

    // Apply ethernet configuration
    ret = apply_ethernet_config(&s_current_config.ethernet);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply ethernet configuration: %s", esp_err_to_name(ret));
    }

    // Apply USB MUX configuration
    ret = apply_usb_mux_config(&s_current_config.usb_mux);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply USB MUX configuration: %s", esp_err_to_name(ret));
    }

    // Apply web server configuration (if auto_start is enabled)
    ret = apply_web_server_config(&s_current_config.web);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply web server configuration: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Configuration applied to all subsystems");
    return ESP_OK;
}

// ==================== Configuration Getters ====================

const complete_config_t* config_manager_get_config(void)
{
    return s_initialized ? &s_current_config : NULL;
}

const fan_config_t* config_manager_get_fan_config(void)
{
    return s_initialized ? &s_current_config.fan : NULL;
}

const led_config_t* config_manager_get_led_config(void)
{
    return s_initialized ? &s_current_config.led : NULL;
}

const usb_mux_config_t* config_manager_get_usb_mux_config(void)
{
    return s_initialized ? &s_current_config.usb_mux : NULL;
}

const ethernet_config_t* config_manager_get_ethernet_config(void)
{
    return s_initialized ? &s_current_config.ethernet : NULL;
}

const dhcp_config_t* config_manager_get_dhcp_config(void)
{
    return s_initialized ? &s_current_config.dhcp : NULL;
}

const gateway_config_t* config_manager_get_gateway_config(void)
{
    return s_initialized ? &s_current_config.gateway : NULL;
}

const system_config_t* config_manager_get_system_config(void)
{
    return s_initialized ? &s_current_config.system : NULL;
}

const web_server_config_t* config_manager_get_web_server_config(void)
{
    return s_initialized ? &s_current_config.web : NULL;
}

// ==================== Configuration Setters ====================

esp_err_t config_manager_set_fan_config(const fan_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate fan configuration
    if (config->default_speed_on > 100 || config->default_speed_off > 100) {
        ESP_LOGE(TAG, "Invalid fan speed values (must be 0-100)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.fan = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Fan configuration updated");
    return ESP_OK;
}

esp_err_t config_manager_set_led_config(const led_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate LED configuration
    if (config->default_brightness > 100) {
        ESP_LOGE(TAG, "Invalid LED brightness (must be 0-100)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "LED configuration updated");
    return ESP_OK;
}

esp_err_t config_manager_set_ethernet_config(const ethernet_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // 对于uint32_t类型的IP地址，基本验证足够
    s_current_config.ethernet = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Ethernet configuration updated");
    return ESP_OK;
}

esp_err_t config_manager_set_dhcp_config(const dhcp_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // 基本验证
    if (config->lease_time_hours == 0 || config->lease_time_hours > 168) { // Max 1 week
        ESP_LOGE(TAG, "Invalid DHCP lease time (must be 1-168 hours)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.dhcp = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "DHCP configuration updated");
    return ESP_OK;
}

const dhcp_reservation_config_t* config_manager_get_dhcp_reservations_config(void)
{
    if (!s_initialized) {
        return NULL;
    }
    return &s_current_config.dhcp_reservations;
}

esp_err_t config_manager_set_dhcp_reservations_config(const dhcp_reservation_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // 验证保留配置
    if (config->reservation_count > 10) {
        ESP_LOGE(TAG, "Invalid DHCP reservations count (max 10)");
        return ESP_ERR_INVALID_ARG;
    }

    // 验证每个保留配置的有效性
    for (int i = 0; i < config->reservation_count; i++) {
        const dhcp_ip_reservation_t *res = &config->reservations[i];
        
        // 检查MAC地址不能全为0
        bool mac_is_zero = true;
        for (int j = 0; j < 6; j++) {
            if (res->mac_addr[j] != 0) {
                mac_is_zero = false;
                break;
            }
        }
        if (mac_is_zero) {
            ESP_LOGE(TAG, "Invalid MAC address in reservation %d (all zeros)", i);
            return ESP_ERR_INVALID_ARG;
        }

        // 检查IP地址不能为0
        if (res->reserved_ip == 0) {
            ESP_LOGE(TAG, "Invalid IP address in reservation %d (zero)", i);
            return ESP_ERR_INVALID_ARG;
        }
    }

    s_current_config.dhcp_reservations = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "DHCP reservations configuration updated (%d reservations)", 
             config->reservation_count);
    return ESP_OK;
}

esp_err_t config_manager_set_gateway_config(const gateway_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.gateway = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Gateway configuration updated");
    return ESP_OK;
}

esp_err_t config_manager_set_system_config(const system_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate system configuration
    if (config->save_interval_ms < 10000) { // Minimum 10 seconds
        ESP_LOGE(TAG, "Invalid save interval (minimum 10000ms)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.system = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "System configuration updated");
    return ESP_OK;
}

esp_err_t config_manager_set_web_server_config(const web_server_config_t *config)
{
    if (!s_initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate web server configuration
    if (config->port == 0) {
        ESP_LOGE(TAG, "Invalid port number (must be 1-65535)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(config->document_root) == 0) {
        ESP_LOGE(TAG, "Document root cannot be empty");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.web = *config;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Web server configuration updated");
    return ESP_OK;
}

// ==================== Individual Parameter Functions ====================

esp_err_t config_manager_set_fan_speed(uint8_t speed_on, uint8_t speed_off)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (speed_on > 100 || speed_off > 100) {
        ESP_LOGE(TAG, "Invalid fan speed values (must be 0-100)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.fan.default_speed_on = speed_on;
    s_current_config.fan.default_speed_off = speed_off;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Fan speed configuration updated: on=%d%%, off=%d%%", speed_on, speed_off);
    return ESP_OK;
}

esp_err_t config_manager_set_led_defaults(uint8_t brightness, 
                                          led_color_t board_color,
                                          led_color_t touch_color)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        ESP_LOGE(TAG, "Invalid LED brightness (must be 0-100)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.default_brightness = brightness;
    s_current_config.led.board_led_color = board_color;
    s_current_config.led.touch_led_color = touch_color;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "LED defaults updated: brightness=%d%%, board_color=(%d,%d,%d), touch_color=(%d,%d,%d)",
             brightness, board_color.red, board_color.green, board_color.blue,
             touch_color.red, touch_color.green, touch_color.blue);
    return ESP_OK;
}

esp_err_t config_manager_set_matrix_config(uint8_t brightness, 
                                           const char *animation_name,
                                           bool auto_start,
                                           bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        ESP_LOGE(TAG, "Invalid matrix brightness (must be 0-100)");
        return ESP_ERR_INVALID_ARG;
    }

    if (!animation_name) {
        ESP_LOGE(TAG, "Animation name cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.matrix_brightness = brightness;
    s_current_config.led.matrix_auto_start = auto_start;
    s_current_config.led.matrix_enable = enable;
    
    // 安全复制动画名称
    strncpy(s_current_config.led.matrix_startup_animation, animation_name, 
            sizeof(s_current_config.led.matrix_startup_animation) - 1);
    s_current_config.led.matrix_startup_animation[sizeof(s_current_config.led.matrix_startup_animation) - 1] = '\0';
    
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Matrix config updated: brightness=%d%%, animation='%s', auto_start=%s, enable=%s",
             brightness, animation_name, auto_start ? "true" : "false", enable ? "true" : "false");
    return ESP_OK;
}

esp_err_t config_manager_get_matrix_config(uint8_t *brightness,
                                           char *animation_name,
                                           bool *auto_start,
                                           bool *enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!brightness || !animation_name || !auto_start || !enable) {
        return ESP_ERR_INVALID_ARG;
    }

    *brightness = s_current_config.led.matrix_brightness;
    *auto_start = s_current_config.led.matrix_auto_start;
    *enable = s_current_config.led.matrix_enable;
    
    strncpy(animation_name, s_current_config.led.matrix_startup_animation, 63);
    animation_name[63] = '\0';
    
    return ESP_OK;
}

esp_err_t config_manager_set_usb_mux_target(uint8_t target)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (target > 2) {
        ESP_LOGE(TAG, "Invalid USB MUX target (must be 0-2)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.usb_mux.default_target = target;
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "USB MUX target updated: %d", target);
    return ESP_OK;
}

esp_err_t config_manager_set_ethernet_ip_from_strings(const char *ip_addr, const char *gateway,
                                                      const char *netmask, const char *dns_server)
{
    if (!s_initialized || !ip_addr || !gateway || !netmask || !dns_server) {
        return ESP_ERR_INVALID_ARG;
    }

    // 将字符串IP地址转换为uint32_t格式 (主机字节序)
    int a, b, c, d;
    uint32_t ip, gw, mask, dns;
    
    if (sscanf(ip_addr, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        ip = (a << 24) | (b << 16) | (c << 8) | d;
    } else {
        ESP_LOGE(TAG, "Invalid IP address format: %s", ip_addr);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sscanf(gateway, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        gw = (a << 24) | (b << 16) | (c << 8) | d;
    } else {
        ESP_LOGE(TAG, "Invalid gateway address format: %s", gateway);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sscanf(netmask, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        mask = (a << 24) | (b << 16) | (c << 8) | d;
    } else {
        ESP_LOGE(TAG, "Invalid netmask format: %s", netmask);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sscanf(dns_server, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
        dns = (a << 24) | (b << 16) | (c << 8) | d;
    } else {
        ESP_LOGE(TAG, "Invalid DNS server format: %s", dns_server);
        return ESP_ERR_INVALID_ARG;
    }

    // 更新以太网配置
    s_current_config.ethernet.ip_addr = ip;
    s_current_config.ethernet.gateway = gw;
    s_current_config.ethernet.netmask = mask;
    s_current_config.ethernet.dns_server = dns;
    
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Ethernet IP configuration updated: IP=%s, Gateway=%s, Netmask=%s, DNS=%s",
             ip_addr, gateway, netmask, dns_server);
    
    // 立即同步配置到以太网接口
    esp_err_t sync_ret = ethernet_save_config_from_manager(&s_current_config.ethernet);
    if (sync_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to sync ethernet config to interface: %s", esp_err_to_name(sync_ret));
    } else {
        ESP_LOGI(TAG, "✅ 以太网配置已同步到接口");
    }
    
    return ESP_OK;
}

esp_err_t config_manager_set_dhcp_params(bool enable, const char *start_ip,
                                         const char *end_ip, uint8_t lease_time)
{
    if (!s_initialized || !start_ip || !end_ip) {
        return ESP_ERR_INVALID_ARG;
    }

    // 基本验证
    if (lease_time == 0 || lease_time > 168) { // Max 1 week
        ESP_LOGE(TAG, "Invalid DHCP lease time (must be 1-168 hours)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.dhcp.enable = enable;
    strncpy(s_current_config.dhcp.start_ip, start_ip, sizeof(s_current_config.dhcp.start_ip) - 1);
    strncpy(s_current_config.dhcp.end_ip, end_ip, sizeof(s_current_config.dhcp.end_ip) - 1);
    s_current_config.dhcp.lease_time_hours = lease_time;
    
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "DHCP parameters updated: enable=%s, start=%s, end=%s, lease=%dh",
             enable ? "true" : "false", start_ip, end_ip, lease_time);
    
    // 立即同步DHCP配置到以太网接口
    esp_err_t sync_ret = ethernet_set_dhcp_server(enable);
    if (sync_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to sync DHCP server enable to interface: %s", esp_err_to_name(sync_ret));
    } else {
        ESP_LOGI(TAG, "✅ DHCP服务器状态已同步到接口");
    }
    
    if (enable) {
        sync_ret = ethernet_set_dhcp_pool(start_ip, end_ip, lease_time);
        if (sync_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to sync DHCP pool to interface: %s", esp_err_to_name(sync_ret));
        } else {
            ESP_LOGI(TAG, "✅ DHCP池配置已同步到接口");
        }
    }
    
    return ESP_OK;
}

esp_err_t config_manager_set_gateway_params(bool enable, bool nat_enable, bool firewall_enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_current_config.gateway.enable = enable;
    s_current_config.gateway.nat_enable = nat_enable;
    s_current_config.gateway.firewall_enable = firewall_enable;
    
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Gateway parameters updated: enable=%s, NAT=%s, firewall=%s",
             enable ? "true" : "false", nat_enable ? "true" : "false", 
             firewall_enable ? "true" : "false");
    
    // 立即同步网关配置到以太网接口
    esp_err_t sync_ret = ethernet_set_gateway(enable);
    if (sync_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to sync gateway enable to interface: %s", esp_err_to_name(sync_ret));
    } else {
        ESP_LOGI(TAG, "✅ 网关状态已同步到接口");
    }
    
    return ESP_OK;
}

esp_err_t config_manager_set_web_server_params(const char *document_root, uint16_t port, bool auto_start)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!document_root || strlen(document_root) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (port == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    strncpy(s_current_config.web.document_root, document_root, sizeof(s_current_config.web.document_root) - 1);
    s_current_config.web.document_root[sizeof(s_current_config.web.document_root) - 1] = '\0';
    s_current_config.web.port = port;
    s_current_config.web.auto_start = auto_start;
    
    s_current_config.checksum = calculate_config_checksum(&s_current_config);
    
    ESP_LOGI(TAG, "Web server parameters updated: root=%s, port=%d, auto_start=%s",
             document_root, port, auto_start ? "true" : "false");
    return ESP_OK;
}

// ==================== Utility Functions ====================

esp_err_t config_manager_register_event_callback(config_event_callback_t callback)
{
    s_event_callback = callback;
    ESP_LOGI(TAG, "Configuration event callback registered");
    return ESP_OK;
}

void config_manager_print_config(void)
{
    if (!s_initialized) {
        printf("Configuration manager not initialized\n");
        return;
    }

    printf("\n=== Current System Configuration ===\n");
    printf("Configuration Version: %lu\n", s_current_config.config_version);
    
    printf("\n[Fan Configuration]\n");
    printf("  Default Speed On: %d%%\n", s_current_config.fan.default_speed_on);
    printf("  Default Speed Off: %d%%\n", s_current_config.fan.default_speed_off);
    printf("  Auto Control: %s\n", s_current_config.fan.auto_enable ? "Enabled" : "Disabled");
    printf("  Auto Temp Threshold: %d°C\n", s_current_config.fan.auto_temp_threshold);
    
    printf("\n[LED Configuration]\n");
    printf("  Default Brightness: %d%%\n", s_current_config.led.default_brightness);
    printf("  Board LED Color: RGB(%d,%d,%d)\n", 
           s_current_config.led.board_led_color.red,
           s_current_config.led.board_led_color.green,
           s_current_config.led.board_led_color.blue);
    printf("  Touch LED Color: RGB(%d,%d,%d)\n",
           s_current_config.led.touch_led_color.red,
           s_current_config.led.touch_led_color.green,
           s_current_config.led.touch_led_color.blue);
    printf("  Effects Enabled: %s\n", s_current_config.led.effect_enable ? "Yes" : "No");
    printf("  Rainbow Speed: %dms\n", s_current_config.led.rainbow_speed_ms);
    
    printf("\n[Ethernet Configuration]\n");
    printf("  IP Address: %d.%d.%d.%d\n", 
           (int)((s_current_config.ethernet.ip_addr >> 24) & 0xFF),
           (int)((s_current_config.ethernet.ip_addr >> 16) & 0xFF),
           (int)((s_current_config.ethernet.ip_addr >> 8) & 0xFF),
           (int)(s_current_config.ethernet.ip_addr & 0xFF));
    printf("  Gateway: %d.%d.%d.%d\n",
           (int)((s_current_config.ethernet.gateway >> 24) & 0xFF),
           (int)((s_current_config.ethernet.gateway >> 16) & 0xFF),
           (int)((s_current_config.ethernet.gateway >> 8) & 0xFF),
           (int)(s_current_config.ethernet.gateway & 0xFF));
    printf("  Netmask: %d.%d.%d.%d\n",
           (int)((s_current_config.ethernet.netmask >> 24) & 0xFF),
           (int)((s_current_config.ethernet.netmask >> 16) & 0xFF),
           (int)((s_current_config.ethernet.netmask >> 8) & 0xFF),
           (int)(s_current_config.ethernet.netmask & 0xFF));
    printf("  DNS Server: %d.%d.%d.%d\n",
           (int)((s_current_config.ethernet.dns_server >> 24) & 0xFF),
           (int)((s_current_config.ethernet.dns_server >> 16) & 0xFF),
           (int)((s_current_config.ethernet.dns_server >> 8) & 0xFF),
           (int)(s_current_config.ethernet.dns_server & 0xFF));
    printf("  DHCP Server: %s\n", s_current_config.ethernet.dhcp_server_enabled ? "Enabled" : "Disabled");
    printf("  Gateway Service: %s\n", s_current_config.ethernet.gateway_enabled ? "Enabled" : "Disabled");
    
    printf("\n[DHCP Server Configuration]\n");
    printf("  Enabled: %s\n", s_current_config.dhcp.enable ? "Yes" : "No");
    printf("  Start IP: %s\n", s_current_config.dhcp.start_ip);
    printf("  End IP: %s\n", s_current_config.dhcp.end_ip);
    printf("  Lease Time: %d hours\n", s_current_config.dhcp.lease_time_hours);
    printf("  Max Clients: %d\n", s_current_config.dhcp.max_clients);
    printf("  Auto Start: %s\n", s_current_config.dhcp.auto_start ? "Yes" : "No");
    
    printf("\n[Gateway Service Configuration]\n");
    printf("  Enabled: %s\n", s_current_config.gateway.enable ? "Yes" : "No");
    printf("  NAT Enabled: %s\n", s_current_config.gateway.nat_enable ? "Yes" : "No");
    printf("  Firewall Enabled: %s\n", s_current_config.gateway.firewall_enable ? "Yes" : "No");
    printf("  Auto Start: %s\n", s_current_config.gateway.auto_start ? "Yes" : "No");
    
    printf("\n[Web Server Configuration]\n");
    printf("  Document Root: %s\n", s_current_config.web.document_root);
    printf("  Port: %d\n", s_current_config.web.port);
    printf("  Auto Start: %s\n", s_current_config.web.auto_start ? "Yes" : "No");
    printf("  CORS Enabled: %s\n", s_current_config.web.enable_cors ? "Yes" : "No");
    printf("  Default Index: %s\n", s_current_config.web.default_index);
    
    printf("\n[System Configuration]\n");
    printf("  Auto Save Config: %s\n", s_current_config.system.auto_save_config ? "Yes" : "No");
    printf("  Save Interval: %lums\n", s_current_config.system.save_interval_ms);
    printf("  Startup Load Config: %s\n", s_current_config.system.startup_load_config ? "Yes" : "No");
    printf("  Debug Logging: %s\n", s_current_config.system.enable_debug_logging ? "Yes" : "No");
    
    printf("\nChecksum: 0x%08lX\n", s_current_config.checksum);
    printf("=======================================\n\n");
}

bool config_manager_validate_config(void)
{
    if (!s_initialized) {
        return false;
    }

    uint32_t calculated_checksum = calculate_config_checksum(&s_current_config);
    return (calculated_checksum == s_current_config.checksum);
}

size_t config_manager_get_config_size(void)
{
    return sizeof(complete_config_t);
}

bool config_manager_config_exists(void)
{
    if (!s_initialized) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t required_size = sizeof(complete_config_t);
    ret = nvs_get_blob(nvs_handle, CONFIG_NVS_KEY, NULL, &required_size);
    nvs_close(nvs_handle);

    return (ret == ESP_OK && required_size == sizeof(complete_config_t));
}

// ==================== Internal Helper Functions ====================

static uint32_t calculate_config_checksum(const complete_config_t *config)
{
    if (!config) {
        return 0;
    }

    // Simple CRC32-like checksum calculation
    uint32_t checksum = 0xFFFFFFFF;
    const uint8_t *data = (const uint8_t *)config;
    size_t len = sizeof(complete_config_t) - sizeof(config->checksum); // Exclude checksum field

    for (size_t i = 0; i < len; i++) {
        checksum ^= data[i];
        for (int j = 0; j < 8; j++) {
            checksum = (checksum >> 1) ^ (0xEDB88320 & (-(checksum & 1)));
        }
    }

    return ~checksum;
}

static void trigger_config_event(config_event_t event, const char *message)
{
    if (s_event_callback) {
        s_event_callback(event, message);
    }
}

static esp_err_t apply_fan_config(const fan_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Applying fan configuration: speed_on=%d%%, speed_off=%d%%, auto=%s",
             config->default_speed_on, config->default_speed_off,
             config->auto_enable ? "enabled" : "disabled");
    
    // Apply fan configuration using hardware control interface
    esp_err_t ret = ESP_OK;
    
    // Set fan to default ON speed if auto mode is disabled
    if (!config->auto_enable) {
        ret = fan_set_speed(config->default_speed_on);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set fan speed to %d%%: %s", 
                     config->default_speed_on, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Fan initialized to default ON speed: %d%%", config->default_speed_on);
    } else {
        ESP_LOGI(TAG, "Fan auto mode enabled, speed will be controlled by temperature");
    }
    
    return ESP_OK;
}

static esp_err_t apply_led_config(const led_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    // Apply LED configuration using hardware control interface
    ESP_LOGI(TAG, "Applying LED configuration: brightness=%d%%, board_color=(%d,%d,%d), touch_color=(%d,%d,%d)",
             config->default_brightness,
             config->board_led_color.red, config->board_led_color.green, config->board_led_color.blue,
             config->touch_led_color.red, config->touch_led_color.green, config->touch_led_color.blue);
    
    // Set board LED color and brightness
    ret = board_led_set_color(config->board_led_color);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set board LED color: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = board_led_set_brightness(config->default_brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set board LED brightness: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set touch LED color and brightness
    ret = touch_led_set_color(config->touch_led_color);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch LED color: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = touch_led_set_brightness(config->default_brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch LED brightness: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LED configuration applied successfully");
    return ESP_OK;
}

static esp_err_t apply_ethernet_config(const ethernet_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    // Apply ethernet configuration using ethernet interface
    ESP_LOGI(TAG, "Applying ethernet configuration: IP=0x%08lx, Gateway=0x%08lx, Netmask=0x%08lx, DNS=0x%08lx",
             config->ip_addr, config->gateway, config->netmask, config->dns_server);
    
    // Note: Ethernet configuration is already handled by ethernet_interface_init()
    // This function just logs the configuration being applied
    
    return ESP_OK;
}

static esp_err_t apply_usb_mux_config(const usb_mux_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Applying USB MUX configuration: target=%d, auto_restore=%s",
             config->default_target, config->auto_restore ? "enabled" : "disabled");
    
    if (config->auto_restore) {
        // Apply USB MUX target using hardware control interface
        esp_err_t ret = usb_mux_set_target((usb_mux_target_t)config->default_target);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set USB MUX target to %d: %s", 
                     config->default_target, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "USB MUX initialized to target: %d", config->default_target);
    } else {
        ESP_LOGI(TAG, "USB MUX auto restore disabled, keeping current target");
    }
    
    return ESP_OK;
}

static esp_err_t apply_web_server_config(const web_server_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Applying web server configuration: root=%s, port=%d, auto_start=%s",
             config->document_root, config->port, config->auto_start ? "enabled" : "disabled");
    
    // Note: Web server configuration is managed by the web_server component
    // This function just logs the configuration being applied
    // The actual web server start/stop is handled in main.c based on auto_start flag
    
    return ESP_OK;
}

// ==================== Color Correction Configuration Functions ====================

esp_err_t config_manager_set_color_correction_config(const led_color_correction_config_t *config)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid color correction config pointer");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate gamma correction range
    if (config->gamma_correction <= 0.0f || config->gamma_correction > 5.0f) {
        ESP_LOGE(TAG, "Invalid gamma correction value: %.2f (must be 0.0-5.0)", config->gamma_correction);
        return ESP_ERR_INVALID_ARG;
    }

    // Validate boost values
    if (config->brightness_boost <= 0.0f || config->brightness_boost > 3.0f ||
        config->saturation_boost <= 0.0f || config->saturation_boost > 3.0f) {
        ESP_LOGE(TAG, "Invalid boost values (must be 0.0-3.0)");
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.color_correction = *config;
    ESP_LOGI(TAG, "Color correction config updated: enable=%s, gamma=%.2f, brightness=%.2f, saturation=%.2f",
             config->enable_correction ? "true" : "false",
             config->gamma_correction, config->brightness_boost, config->saturation_boost);

    return ESP_OK;
}

esp_err_t config_manager_get_color_correction_config(led_color_correction_config_t *config)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid config pointer");
        return ESP_ERR_INVALID_ARG;
    }

    *config = s_current_config.led.color_correction;
    return ESP_OK;
}

esp_err_t config_manager_set_color_correction_white_point(uint8_t white_r, uint8_t white_g, uint8_t white_b)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_current_config.led.color_correction.white_point_r = white_r;
    s_current_config.led.color_correction.white_point_g = white_g;
    s_current_config.led.color_correction.white_point_b = white_b;

    ESP_LOGI(TAG, "Color correction white point set to R:%d G:%d B:%d", white_r, white_g, white_b);
    return ESP_OK;
}

esp_err_t config_manager_set_color_correction_gamma(float gamma)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (gamma <= 0.0f || gamma > 5.0f) {
        ESP_LOGE(TAG, "Invalid gamma correction value: %.2f (must be 0.0-5.0)", gamma);
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.color_correction.gamma_correction = gamma;
    ESP_LOGI(TAG, "Color correction gamma set to %.2f", gamma);
    return ESP_OK;
}

esp_err_t config_manager_set_color_correction_brightness_boost(float boost)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (boost <= 0.0f || boost > 3.0f) {
        ESP_LOGE(TAG, "Invalid brightness boost value: %.2f (must be 0.0-3.0)", boost);
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.color_correction.brightness_boost = boost;
    ESP_LOGI(TAG, "Color correction brightness boost set to %.2f", boost);
    return ESP_OK;
}

esp_err_t config_manager_set_color_correction_saturation_boost(float boost)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (boost <= 0.0f || boost > 3.0f) {
        ESP_LOGE(TAG, "Invalid saturation boost value: %.2f (must be 0.0-3.0)", boost);
        return ESP_ERR_INVALID_ARG;
    }

    s_current_config.led.color_correction.saturation_boost = boost;
    ESP_LOGI(TAG, "Color correction saturation boost set to %.2f", boost);
    return ESP_OK;
}

esp_err_t config_manager_set_color_correction_enable(bool enable)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Configuration manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_current_config.led.color_correction.enable_correction = enable;
    ESP_LOGI(TAG, "Color correction %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}
