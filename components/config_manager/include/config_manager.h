/**
 * @file config_manager.h
 * @brief ESP32S3 Configuration Manager Component
 * 
 * This component provides comprehensive configuration management for the ESP32S3 BSP,
 * including saving/loading configurations for fan, LED, ethernet, DHCP, and gateway settings.
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ethernet_interface.h"  // 使用ethernet_interface中定义的ethernet_config_t
#include "hardware_control.h"    // 使用hardware_control中定义的led_color_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web server configuration structure (simplified for config manager)
 */
typedef struct {
    char document_root[256];        ///< Document root path on SD card
    uint16_t port;                  ///< HTTP server port
    bool auto_start;                ///< Auto start server on boot
    bool enable_cors;               ///< Enable CORS headers
    char default_index[64];         ///< Default index file
} web_server_config_t;

/**
 * @brief Fan configuration structure
 */
typedef struct {
    uint8_t default_speed_on;   ///< Default fan speed when turned on (0-100)
    uint8_t default_speed_off;  ///< Default fan speed when turned off (0-100)
    bool auto_enable;           ///< Auto fan control based on temperature
    uint8_t auto_temp_threshold; ///< Temperature threshold for auto control (°C)
} fan_config_t;

/**
 * @brief LED configuration structure
 */
typedef struct {
    uint8_t default_brightness;         ///< Default LED brightness (0-100)
    led_color_t board_led_color; ///< Default board LED color
    led_color_t touch_led_color; ///< Default touch LED color
    bool effect_enable;                 ///< Enable LED effects
    uint16_t rainbow_speed_ms;          ///< Rainbow effect speed in milliseconds
    
    // LED矩阵配置
    bool matrix_auto_start;             ///< Auto start matrix display on boot
    uint8_t matrix_brightness;          ///< Matrix default brightness (0-100)
    char matrix_startup_animation[64];  ///< Startup animation name
    bool matrix_enable;                 ///< Enable matrix display
} led_config_t;

/**
 * @brief DHCP server configuration structure
 */
typedef struct {
    bool enable;                ///< Enable DHCP server
    char start_ip[16];          ///< DHCP pool start IP
    char end_ip[16];            ///< DHCP pool end IP
    uint8_t lease_time_hours;   ///< Lease time in hours
    uint8_t max_clients;        ///< Maximum number of clients
    bool auto_start;            ///< Auto start DHCP server on boot
} dhcp_config_t;

/**
 * @brief Gateway service configuration structure
 */
typedef struct {
    bool enable;            ///< Enable gateway service
    bool nat_enable;        ///< Enable NAT forwarding
    bool firewall_enable;   ///< Enable firewall
    bool auto_start;        ///< Auto start gateway service on boot
} gateway_config_t;

/**
 * @brief USB MUX configuration structure
 */
typedef struct {
    uint8_t default_target;     ///< Default USB MUX target (0=ESP32S3, 1=AGX, 2=LPMU)
    bool auto_restore;          ///< Auto restore USB MUX target on boot
} usb_mux_config_t;

/**
 * @brief System configuration structure
 */
typedef struct {
    bool auto_save_config;          ///< Auto save configuration changes
    uint32_t save_interval_ms;      ///< Configuration save interval in milliseconds
    bool startup_load_config;       ///< Load configuration on startup
    bool enable_debug_logging;      ///< Enable debug logging
} system_config_t;

/**
 * @brief Complete system configuration structure
 */
typedef struct {
    fan_config_t fan;           ///< Fan configuration
    led_config_t led;           ///< LED configuration
    ethernet_config_t ethernet; ///< Ethernet configuration
    dhcp_config_t dhcp;         ///< DHCP server configuration
    gateway_config_t gateway;   ///< Gateway service configuration
    usb_mux_config_t usb_mux;   ///< USB MUX configuration
    web_server_config_t web;    ///< Web server configuration
    system_config_t system;     ///< System configuration
    uint32_t config_version;    ///< Configuration version for compatibility
    uint32_t checksum;          ///< Configuration checksum for validation
} complete_config_t;

/**
 * @brief Configuration events enumeration
 */
typedef enum {
    CONFIG_EVENT_SAVED,         ///< Configuration was saved
    CONFIG_EVENT_LOADED,        ///< Configuration was loaded
    CONFIG_EVENT_RESET,         ///< Configuration was reset to defaults
    CONFIG_EVENT_ERROR          ///< Configuration operation error
} config_event_t;

/**
 * @brief Configuration event callback function type
 */
typedef void (*config_event_callback_t)(config_event_t event, const char *message);

/**
 * @brief Default fan configuration
 */
