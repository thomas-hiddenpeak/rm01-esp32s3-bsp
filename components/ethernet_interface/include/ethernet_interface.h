/**
 * @file ethernet_interface.h
 * @brief ESP32S3 Ethernet Interface Component for W5500
 * 
 * This component provides comprehensive ethernet control using W5500 chip,
 * including network configuration, DHCP server, gateway functionality, and ping tests.
 */

#ifndef ETHERNET_INTERFACE_H
#define ETHERNET_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ethernet interface configuration structure
 */
typedef struct {
    uint32_t ip_addr;           ///< IP address (network byte order)
    uint32_t gateway;           ///< Gateway address (network byte order)
    uint32_t netmask;           ///< Subnet mask (network byte order)
    uint32_t dns_server;        ///< DNS server address (network byte order)
    bool dhcp_server_enabled;   ///< DHCP server enable flag
    bool gateway_enabled;       ///< Gateway service enable flag
    uint32_t dhcp_start_ip;     ///< DHCP pool start IP (network byte order)
    uint32_t dhcp_end_ip;       ///< DHCP pool end IP (network byte order)
    uint8_t dhcp_lease_time;    ///< DHCP lease time in hours
} ethernet_config_t;

/**
 * @brief Ethernet status enumeration
 */
typedef enum {
    ETH_STATUS_DISCONNECTED = 0,    ///< Ethernet cable disconnected
    ETH_STATUS_CONNECTED,           ///< Ethernet cable connected
    ETH_STATUS_GOT_IP,              ///< Got IP address
    ETH_STATUS_DHCP_SERVER_STARTED, ///< DHCP server started
    ETH_STATUS_GATEWAY_ENABLED      ///< Gateway service enabled
} ethernet_status_t;

/**
 * @brief Ethernet statistics structure
 */
typedef struct {
    uint32_t rx_packets;        ///< Received packets count
    uint32_t tx_packets;        ///< Transmitted packets count
    uint32_t rx_bytes;          ///< Received bytes count
    uint32_t tx_bytes;          ///< Transmitted bytes count
    uint32_t rx_errors;         ///< Receive error count
    uint32_t tx_errors;         ///< Transmit error count
    uint32_t dhcp_clients;      ///< Current DHCP clients count
} ethernet_stats_t;

/**
 * @brief DHCP client information structure
 */
typedef struct {
    uint8_t mac_addr[6];        ///< Client MAC address
    uint32_t ip_addr;           ///< Assigned IP address (network byte order)
    uint32_t lease_start_time;  ///< Lease start time (timestamp)
    uint32_t lease_duration;    ///< Lease duration in seconds
    char hostname[32];          ///< Client hostname (if available)
    bool is_active;             ///< Whether the lease is currently active
} dhcp_client_info_t;

/**
 * @brief DHCP server status structure
 */
typedef struct {
    bool server_enabled;        ///< DHCP server status
    uint32_t pool_start_ip;     ///< Pool start IP address
    uint32_t pool_end_ip;       ///< Pool end IP address
    uint32_t total_addresses;   ///< Total available addresses in pool
    uint32_t allocated_count;   ///< Number of allocated addresses
    uint32_t available_count;   ///< Number of available addresses
    uint32_t lease_time_hours;  ///< Default lease time in hours
    dhcp_client_info_t clients[10]; ///< Connected clients (max 10)
    uint8_t client_count;       ///< Current client count
} dhcp_server_status_t;

/**
 * @brief Ping result structure
 */
typedef struct {
    uint32_t target_ip;         ///< Target IP address
    uint32_t packets_sent;      ///< Packets sent
    uint32_t packets_received;  ///< Packets received
    uint32_t min_time_ms;       ///< Minimum round trip time (ms)
    uint32_t max_time_ms;       ///< Maximum round trip time (ms)
    uint32_t avg_time_ms;       ///< Average round trip time (ms)
    bool success;               ///< Ping test success flag
} ping_result_t;

/**
 * @brief Ethernet event callback function type
 */
typedef void (*ethernet_event_callback_t)(ethernet_status_t status, void *data);

/**
 * @brief Default ethernet configuration
 */
#define ETHERNET_DEFAULT_CONFIG() { \
    .ip_addr = ESP_IP4TOADDR(10, 10, 99, 97), \
    .gateway = ESP_IP4TOADDR(10, 10, 99, 97), \
    .netmask = ESP_IP4TOADDR(255, 255, 255, 0), \
    .dns_server = ESP_IP4TOADDR(8, 8, 8, 8), \
    .dhcp_server_enabled = false, \
    .gateway_enabled = false, \
    .dhcp_start_ip = ESP_IP4TOADDR(10, 10, 99, 101), \
    .dhcp_end_ip = ESP_IP4TOADDR(10, 10, 99, 110), \
    .dhcp_lease_time = 24 \
}

