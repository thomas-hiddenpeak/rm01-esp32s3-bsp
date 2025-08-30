/**
 * @file config_manager.h
 * @brief ESP32S3 Configuration Manager Component
 * 
 * This component provides comprehensive configuration management for the ESP32S3 BSP,
 * including saving/loading default parameters for fan, LED, ethernet, DHCP, and gateway settings.
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
    .rainbow_speed_ms = 100 \
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
 * @brief Default system configuration
 */
#define DEFAULT_SYSTEM_CONFIG() { \
    .auto_save_config = true, \
    .save_interval_ms = 300000, \
    .startup_load_config = true, \
    .enable_debug_logging = false \
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