#define DEFAULT_FAN_CONFIG() { \
    .default_speed_on = 50, \
    .default_speed_off = 0, \
    .auto_enable = false, \
    .auto_temp_threshold = 60 \
}

/**
 * @brief Default LED configuration
 */
#define DEFAULT_LED_CONFIG() { \
    .default_brightness = 50, \
    .board_led_color = {0, 0, 255}, \
    .touch_led_color = {0, 255, 0}, \
    .effect_enable = false, \
    .rainbow_speed_ms = 100, \
    .matrix_auto_start = true, \
    .matrix_brightness = 30, \
    .matrix_startup_animation = "Logo", \
    .matrix_enable = true \
}

/**
 * @brief Default ethernet configuration 
 * 注意：现在使用ethernet_interface.h中定义的配置格式
 */
#define DEFAULT_ETHERNET_CONFIG() ETHERNET_DEFAULT_CONFIG()

/**
 * @brief Default DHCP configuration
 */
#define DEFAULT_DHCP_CONFIG() { \
    .enable = true, \
    .start_ip = "10.10.99.101", \
    .end_ip = "10.10.99.110", \
    .lease_time_hours = 24, \
    .max_clients = 8, \
    .auto_start = true \
}

/**
 * @brief Default gateway configuration
 */
#define DEFAULT_GATEWAY_CONFIG() { \
    .enable = true, \
    .nat_enable = true, \
    .firewall_enable = false, \
    .auto_start = true \
}

/**
 * @brief Default USB MUX configuration
 */
#define DEFAULT_USB_MUX_CONFIG() { \
    .default_target = 0, \
    .auto_restore = true \
}

/**
 * @brief Default system configuration
 */
#define DEFAULT_SYSTEM_CONFIG() { \
    .auto_save_config = true, \
    .save_interval_ms = 300000, \
    .startup_load_config = true, \
    .enable_debug_logging = false \
}

/**
 * @brief Default web server configuration
 */
#define DEFAULT_WEB_SERVER_CONFIG() { \
    .document_root = "/sdcard/web", \
    .port = 80, \
    .auto_start = false, \
    .enable_cors = true, \
    .default_index = "index.html" \
}

/**
 * @brief Default complete configuration
 */
#define DEFAULT_COMPLETE_CONFIG() { \
    .fan = DEFAULT_FAN_CONFIG(), \
    .led = DEFAULT_LED_CONFIG(), \
    .ethernet = DEFAULT_ETHERNET_CONFIG(), \
    .dhcp = DEFAULT_DHCP_CONFIG(), \
    .gateway = DEFAULT_GATEWAY_CONFIG(), \
    .usb_mux = DEFAULT_USB_MUX_CONFIG(), \
    .web = DEFAULT_WEB_SERVER_CONFIG(), \
    .system = DEFAULT_SYSTEM_CONFIG(), \
    .config_version = 1, \
    .checksum = 0 \
}

// ==================== Main API Functions ====================

/**
 * @brief Initialize configuration manager
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_init(void);

/**
 * @brief Deinitialize configuration manager
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_deinit(void);

/**
 * @brief Save current configuration to NVS
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_save(void);

/**
 * @brief Load configuration from NVS
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_load(void);

/**
 * @brief Reset configuration to factory defaults
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_reset_to_defaults(void);

/**
 * @brief Clear all configuration from NVS
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_clear(void);

/**
 * @brief Auto-apply loaded configuration to all subsystems
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_apply_config(void);

// ==================== Configuration Getters ====================

/**
 * @brief Get current complete configuration
 * 
 * @return const complete_config_t* Pointer to current configuration
 */
const complete_config_t* config_manager_get_config(void);

/**
 * @brief Get fan configuration
 * 
 * @return const fan_config_t* Pointer to fan configuration
 */
const fan_config_t* config_manager_get_fan_config(void);

/**
 * @brief Get LED configuration
 * 
 * @return const led_config_t* Pointer to LED configuration
 */
const led_config_t* config_manager_get_led_config(void);

/**
 * @brief Get ethernet configuration
 * 
 * @return const ethernet_config_t* Pointer to ethernet configuration
 */
const ethernet_config_t* config_manager_get_ethernet_config(void);

/**
 * @brief Get DHCP configuration
 * 
 * @return const dhcp_config_t* Pointer to DHCP configuration
 */
const dhcp_config_t* config_manager_get_dhcp_config(void);

/**
 * @brief Get gateway configuration
 * 
 * @return const gateway_config_t* Pointer to gateway configuration
 */
const gateway_config_t* config_manager_get_gateway_config(void);

/**
 * @brief Get system configuration
 * 
 * @return const system_config_t* Pointer to system configuration
 */
const system_config_t* config_manager_get_system_config(void);