/**
 * @brief Initialize the ethernet interface
 * 
 * @param config Ethernet configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_interface_init(const ethernet_config_t *config);

/**
 * @brief Start the ethernet interface
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_interface_start(void);

/**
 * @brief Stop the ethernet interface
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_interface_stop(void);

/**
 * @brief Deinitialize the ethernet interface
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_interface_deinit(void);

/**
 * @brief Set ethernet network configuration
 * 
 * @param ip_addr IP address string (e.g., "10.10.99.97")
 * @param gateway Gateway address string
 * @param netmask Subnet mask string
 * @param dns_server DNS server address string
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_set_network_config(const char *ip_addr, const char *gateway, 
                                     const char *netmask, const char *dns_server);

/**
 * @brief Enable or disable DHCP server
 * 
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_set_dhcp_server(bool enable);

/**
 * @brief Configure DHCP server IP pool
 * 
 * @param start_ip DHCP pool start IP string
 * @param end_ip DHCP pool end IP string
 * @param lease_time Lease time in hours
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_set_dhcp_pool(const char *start_ip, const char *end_ip, uint8_t lease_time);

/**
 * @brief Enable or disable gateway service
 * 
 * @param enable true to enable, false to disable
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_set_gateway(bool enable);

/**
 * @brief Get current ethernet status
 * 
 * @return ethernet_status_t Current ethernet status
 */
ethernet_status_t ethernet_get_status(void);

/**
 * @brief Get ethernet configuration
 * 
 * @param config Pointer to store current configuration
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_get_config(ethernet_config_t *config);

/**
 * @brief Save ethernet configuration from config manager
 * 
 * This function allows the config manager to update the ethernet configuration
 * stored in the ethernet interface's NVS storage.
 * 
 * @param config Pointer to ethernet configuration to save
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_save_config_from_manager(const ethernet_config_t *config);

/**
 * @brief Get ethernet statistics
 * 
 * @param stats Pointer to store statistics
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_get_stats(ethernet_stats_t *stats);

/**
 * @brief Get DHCP server status and client information
 * 
 * @param status Pointer to store DHCP server status
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_get_dhcp_status(dhcp_server_status_t *status);

/**
 * @brief Get specific DHCP client information by MAC address
 * 
 * @param mac_addr Client MAC address (6 bytes)
 * @param client_info Pointer to store client information
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if client not found
 */
esp_err_t ethernet_get_dhcp_client_by_mac(const uint8_t *mac_addr, dhcp_client_info_t *client_info);

/**
 * @brief Get specific DHCP client information by IP address
 * 
 * @param ip_addr Client IP address
 * @param client_info Pointer to store client information
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if client not found
 */
esp_err_t ethernet_get_dhcp_client_by_ip(uint32_t ip_addr, dhcp_client_info_t *client_info);

/**
 * @brief Release a DHCP lease for a specific client
 * 
 * @param mac_addr Client MAC address (6 bytes)
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if client not found
 */
esp_err_t ethernet_release_dhcp_lease(const uint8_t *mac_addr);

/**
 * @brief Perform ping test
 * 
 * @param target_ip Target IP address string
 * @param count Number of ping packets
 * @param timeout_ms Timeout for each ping in milliseconds
 * @param result Pointer to store ping result
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_ping(const char *target_ip, uint32_t count, uint32_t timeout_ms, ping_result_t *result);

/**
 * @brief Register ethernet event callback
 * 
 * @param callback Callback function to register
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_register_event_callback(ethernet_event_callback_t callback);

/**
 * @brief Get MAC address of ethernet interface
 * 
 * @param mac Pointer to store MAC address (6 bytes)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_get_mac_address(uint8_t *mac);

/**
 * @brief Reset ethernet interface
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_reset(void);

/**
 * @brief Check if ethernet cable is connected
 * 
 * @return true if connected, false otherwise
 */
bool ethernet_is_cable_connected(void);

/**
 * @brief Register ethernet console commands
 * 
 * This function registers all ethernet-related console commands.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t ethernet_register_console_commands(void);

#ifdef __cplusplus
}
#endif

#endif // ETHERNET_INTERFACE_H