/**
 * @brief Get web server configuration
 * 
 * @return const web_server_config_t* Pointer to web server configuration
 */
const web_server_config_t* config_manager_get_web_server_config(void);

// ==================== Configuration Setters ====================

/**
 * @brief Set fan configuration
 * 
 * @param config Fan configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_fan_config(const fan_config_t *config);

/**
 * @brief Set LED configuration
 * 
 * @param config LED configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_led_config(const led_config_t *config);

/**
 * @brief Set ethernet configuration
 * 
 * @param config Ethernet configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_ethernet_config(const ethernet_config_t *config);

/**
 * @brief Set DHCP configuration
 * 
 * @param config DHCP configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_dhcp_config(const dhcp_config_t *config);

/**
 * @brief Set gateway configuration
 * 
 * @param config Gateway configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_gateway_config(const gateway_config_t *config);

/**
 * @brief Set system configuration
 * 
 * @param config System configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_system_config(const system_config_t *config);

/**
 * @brief Set web server configuration
 * 
 * @param config Web server configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_web_server_config(const web_server_config_t *config);

// ==================== Individual Parameter Functions ====================

/**
 * @brief Set fan default speed
 * 
 * @param speed_on Default speed when turned on (0-100)
 * @param speed_off Default speed when turned off (0-100)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_fan_speed(uint8_t speed_on, uint8_t speed_off);

/**
 * @brief Set LED default brightness and colors
 * 
 * @param brightness Default brightness (0-100)
 * @param board_color Board LED color
 * @param touch_color Touch LED color
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_led_defaults(uint8_t brightness, 
                                          led_color_t board_color,
                                          led_color_t touch_color);

/**
 * @brief Set LED matrix configuration
 * 
 * @param brightness Matrix brightness (0-100)
 * @param animation_name Startup animation name
 * @param auto_start Auto start on boot
 * @param enable Enable matrix display
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_matrix_config(uint8_t brightness, 
                                           const char *animation_name,
                                           bool auto_start,
                                           bool enable);

/**
 * @brief Get LED matrix configuration from current LED config
 * 
 * @param brightness Output matrix brightness
 * @param animation_name Output animation name buffer (min 64 bytes)
 * @param auto_start Output auto start setting
 * @param enable Output enable setting
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_get_matrix_config(uint8_t *brightness,
                                           char *animation_name,
                                           bool *auto_start,
                                           bool *enable);

/**
 * @brief Set USB MUX default target
 * 
 * @param target USB MUX target (0=ESP32S3, 1=AGX, 2=LPMU)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_usb_mux_target(uint8_t target);

/**
 * @brief Get USB MUX configuration
 * 
 * @return const usb_mux_config_t* Pointer to USB MUX configuration
 */
const usb_mux_config_t* config_manager_get_usb_mux_config(void);

/**
 * @brief Set ethernet IP configuration using string format
 * 
 * @param ip_addr IP address string
 * @param gateway Gateway address string  
 * @param netmask Netmask string
 * @param dns_server DNS server string
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_ethernet_ip_from_strings(const char *ip_addr, const char *gateway,
                                                      const char *netmask, const char *dns_server);

/**
 * @brief Set DHCP server parameters
 * 
 * @param enable Enable DHCP server
 * @param start_ip Start IP address string
 * @param end_ip End IP address string
 * @param lease_time Lease time in hours
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_dhcp_params(bool enable, const char *start_ip,
                                         const char *end_ip, uint8_t lease_time);

/**
 * @brief Set gateway service parameters
 * 
 * @param enable Enable gateway service
 * @param nat_enable Enable NAT forwarding
 * @param firewall_enable Enable firewall
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_gateway_params(bool enable, bool nat_enable, bool firewall_enable);

/**
 * @brief Set web server parameters
 * 
 * @param document_root Document root path
 * @param port HTTP server port  
 * @param auto_start Auto start flag
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_set_web_server_params(const char *document_root, uint16_t port, bool auto_start);

// ==================== Utility Functions ====================

/**
 * @brief Register configuration event callback
 * 
 * @param callback Event callback function
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_register_event_callback(config_event_callback_t callback);

/**
 * @brief Print current configuration to console
 */
void config_manager_print_config(void);

/**
 * @brief Validate configuration integrity
 * 
 * @return bool true if configuration is valid, false otherwise
 */
bool config_manager_validate_config(void);

/**
 * @brief Get configuration size in bytes
 * 
 * @return size_t Configuration size
 */
size_t config_manager_get_config_size(void);

/**
 * @brief Check if configuration exists in NVS
 * 
 * @return bool true if configuration exists, false otherwise
 */
bool config_manager_config_exists(void);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MANAGER_H
