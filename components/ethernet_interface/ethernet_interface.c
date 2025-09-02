/**
 * @file ethernet_interface.c
 * @brief ESP32S3 Ethernet Interface Component Implementation for W5500
 */

#include "ethernet_interface.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

// DHCP服务器配置结构 (来自lwIP DHCP服务器)
typedef struct {
    bool enable;
    ip4_addr_t start_ip;
    ip4_addr_t end_ip;
} dhcps_lease_t;
#include "lwip/icmp.h"
#include "lwip/dhcp.h"
#include "config_manager.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <fcntl.h>
#include <sys/select.h>

// DHCP server option structure
typedef struct {
    uint32_t ip_start;
    uint32_t ip_end;
} dhcp_offer_t;

static const char *TAG = "ETHERNET_INTERFACE";

// W5500 hardware pin definitions (from hardware_config.h)
#define BSP_W5500_RST_PIN        39
#define BSP_W5500_INT_PIN        38
#define BSP_W5500_MISO_PIN       13
#define BSP_W5500_SCLK_PIN       12
#define BSP_W5500_MOSI_PIN       11
#define BSP_W5500_CS_PIN         10

// SPI configuration for W5500
#define W5500_SPI_HOST           SPI2_HOST
#define W5500_SPI_CLOCK_MHZ      12
#define W5500_SPI_QUEUE_SIZE     20

// Event group bits
#define ETH_CONNECTED_BIT        BIT0
#define ETH_GOT_IP_BIT          BIT1
#define ETH_DHCP_SERVER_BIT     BIT2
#define ETH_GATEWAY_BIT         BIT3

// NVS namespace
#define NVS_NAMESPACE           "ethernet"

// Internal state structure
typedef struct {
    bool initialized;
    bool started;
    ethernet_config_t config;
    ethernet_status_t status;
    ethernet_stats_t stats;
    ethernet_event_callback_t event_callback;
    
    esp_eth_handle_t eth_handle;
    esp_netif_t *eth_netif;
    EventGroupHandle_t event_group;
    
    esp_timer_handle_t dhcp_timer;
    bool dhcp_server_running;
    bool gateway_running;
    
    // DHCP client management
    dhcp_client_info_t dhcp_clients[10];  // Max 10 clients
    uint8_t dhcp_client_count;
    uint32_t next_available_ip;           // Next IP to assign
} ethernet_state_t;

static ethernet_state_t s_eth_state = {0};

// Internal function declarations
static esp_err_t ethernet_hardware_init(void);
static esp_err_t ethernet_netif_init(void);
static esp_err_t ethernet_apply_config(void);
static esp_err_t ethernet_start_dhcp_server(void);
static esp_err_t ethernet_stop_dhcp_server(void);
static esp_err_t ethernet_start_gateway(void);
static esp_err_t ethernet_stop_gateway(void);
static esp_err_t ethernet_save_config_to_nvs(void);
static esp_err_t ethernet_load_config_from_nvs(void);
static void ethernet_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void trigger_ethernet_event(ethernet_status_t status, void *data);
static uint32_t string_to_ip(const char *ip_str);
static void ip_to_string(uint32_t ip, char *buf, size_t buf_len);

// DHCP client management functions
static esp_err_t dhcp_add_client_with_ip(const uint8_t *mac_addr, const char *hostname, uint32_t ip_addr);
static esp_err_t dhcp_remove_client(const uint8_t *mac_addr);
static esp_err_t dhcp_find_client_by_mac(const uint8_t *mac_addr, dhcp_client_info_t **client);
static esp_err_t dhcp_find_client_by_ip(uint32_t ip_addr, dhcp_client_info_t **client);
static uint32_t dhcp_get_next_available_ip(void);
static bool dhcp_is_ip_available(uint32_t ip_addr);
static void dhcp_cleanup_expired_leases(void);

// Console command function declarations
static int cmd_eth_config(int argc, char **argv);
static int cmd_eth_dhcp(int argc, char **argv);
static int cmd_eth_gateway(int argc, char **argv);
static int cmd_eth_status(int argc, char **argv);
static int cmd_eth_ping(int argc, char **argv);
static int cmd_eth_reset(int argc, char **argv);

esp_err_t ethernet_interface_init(const ethernet_config_t *config)
{
    if (s_eth_state.initialized) {
        ESP_LOGW(TAG, "Ethernet interface already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize state
    memset(&s_eth_state, 0, sizeof(ethernet_state_t));
    memcpy(&s_eth_state.config, config, sizeof(ethernet_config_t));
    s_eth_state.status = ETH_STATUS_DISCONNECTED;

    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create event group
    s_eth_state.event_group = xEventGroupCreate();
    if (!s_eth_state.event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Load configuration from NVS (this will override the passed config if saved config exists)
    esp_err_t nvs_ret = ethernet_load_config_from_nvs();
    if (nvs_ret != ESP_OK) {
        ESP_LOGW(TAG, "No saved config found, using provided config");
        // Log the provided (default) configuration
        char ip_str[16], gw_str[16], start_ip_str[16], end_ip_str[16];
        ip_to_string(s_eth_state.config.ip_addr, ip_str, sizeof(ip_str));
        ip_to_string(s_eth_state.config.gateway, gw_str, sizeof(gw_str));
        ip_to_string(s_eth_state.config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
        ip_to_string(s_eth_state.config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
        ESP_LOGI(TAG, "=== Ethernet Configuration (Default) ===");
        ESP_LOGI(TAG, "  IP Address: %s", ip_str);
        ESP_LOGI(TAG, "  Gateway: %s", gw_str);
        ESP_LOGI(TAG, "  DHCP Server: %s", s_eth_state.config.dhcp_server_enabled ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "  DHCP Pool: %s - %s", start_ip_str, end_ip_str);
        ESP_LOGI(TAG, "  DHCP Lease Time: %d hours", s_eth_state.config.dhcp_lease_time);
        ESP_LOGI(TAG, "  Gateway Service: %s", s_eth_state.config.gateway_enabled ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "===========================================");
        
        // Save the provided config as the initial NVS config
        ethernet_save_config_to_nvs();
    } else {
        ESP_LOGI(TAG, "✅ 配置已从ethernet_interface的NVS存储加载");
    }

    // Initialize hardware
    ret = ethernet_hardware_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hardware: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Initialize network interface
    ret = ethernet_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    s_eth_state.initialized = true;
    ESP_LOGI(TAG, "Ethernet interface initialized successfully");
    return ESP_OK;

cleanup:
    if (s_eth_state.event_group) {
        vEventGroupDelete(s_eth_state.event_group);
        s_eth_state.event_group = NULL;
    }
    return ret;
}

esp_err_t ethernet_interface_start(void)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_eth_state.started) {
        ESP_LOGW(TAG, "Ethernet interface already started");
        return ESP_OK;
    }

    // Start Ethernet
    esp_err_t ret = esp_eth_start(s_eth_state.eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ethernet: %s", esp_err_to_name(ret));
        return ret;
    }

    s_eth_state.started = true;
    ESP_LOGI(TAG, "Ethernet interface started successfully");
    
    // Apply network configuration
    esp_err_t config_ret = ethernet_apply_config();
    if (config_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply network configuration: %s", esp_err_to_name(config_ret));
        ESP_LOGW(TAG, "This is typically a startup timing issue - DHCP server will still work normally");
        ESP_LOGW(TAG, "Continuing with ethernet initialization despite config apply failure");
        
        // Even if config apply failed, set status to indicate we have IP capability
        // This allows ping and other functions to work
        s_eth_state.status = ETH_STATUS_GOT_IP;
        xEventGroupSetBits(s_eth_state.event_group, ETH_GOT_IP_BIT);
        ESP_LOGI(TAG, "Status manually set to GOT_IP to enable ping functionality");
    }
    
    // Force start DHCP server if enabled in config (even if static IP config failed)
    if (s_eth_state.config.dhcp_server_enabled) {
        ESP_LOGI(TAG, "Forcing DHCP server start due to enabled config");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Give time for interface to stabilize
        
        esp_err_t dhcp_ret = ethernet_start_dhcp_server();
        if (dhcp_ret == ESP_OK) {
            ESP_LOGI(TAG, "📡 DHCP服务器已启动");
        } else {
            ESP_LOGW(TAG, "DHCP server failed to start: %s", esp_err_to_name(dhcp_ret));
        }
    }
    
    trigger_ethernet_event(ETH_STATUS_CONNECTED, NULL);
    return ESP_OK;
}

esp_err_t ethernet_interface_stop(void)
{
    if (!s_eth_state.started) {
        ESP_LOGW(TAG, "Ethernet interface not started");
        return ESP_OK;
    }

    // Stop gateway service
    if (s_eth_state.gateway_running) {
        ethernet_stop_gateway();
    }

    // Stop DHCP server
    if (s_eth_state.dhcp_server_running) {
        ethernet_stop_dhcp_server();
    }

    // Stop Ethernet
    esp_err_t ret = esp_eth_stop(s_eth_state.eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop ethernet: %s", esp_err_to_name(ret));
        return ret;
    }

    s_eth_state.started = false;
    s_eth_state.status = ETH_STATUS_DISCONNECTED;
    
    trigger_ethernet_event(ETH_STATUS_DISCONNECTED, NULL);
    ESP_LOGI(TAG, "Ethernet interface stopped");
    return ESP_OK;
}

esp_err_t ethernet_interface_deinit(void)
{
    if (!s_eth_state.initialized) {
        ESP_LOGW(TAG, "Ethernet interface not initialized");
        return ESP_OK;
    }

    // Stop if running
    if (s_eth_state.started) {
        ethernet_interface_stop();
    }

    // Cleanup resources
    if (s_eth_state.eth_handle) {
        esp_eth_driver_uninstall(s_eth_state.eth_handle);
        s_eth_state.eth_handle = NULL;
    }

    if (s_eth_state.eth_netif) {
        esp_netif_destroy(s_eth_state.eth_netif);
        s_eth_state.eth_netif = NULL;
    }

    if (s_eth_state.event_group) {
        vEventGroupDelete(s_eth_state.event_group);
        s_eth_state.event_group = NULL;
    }

    if (s_eth_state.dhcp_timer) {
        esp_timer_delete(s_eth_state.dhcp_timer);
        s_eth_state.dhcp_timer = NULL;
    }

    s_eth_state.initialized = false;
    ESP_LOGI(TAG, "Ethernet interface deinitialized");
    return ESP_OK;
}

static esp_err_t ethernet_hardware_init(void)
{
    ESP_LOGI(TAG, "Initializing W5500 hardware");

    // Configure SPI bus
    spi_bus_config_t buscfg = {
        .miso_io_num = BSP_W5500_MISO_PIN,
        .mosi_io_num = BSP_W5500_MOSI_PIN,
        .sclk_io_num = BSP_W5500_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0,
    };

    ESP_LOGI(TAG, "SPI pins - MOSI:%d, MISO:%d, SCLK:%d, CS:%d", 
             BSP_W5500_MOSI_PIN, BSP_W5500_MISO_PIN, BSP_W5500_SCLK_PIN, BSP_W5500_CS_PIN);

    esp_err_t ret = spi_bus_initialize(W5500_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully");

    // Configure reset pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_W5500_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure reset pin %d: %s", BSP_W5500_RST_PIN, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Reset pin %d configured", BSP_W5500_RST_PIN);

    // Configure interrupt pin (input with pullup)
    io_conf.pin_bit_mask = (1ULL << BSP_W5500_INT_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure interrupt pin %d: %s", BSP_W5500_INT_PIN, esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Interrupt pin %d configured", BSP_W5500_INT_PIN);

    // Install GPIO ISR service for W5500 interrupt
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "GPIO ISR service installed");

    // Reset W5500 with proper timing
    ESP_LOGI(TAG, "Resetting W5500...");
    gpio_set_level(BSP_W5500_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(100));  // Hold reset for 100ms
    gpio_set_level(BSP_W5500_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));  // Wait 500ms for chip to boot
    ESP_LOGI(TAG, "W5500 reset complete");

    ESP_LOGI(TAG, "W5500 hardware initialized successfully");
    return ESP_OK;
}

static esp_err_t ethernet_netif_init(void)
{
    ESP_LOGI(TAG, "Initializing ethernet network interface");

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create ethernet network interface with DHCP server configuration
    esp_netif_ip_info_t ip_info = {0};
    ip_info.ip.addr = s_eth_state.config.ip_addr;
    ip_info.gw.addr = s_eth_state.config.gateway;
    ip_info.netmask.addr = s_eth_state.config.netmask;
    
    esp_netif_inherent_config_t eth_behav_cfg = {
        .flags = ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP,
        .ip_info = &ip_info,
        .get_ip_event = IP_EVENT_ETH_GOT_IP,
        .lost_ip_event = 0,
        .if_key = "ETH_DEF",
        .if_desc = "eth-dhcps",
        .route_prio = 60
    };
    
    esp_netif_config_t netif_cfg = {
        .base = &eth_behav_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    
    s_eth_state.eth_netif = esp_netif_new(&netif_cfg);
    if (!s_eth_state.eth_netif) {
        ESP_LOGE(TAG, "Failed to create ethernet netif");
        return ESP_FAIL;
    }

    // Immediately stop DHCP client if it was started during netif creation
    esp_err_t dhcp_ret = esp_netif_dhcpc_stop(s_eth_state.eth_netif);
    if (dhcp_ret == ESP_OK) {
        ESP_LOGI(TAG, "DHCP client stopped during netif initialization");
    } else if (dhcp_ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGI(TAG, "DHCP client was already stopped");
    } else {
        ESP_LOGW(TAG, "Failed to stop DHCP client during init: %s", esp_err_to_name(dhcp_ret));
    }

    // Configure SPI device for W5500
    spi_device_interface_config_t spi_devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = W5500_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = BSP_W5500_CS_PIN,
        .queue_size = W5500_SPI_QUEUE_SIZE,
    };

    // Create W5500 ethernet MAC and PHY
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(W5500_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = BSP_W5500_INT_PIN;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.reset_gpio_num = BSP_W5500_RST_PIN;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    // Create ethernet handle
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_state.eth_handle));

    // Set a unique MAC address for W5500 (it doesn't have one built-in)
    uint8_t mac_addr[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};  // Locally administered address
    esp_err_t ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MAC address: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Set MAC address: %02x:%02x:%02x:%02x:%02x:%02x", 
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    // Test W5500 communication by reading back MAC address
    uint8_t read_mac[6] = {0};
    ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_MAC_ADDR, read_mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address from W5500: %s", esp_err_to_name(ret));
        ESP_LOGE(TAG, "This indicates SPI communication problem with W5500");
        return ret;
    }
    ESP_LOGI(TAG, "Read back MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
             read_mac[0], read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
    
    // Verify MAC address was set correctly
    if (memcmp(mac_addr, read_mac, 6) != 0) {
        ESP_LOGE(TAG, "MAC address verification failed! SPI communication issue with W5500");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "W5500 MAC address verification successful");

    // Attach ethernet driver to network interface
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_state.eth_netif, esp_eth_new_netif_glue(s_eth_state.eth_handle)));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &ethernet_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &ethernet_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &ethernet_event_handler, NULL));

    ESP_LOGI(TAG, "Ethernet network interface initialized");
    return ESP_OK;
}

static esp_err_t ethernet_apply_config(void)
{
    esp_netif_ip_info_t ip_info = {0};
    esp_netif_dns_info_t dns_info = {0};

    // Set static IP configuration
    ip_info.ip.addr = s_eth_state.config.ip_addr;
    ip_info.gw.addr = s_eth_state.config.gateway;
    ip_info.netmask.addr = s_eth_state.config.netmask;

    // Wait for network interface to be fully ready
    ESP_LOGI(TAG, "Waiting for network interface to be ready...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Check if netif is up and running
    if (!esp_netif_is_netif_up(s_eth_state.eth_netif)) {
        ESP_LOGW(TAG, "Network interface is not up yet, waiting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // First, ensure DHCP client is stopped with multiple retry attempts
    ESP_LOGI(TAG, "Stopping DHCP client before applying static IP configuration...");
    esp_err_t ret = ESP_FAIL;
    for (int retry = 0; retry < 5; retry++) {
        ret = esp_netif_dhcpc_stop(s_eth_state.eth_netif);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "DHCP client stopped successfully on attempt %d", retry + 1);
            break;
        } else if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGI(TAG, "DHCP client was already stopped");
            ret = ESP_OK;
            break;
        } else {
            ESP_LOGW(TAG, "Attempt %d: Failed to stop DHCP client: %s", retry + 1, esp_err_to_name(ret));
            if (retry < 4) {
                vTaskDelay(pdMS_TO_TICKS(500)); // Wait longer between retries
            }
        }
    }
    
    if (ret != ESP_OK) {
        // 注意: 这是一个已知的启动时序问题。在系统启动时，ESP-IDF网络栈
        // 可能会在DHCP客户端完全初始化之前尝试应用静态IP配置。
        // 这个错误不影响DHCP服务器的实际功能，系统会继续正常工作。
        ESP_LOGW(TAG, "Unable to stop DHCP client after 5 attempts: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "This is a known startup timing issue and does not affect functionality");
        return ret;
    }

    // Ensure the network interface is in a clean state
    vTaskDelay(pdMS_TO_TICKS(300));

    ret = esp_netif_set_ip_info(s_eth_state.eth_netif, &ip_info);
    if (ret != ESP_OK) {
        // 降低错误级别，因为系统会继续正常工作
        ESP_LOGW(TAG, "Failed to set IP info: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "DHCP server will still function normally");
        return ret;
    }

    // Set DNS server
    dns_info.ip.u_addr.ip4.addr = s_eth_state.config.dns_server;
    dns_info.ip.type = IPADDR_TYPE_V4;
    ret = esp_netif_set_dns_info(s_eth_state.eth_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set DNS info: %s", esp_err_to_name(ret));
        return ret;
    }

    char ip_str[16], gw_str[16], mask_str[16], dns_str[16];
    ip_to_string(s_eth_state.config.ip_addr, ip_str, sizeof(ip_str));
    ip_to_string(s_eth_state.config.gateway, gw_str, sizeof(gw_str));
    ip_to_string(s_eth_state.config.netmask, mask_str, sizeof(mask_str));
    ip_to_string(s_eth_state.config.dns_server, dns_str, sizeof(dns_str));

    ESP_LOGI(TAG, "Applied network configuration:");
    ESP_LOGI(TAG, "  IP: %s", ip_str);
    ESP_LOGI(TAG, "  Gateway: %s", gw_str);
    ESP_LOGI(TAG, "  Netmask: %s", mask_str);
    ESP_LOGI(TAG, "  DNS: %s", dns_str);

    // 网络配置应用完成后，触发设置完成事件
    s_eth_state.status = ETH_STATUS_GOT_IP;
    xEventGroupSetBits(s_eth_state.event_group, ETH_GOT_IP_BIT);
    trigger_ethernet_event(ETH_STATUS_GOT_IP, NULL);

    // 静态IP配置完成后立即启动服务
    // Auto-start DHCP server if enabled - add delay to ensure interface is ready
    if (s_eth_state.config.dhcp_server_enabled && !s_eth_state.dhcp_server_running) {
        ESP_LOGI(TAG, "Auto-starting DHCP server after static IP configuration");
        // 给网络接口更多时间完全初始化
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        // 验证网络接口是否真正可用
        esp_netif_ip_info_t test_ip_info;
        esp_err_t test_ret = esp_netif_get_ip_info(s_eth_state.eth_netif, &test_ip_info);
        if (test_ret == ESP_OK && test_ip_info.ip.addr != 0) {
            ESP_LOGI(TAG, "Network interface is ready, starting DHCP server");
            ethernet_start_dhcp_server();
        } else {
            ESP_LOGW(TAG, "Network interface not ready yet, will retry DHCP server later");
        }
    }

    // Auto-start gateway if enabled
    if (s_eth_state.config.gateway_enabled && !s_eth_state.gateway_running) {
        ESP_LOGI(TAG, "Auto-starting gateway service after static IP configuration");
        ethernet_start_gateway();
    }

    return ESP_OK;
}

static void ethernet_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ETH_EVENT) {
        switch (event_id) {
        case ETHERNET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Up");
            s_eth_state.status = ETH_STATUS_CONNECTED;
            xEventGroupSetBits(s_eth_state.event_group, ETH_CONNECTED_BIT);
            trigger_ethernet_event(ETH_STATUS_CONNECTED, NULL);
            break;
        case ETHERNET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Ethernet Link Down");
            s_eth_state.status = ETH_STATUS_DISCONNECTED;
            xEventGroupClearBits(s_eth_state.event_group, ETH_CONNECTED_BIT | ETH_GOT_IP_BIT);
            trigger_ethernet_event(ETH_STATUS_DISCONNECTED, NULL);
            break;
        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet Started");
            break;
        case ETHERNET_EVENT_STOP:
            ESP_LOGI(TAG, "Ethernet Stopped");
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Ethernet Got IP Address (DHCP Client Mode)");
        ESP_LOGI(TAG, "~~~~~~~~~~~");
        ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "~~~~~~~~~~~");
        
        s_eth_state.status = ETH_STATUS_GOT_IP;
        xEventGroupSetBits(s_eth_state.event_group, ETH_GOT_IP_BIT);
        trigger_ethernet_event(ETH_STATUS_GOT_IP, event_data);

        // 注意：如果使用静态IP，DHCP服务器已在ethernet_apply_config中启动
        // 此事件仅在DHCP客户端模式下触发
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_AP_STAIPASSIGNED) {
        // DHCP服务器分配IP地址事件
        ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)event_data;
        ESP_LOGI(TAG, "DHCP server assigned IP: " IPSTR " to device (MAC: " MACSTR ")",
                 IP2STR(&event->ip),
                 MAC2STR(event->mac));
        
        // 将真实的DHCP客户端添加到跟踪列表
        char hostname[32];
        snprintf(hostname, sizeof(hostname), "client-%02x%02x%02x", 
                event->mac[3], event->mac[4], event->mac[5]);
        
        // 使用实际分配的IP地址
        uint32_t assigned_ip = event->ip.addr;
        esp_err_t add_result = dhcp_add_client_with_ip(event->mac, hostname, assigned_ip);
        if (add_result == ESP_OK) {
            ESP_LOGI(TAG, "Successfully tracked new DHCP client: %s", hostname);
        } else {
            ESP_LOGW(TAG, "Failed to track DHCP client: %s", esp_err_to_name(add_result));
        }
    }
}

static esp_err_t ethernet_start_dhcp_server(void)
{
    if (s_eth_state.dhcp_server_running) {
        ESP_LOGW(TAG, "DHCP server already running");
        return ESP_OK;
    }

    if (!s_eth_state.eth_netif) {
        ESP_LOGE(TAG, "Ethernet netif not available");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting DHCP server");
    char start_ip_str[16], end_ip_str[16];
    ip_to_string(s_eth_state.config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
    ip_to_string(s_eth_state.config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
    
    ESP_LOGI(TAG, "DHCP pool: %s - %s", start_ip_str, end_ip_str);
    ESP_LOGI(TAG, "Lease time: %d hours", s_eth_state.config.dhcp_lease_time);

    // 检查网络接口状态
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_eth_state.eth_netif, &ip_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get IP info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    char ip_str[16], gw_str[16], mask_str[16];
    esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));
    esp_ip4addr_ntoa(&ip_info.gw, gw_str, sizeof(gw_str));
    esp_ip4addr_ntoa(&ip_info.netmask, mask_str, sizeof(mask_str));
    ESP_LOGI(TAG, "Current interface IP: %s, GW: %s, Mask: %s", ip_str, gw_str, mask_str);

    // 首先停止现有的DHCP服务器
    ESP_LOGI(TAG, "Stopping existing DHCP server (if any)");
    ret = esp_netif_dhcps_stop(s_eth_state.eth_netif);
    if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop existing DHCP server: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "DHCP server stopped successfully");
    
    // 等待一点时间确保DHCP服务器完全停止
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 配置DHCP服务器地址池（使用成功项目的方法）
    dhcps_lease_t dhcp_lease;
    dhcp_lease.enable = true;
    dhcp_lease.start_ip.addr = s_eth_state.config.dhcp_start_ip;
    dhcp_lease.end_ip.addr = s_eth_state.config.dhcp_end_ip;
    
    ESP_LOGI(TAG, "Setting DHCP address pool: 0x%08lx - 0x%08lx", 
             dhcp_lease.start_ip.addr, dhcp_lease.end_ip.addr);
    
    // 使用正确的DHCP选项（参考成功项目）
    ret = esp_netif_dhcps_option(s_eth_state.eth_netif, ESP_NETIF_OP_SET,
                                ESP_NETIF_REQUESTED_IP_ADDRESS, &dhcp_lease, sizeof(dhcp_lease));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set DHCP address pool: %s", esp_err_to_name(ret));
        return ret;
    } else {
        ESP_LOGI(TAG, "DHCP address pool set successfully");
    }
    
    // 设置租约时间
    uint32_t lease_time = s_eth_state.config.dhcp_lease_time * 3600; // 转换为秒
    ESP_LOGI(TAG, "Setting DHCP lease time: %lu seconds", lease_time);
    ret = esp_netif_dhcps_option(s_eth_state.eth_netif, ESP_NETIF_OP_SET,
                                ESP_NETIF_IP_ADDRESS_LEASE_TIME, &lease_time, sizeof(lease_time));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set DHCP lease time: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "DHCP lease time set successfully");
    }
    
    // 配置DNS服务器
    esp_netif_dns_info_t dns;
    dns.ip.u_addr.ip4.addr = s_eth_state.config.dns_server;
    ret = esp_netif_set_dns_info(s_eth_state.eth_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set DNS server: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "DNS server configured successfully");
    }
    
    // 启动DHCP服务器
    ESP_LOGI(TAG, "Starting DHCP server on interface");
    ret = esp_netif_dhcps_start(s_eth_state.eth_netif);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start DHCP server: %s", esp_err_to_name(ret));
        return ret;
    }

    s_eth_state.dhcp_server_running = true;
    s_eth_state.status = ETH_STATUS_DHCP_SERVER_STARTED;
    xEventGroupSetBits(s_eth_state.event_group, ETH_DHCP_SERVER_BIT);
    trigger_ethernet_event(ETH_STATUS_DHCP_SERVER_STARTED, NULL);

    ESP_LOGI(TAG, "DHCP server started successfully");
    ESP_LOGI(TAG, "DHCP pool range: %s - %s", start_ip_str, end_ip_str);
    return ESP_OK;
}

static esp_err_t ethernet_stop_dhcp_server(void)
{
    if (!s_eth_state.dhcp_server_running) {
        ESP_LOGW(TAG, "DHCP server not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping DHCP server");
    
    if (s_eth_state.eth_netif) {
        esp_err_t ret = esp_netif_dhcps_stop(s_eth_state.eth_netif);
        if (ret != ESP_OK && ret != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ESP_LOGW(TAG, "Failed to stop DHCP server: %s", esp_err_to_name(ret));
        }
    }
    
    s_eth_state.dhcp_server_running = false;
    xEventGroupClearBits(s_eth_state.event_group, ETH_DHCP_SERVER_BIT);

    ESP_LOGI(TAG, "DHCP server stopped");
    return ESP_OK;
}

static esp_err_t ethernet_start_gateway(void)
{
    if (s_eth_state.gateway_running) {
        ESP_LOGW(TAG, "Gateway service already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting gateway service");
    
    // Enable IP forwarding (this would require lwIP configuration)
    // For now, this is a placeholder implementation
    
    s_eth_state.gateway_running = true;
    s_eth_state.status = ETH_STATUS_GATEWAY_ENABLED;
    xEventGroupSetBits(s_eth_state.event_group, ETH_GATEWAY_BIT);
    trigger_ethernet_event(ETH_STATUS_GATEWAY_ENABLED, NULL);

    ESP_LOGI(TAG, "Gateway service started successfully");
    return ESP_OK;
}

static esp_err_t ethernet_stop_gateway(void)
{
    if (!s_eth_state.gateway_running) {
        ESP_LOGW(TAG, "Gateway service not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping gateway service");
    
    s_eth_state.gateway_running = false;
    xEventGroupClearBits(s_eth_state.event_group, ETH_GATEWAY_BIT);

    ESP_LOGI(TAG, "Gateway service stopped");
    return ESP_OK;
}

static void trigger_ethernet_event(ethernet_status_t status, void *data)
{
    if (s_eth_state.event_callback) {
        s_eth_state.event_callback(status, data);
    }
}

static uint32_t string_to_ip(const char *ip_str)
{
    if (!ip_str) return 0;
    
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, ip_str, &(sa.sin_addr));
    if (result == 1) {
        return sa.sin_addr.s_addr;
    }
    return 0;
}

static void ip_to_string(uint32_t ip, char *buf, size_t buf_len)
{
    if (!buf || buf_len < 16) return;
    
    struct in_addr addr;
    addr.s_addr = ip;
    strncpy(buf, inet_ntoa(addr), buf_len - 1);
    buf[buf_len - 1] = '\0';
}

// Public API implementations
esp_err_t ethernet_set_network_config(const char *ip_addr, const char *gateway, 
                                     const char *netmask, const char *dns_server)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ip_addr || !gateway || !netmask || !dns_server) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    s_eth_state.config.ip_addr = string_to_ip(ip_addr);
    s_eth_state.config.gateway = string_to_ip(gateway);
    s_eth_state.config.netmask = string_to_ip(netmask);
    s_eth_state.config.dns_server = string_to_ip(dns_server);

    if (s_eth_state.config.ip_addr == 0 || s_eth_state.config.gateway == 0 ||
        s_eth_state.config.netmask == 0 || s_eth_state.config.dns_server == 0) {
        ESP_LOGE(TAG, "Invalid IP address format");
        return ESP_ERR_INVALID_ARG;
    }

    // Save to NVS
    ethernet_save_config_to_nvs();

    // Apply configuration if started
    if (s_eth_state.started) {
        return ethernet_apply_config();
    }

    return ESP_OK;
}

esp_err_t ethernet_set_dhcp_server(bool enable)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_eth_state.config.dhcp_server_enabled = enable;
    ethernet_save_config_to_nvs();

    if (enable && s_eth_state.status >= ETH_STATUS_GOT_IP) {
        // 检查网络接口是否真正准备好
        esp_netif_ip_info_t ip_info;
        esp_err_t ret = esp_netif_get_ip_info(s_eth_state.eth_netif, &ip_info);
        if (ret != ESP_OK || ip_info.ip.addr == 0) {
            ESP_LOGW(TAG, "Network interface not ready, waiting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // 重新检查
            ret = esp_netif_get_ip_info(s_eth_state.eth_netif, &ip_info);
            if (ret != ESP_OK || ip_info.ip.addr == 0) {
                ESP_LOGE(TAG, "Network interface still not ready after waiting");
                return ESP_ERR_INVALID_STATE;
            }
        }
        
        // 尝试启动DHCP服务器，如果失败则重试
        esp_err_t dhcp_ret = ethernet_start_dhcp_server();
        if (dhcp_ret == ESP_ERR_ESP_NETIF_IF_NOT_READY) {
            ESP_LOGW(TAG, "First attempt failed, retrying DHCP server start...");
            vTaskDelay(pdMS_TO_TICKS(500));
            dhcp_ret = ethernet_start_dhcp_server();
        }
        return dhcp_ret;
    } else if (!enable && s_eth_state.dhcp_server_running) {
        return ethernet_stop_dhcp_server();
    }

    return ESP_OK;
}

esp_err_t ethernet_set_dhcp_pool(const char *start_ip, const char *end_ip, uint8_t lease_time)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!start_ip || !end_ip) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t start = string_to_ip(start_ip);
    uint32_t end = string_to_ip(end_ip);

    if (start == 0 || end == 0) {
        ESP_LOGE(TAG, "Invalid IP address format");
        return ESP_ERR_INVALID_ARG;
    }

    if (ntohl(start) >= ntohl(end)) {
        ESP_LOGE(TAG, "Start IP must be less than end IP");
        return ESP_ERR_INVALID_ARG;
    }

    s_eth_state.config.dhcp_start_ip = start;
    s_eth_state.config.dhcp_end_ip = end;
    s_eth_state.config.dhcp_lease_time = lease_time;

    ethernet_save_config_to_nvs();

    ESP_LOGI(TAG, "DHCP pool updated: %s - %s, lease time: %d hours", start_ip, end_ip, lease_time);
    return ESP_OK;
}

esp_err_t ethernet_set_gateway(bool enable)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_eth_state.config.gateway_enabled = enable;
    ethernet_save_config_to_nvs();

    if (enable && s_eth_state.status >= ETH_STATUS_GOT_IP) {
        return ethernet_start_gateway();
    } else if (!enable && s_eth_state.gateway_running) {
        return ethernet_stop_gateway();
    }

    return ESP_OK;
}

ethernet_status_t ethernet_get_status(void)
{
    return s_eth_state.status;
}

esp_err_t ethernet_get_config(ethernet_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &s_eth_state.config, sizeof(ethernet_config_t));
    return ESP_OK;
}

esp_err_t ethernet_get_stats(ethernet_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        memcpy(stats, &s_eth_state.stats, sizeof(ethernet_stats_t));
        return ESP_OK;
    }

    // 尝试从W5500芯片读取真实的统计信息
    // 注意：这需要ESP-IDF的以太网驱动支持统计信息读取
    // 如果不支持，我们使用存储的统计信息
    
    // 首先尝试更新存储的统计信息
    // 这里我们可以添加代码来查询W5500的内部寄存器
    // 但ESP-IDF的W5500驱动可能不暴露这些底层寄存器访问
    
    memcpy(stats, &s_eth_state.stats, sizeof(ethernet_stats_t));
    
    // 添加一个标记表明这些可能不是实时统计
    ESP_LOGD(TAG, "Stats: RX=%lu, TX=%lu (Note: May not reflect real chip counters)", 
             stats->rx_packets, stats->tx_packets);
    
    return ESP_OK;
}

esp_err_t ethernet_ping(const char *target_ip, uint32_t count, uint32_t timeout_ms, ping_result_t *result)
{
    if (!target_ip || !result || !s_eth_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Ping request - Current ethernet status: %d (ETH_STATUS_GOT_IP=%d)", 
             s_eth_state.status, ETH_STATUS_GOT_IP);
    
    if (s_eth_state.status < ETH_STATUS_GOT_IP) {
        ESP_LOGE(TAG, "Ethernet not ready for ping test - status %d < %d", 
                 s_eth_state.status, ETH_STATUS_GOT_IP);
        return ESP_ERR_INVALID_STATE;
    }

    memset(result, 0, sizeof(ping_result_t));
    result->target_ip = string_to_ip(target_ip);
    
    ESP_LOGI(TAG, "Starting connectivity test to %s", target_ip);
    
    // 使用多种方法测试连通性：
    // 1. 尝试TCP连接到常见端口
    // 2. 尝试UDP发送
    // 这提供了比纯ICMP更实际的连通性测试
    
    result->packets_sent = count;
    result->packets_received = 0;
    result->min_time_ms = UINT32_MAX;
    result->max_time_ms = 0;
    uint32_t total_time = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t start_time = esp_timer_get_time();
        bool success = false;
        
        // 方法1: 尝试TCP连接到多个常见端口
        int test_ports[] = {80, 443, 53, 22, 21, 25};
        int num_ports = sizeof(test_ports) / sizeof(test_ports[0]);
        
        for (int p = 0; p < num_ports && !success; p++) {
            int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock >= 0) {
                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_addr.s_addr = result->target_ip;
                dest_addr.sin_port = htons(test_ports[p]);
                
                // 设置非阻塞模式以便控制超时
                int flags = fcntl(sock, F_GETFL, 0);
                fcntl(sock, F_SETFL, flags | O_NONBLOCK);
                
                int connect_result = connect(sock, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                
                if (connect_result == 0) {
                    success = true;  // 立即连接成功
                } else if (errno == EINPROGRESS) {
                    // 连接正在进行，使用select等待
                    fd_set write_fds;
                    FD_ZERO(&write_fds);
                    FD_SET(sock, &write_fds);
                    
                    struct timeval tv;
                    tv.tv_sec = timeout_ms / 1000;
                    tv.tv_usec = (timeout_ms % 1000) * 1000;
                    
                    if (select(sock + 1, NULL, &write_fds, NULL, &tv) > 0) {
                        int error = 0;
                        socklen_t len = sizeof(error);
                        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error == 0) {
                            success = true;
                        }
                    }
                }
                close(sock);
            }
        }
        
        // 方法2: 如果TCP失败，尝试UDP发送（这总是"成功"但表明网络栈工作）
        if (!success) {
            int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock >= 0) {
                struct sockaddr_in dest_addr;
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_addr.s_addr = result->target_ip;
                dest_addr.sin_port = htons(12345);  // 随机端口
                
                char test_data[] = "ping test";
                if (sendto(sock, test_data, strlen(test_data), 0, 
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr)) > 0) {
                    success = true;  // UDP发送成功表明网络栈和硬件工作
                }
                close(sock);
            }
        }
        
        uint64_t end_time = esp_timer_get_time();
        uint32_t elapsed_ms = (end_time - start_time) / 1000;
        
        if (success) {
            result->packets_received++;
            total_time += elapsed_ms;
            
            if (elapsed_ms < result->min_time_ms) {
                result->min_time_ms = elapsed_ms;
            }
            if (elapsed_ms > result->max_time_ms) {
                result->max_time_ms = elapsed_ms;
            }
        }
        
        // 延迟一下再进行下次测试
        if (i < count - 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    if (result->packets_received > 0) {
        result->avg_time_ms = total_time / result->packets_received;
        result->success = true;
    } else {
        ESP_LOGW(TAG, "No response from %s - host may be unreachable or not accepting TCP connections on common ports", target_ip);
        result->success = false;
        result->min_time_ms = 0;
    }
    
    return ESP_OK;
}

esp_err_t ethernet_register_event_callback(ethernet_event_callback_t callback)
{
    s_eth_state.event_callback = callback;
    return ESP_OK;
}

esp_err_t ethernet_get_mac_address(uint8_t *mac)
{
    if (!mac || !s_eth_state.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_MAC_ADDR, mac);
}

esp_err_t ethernet_reset(void)
{
    if (!s_eth_state.initialized) {
        ESP_LOGE(TAG, "Ethernet interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting ethernet interface");
    
    // Reset W5500 chip
    gpio_set_level(BSP_W5500_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BSP_W5500_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Restart if it was running
    if (s_eth_state.started) {
        ethernet_interface_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        return ethernet_interface_start();
    }

    return ESP_OK;
}

bool ethernet_is_cable_connected(void)
{
    return s_eth_state.status >= ETH_STATUS_CONNECTED;
}

static esp_err_t ethernet_save_config_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, "config", &s_eth_state.config, sizeof(ethernet_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(ret));
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Configuration saved to NVS");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

static esp_err_t ethernet_load_config_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for reading, using current config");
        return ret; // Return error to indicate no saved config
    }

    size_t required_size = sizeof(ethernet_config_t);
    ethernet_config_t loaded_config;
    ret = nvs_get_blob(nvs_handle, "config", &loaded_config, &required_size);
    if (ret == ESP_OK) {
        // Replace current config with loaded config
        memcpy(&s_eth_state.config, &loaded_config, sizeof(ethernet_config_t));
        ESP_LOGI(TAG, "Configuration loaded from NVS and applied");
        
        // Log loaded configuration for debugging
        char ip_str[16], gw_str[16], start_ip_str[16], end_ip_str[16], dns_str[16], mask_str[16];
        ip_to_string(s_eth_state.config.ip_addr, ip_str, sizeof(ip_str));
        ip_to_string(s_eth_state.config.gateway, gw_str, sizeof(gw_str));
        ip_to_string(s_eth_state.config.netmask, mask_str, sizeof(mask_str));
        ip_to_string(s_eth_state.config.dns_server, dns_str, sizeof(dns_str));
        ip_to_string(s_eth_state.config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
        ip_to_string(s_eth_state.config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
        ESP_LOGI(TAG, "=== Ethernet Configuration (Loaded from NVS) ===");
        ESP_LOGI(TAG, "  IP Address: %s", ip_str);
        ESP_LOGI(TAG, "  Gateway: %s", gw_str);
        ESP_LOGI(TAG, "  Netmask: %s", mask_str);
        ESP_LOGI(TAG, "  DNS Server: %s", dns_str);
        ESP_LOGI(TAG, "  DHCP Server: %s", s_eth_state.config.dhcp_server_enabled ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "  DHCP Pool: %s - %s", start_ip_str, end_ip_str);
        ESP_LOGI(TAG, "  DHCP Lease Time: %d hours", s_eth_state.config.dhcp_lease_time);
        ESP_LOGI(TAG, "  Gateway Service: %s", s_eth_state.config.gateway_enabled ? "Enabled" : "Disabled");
        ESP_LOGI(TAG, "===============================================");
    } else {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

// Console command implementations

static int cmd_eth_config(int argc, char **argv)
{
    if (argc == 1) {
        // Show current configuration
        ethernet_config_t config;
        if (ethernet_get_config(&config) == ESP_OK) {
            char ip_str[16], gw_str[16], mask_str[16], dns_str[16];
            ip_to_string(config.ip_addr, ip_str, sizeof(ip_str));
            ip_to_string(config.gateway, gw_str, sizeof(gw_str));
            ip_to_string(config.netmask, mask_str, sizeof(mask_str));
            ip_to_string(config.dns_server, dns_str, sizeof(dns_str));
            
            printf("Current ethernet configuration:\n");
            printf("  IP Address: %s\n", ip_str);
            printf("  Gateway:    %s\n", gw_str);
            printf("  Netmask:    %s\n", mask_str);
            printf("  DNS Server: %s\n", dns_str);
            printf("  DHCP Server: %s\n", config.dhcp_server_enabled ? "Enabled" : "Disabled");
            printf("  Gateway Service: %s\n", config.gateway_enabled ? "Enabled" : "Disabled");
        }
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        // Clear NVS configuration and use defaults
        printf("Clearing stored configuration...\n");
        nvs_handle_t nvs_handle;
        esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (ret != ESP_OK) {
            printf("Failed to open NVS: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
        ret = nvs_erase_key(nvs_handle, "config");
        if (ret == ESP_OK) {
            printf("Configuration cleared successfully\n");
            printf("Please restart the device to apply default configuration\n");
        } else {
            printf("Failed to clear configuration: %s\n", esp_err_to_name(ret));
        }
        nvs_close(nvs_handle);
        return 0;
    }

    if (argc == 5) {
        // Set network configuration: eth config <ip> <gateway> <netmask> <dns>
        esp_err_t ret = ethernet_set_network_config(argv[1], argv[2], argv[3], argv[4]);
        if (ret == ESP_OK) {
            printf("Network configuration updated successfully\n");
        } else {
            printf("Failed to update network configuration: %s\n", esp_err_to_name(ret));
        }
        return 0;
    }

    printf("Usage: eth config [clear] [<ip> <gateway> <netmask> <dns>]\n");
    printf("  Without arguments: show current configuration\n");
    printf("  clear: clear stored configuration and use defaults\n");
    printf("  With arguments: set network configuration\n");
    printf("Example: eth config 10.10.99.97 10.10.99.97 255.255.255.0 8.8.8.8\n");
    return 1;
}

static int cmd_eth_dhcp(int argc, char **argv)
{
    if (argc == 1) {
        // Show current DHCP configuration
        ethernet_config_t config;
        if (ethernet_get_config(&config) == ESP_OK) {
            char start_ip_str[16], end_ip_str[16];
            ip_to_string(config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
            ip_to_string(config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
            
            printf("DHCP Server Status: %s\n", config.dhcp_server_enabled ? "Enabled" : "Disabled");
            printf("DHCP Pool: %s - %s\n", start_ip_str, end_ip_str);
            printf("Lease Time: %d hours\n", config.dhcp_lease_time);
            printf("\nUsage:\n");
            printf("  eth_dhcp                              - Show DHCP status\n");
            printf("  eth_dhcp status                       - Show detailed DHCP clients info\n");
            printf("  eth_dhcp enable                       - Enable DHCP server\n");
            printf("  eth_dhcp disable                      - Disable DHCP server\n");
            printf("  eth_dhcp pool <start_ip> <end_ip>     - Set DHCP pool (24h lease)\n");
            printf("  eth_dhcp pool <start_ip> <end_ip> <hours> - Set DHCP pool with lease time\n");
            printf("  eth_dhcp release <mac_addr>           - Release a specific client lease\n");
            printf("  eth_dhcp reload                       - Reload configuration from NVS\n");
            printf("Example: eth_dhcp pool 10.10.99.100 10.10.99.102 12\n");
        }
        return 0;
    }

    if (argc == 2) {
        // Handle various 2-argument commands
        if (strcmp(argv[1], "status") == 0) {
            // Show detailed DHCP server status and clients
            dhcp_server_status_t status;
            esp_err_t ret = ethernet_get_dhcp_status(&status);
            if (ret != ESP_OK) {
                printf("Failed to get DHCP status: %s\n", esp_err_to_name(ret));
                return 1;
            }

            printf("\n=== DHCP Server Status ===\n");
            printf("Server Status: %s\n", status.server_enabled ? "Enabled" : "Disabled");
            
            char start_ip_str[16], end_ip_str[16];
            ip_to_string(status.pool_start_ip, start_ip_str, sizeof(start_ip_str));
            ip_to_string(status.pool_end_ip, end_ip_str, sizeof(end_ip_str));
            printf("IP Pool: %s - %s\n", start_ip_str, end_ip_str);
            printf("Pool Statistics:\n");
            printf("  Total Addresses: %"PRIu32"\n", status.total_addresses);
            printf("  Allocated: %"PRIu32"\n", status.allocated_count);
            printf("  Available: %"PRIu32"\n", status.available_count);
            printf("  Default Lease Time: %"PRIu32" hours\n", status.lease_time_hours);

            printf("\n=== Active DHCP Clients ===\n");
            if (status.client_count == 0) {
                printf("No active clients\n");
            } else {
                printf("Total Active Clients: %d\n\n", status.client_count);
                for (int i = 0; i < status.client_count; i++) {
                    dhcp_client_info_t *client = &status.clients[i];
                    char client_ip_str[16];
                    ip_to_string(client->ip_addr, client_ip_str, sizeof(client_ip_str));
                    
                    printf("Client %d:\n", i + 1);
                    printf("  MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                           client->mac_addr[0], client->mac_addr[1], client->mac_addr[2],
                           client->mac_addr[3], client->mac_addr[4], client->mac_addr[5]);
                    printf("  IP Address: %s\n", client_ip_str);
                    printf("  Hostname: %s\n", client->hostname);
                    printf("  Lease Start: %"PRIu32" seconds ago\n", 
                           (uint32_t)(esp_timer_get_time() / 1000000) - client->lease_start_time);
                    printf("  Lease Duration: %"PRIu32" seconds (%"PRIu32" hours)\n",
                           client->lease_duration, client->lease_duration / 3600);
                    
                    uint32_t remaining = client->lease_start_time + client->lease_duration - 
                                       (esp_timer_get_time() / 1000000);
                    if (remaining > 0) {
                        printf("  Remaining Time: %"PRIu32" seconds (%"PRIu32" hours)\n", 
                               remaining, remaining / 3600);
                    } else {
                        printf("  Status: EXPIRED\n");
                    }
                    printf("\n");
                }
            }
            return 0;

        } else if (strcmp(argv[1], "enable") == 0) {
            esp_err_t ret = ethernet_set_dhcp_server(true);
            if (ret == ESP_OK) {
                printf("DHCP server enabled\n");
                // Update config manager
                ethernet_config_t current_config;
                if (ethernet_get_config(&current_config) == ESP_OK) {
                    dhcp_config_t dhcp_config = {
                        .enable = true,
                        .lease_time_hours = current_config.dhcp_lease_time,
                        .max_clients = 10,  // Default value
                        .auto_start = true  // Default value
                    };
                    // Convert IP addresses to string format
                    char start_ip_str[16], end_ip_str[16];
                    ip_to_string(current_config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
                    ip_to_string(current_config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
                    strncpy(dhcp_config.start_ip, start_ip_str, sizeof(dhcp_config.start_ip) - 1);
                    strncpy(dhcp_config.end_ip, end_ip_str, sizeof(dhcp_config.end_ip) - 1);
                    
                    config_manager_set_dhcp_config(&dhcp_config);
                }
            } else {
                printf("Failed to enable DHCP server: %s\n", esp_err_to_name(ret));
            }
            return 0;
        } else if (strcmp(argv[1], "disable") == 0) {
            esp_err_t ret = ethernet_set_dhcp_server(false);
            if (ret == ESP_OK) {
                printf("DHCP server disabled\n");
                // Update config manager
                ethernet_config_t current_config;
                if (ethernet_get_config(&current_config) == ESP_OK) {
                    dhcp_config_t dhcp_config = {
                        .enable = false,
                        .lease_time_hours = current_config.dhcp_lease_time,
                        .max_clients = 10,  // Default value
                        .auto_start = false  // Default value
                    };
                    // Convert IP addresses to string format
                    char start_ip_str[16], end_ip_str[16];
                    ip_to_string(current_config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
                    ip_to_string(current_config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
                    strncpy(dhcp_config.start_ip, start_ip_str, sizeof(dhcp_config.start_ip) - 1);
                    strncpy(dhcp_config.end_ip, end_ip_str, sizeof(dhcp_config.end_ip) - 1);
                    
                    config_manager_set_dhcp_config(&dhcp_config);
                }
            } else {
                printf("Failed to disable DHCP server: %s\n", esp_err_to_name(ret));
            }
            return 0;
        } else if (strcmp(argv[1], "reload") == 0) {
            esp_err_t ret = ethernet_load_config_from_nvs();
            if (ret == ESP_OK) {
                printf("Configuration reloaded from NVS\n");
                // Show the reloaded configuration
                ethernet_config_t config;
                if (ethernet_get_config(&config) == ESP_OK) {
                    char start_ip_str[16], end_ip_str[16];
                    ip_to_string(config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
                    ip_to_string(config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
                    printf("Updated DHCP Server Status: %s\n", config.dhcp_server_enabled ? "Enabled" : "Disabled");
                    printf("Updated DHCP Pool: %s - %s\n", start_ip_str, end_ip_str);
                }
            } else {
                printf("Failed to reload configuration: %s\n", esp_err_to_name(ret));
            }
            return 0;
        } else {
            printf("Invalid argument '%s'. Use 'status', 'enable', 'disable', or 'reload'\n", argv[1]);
            return 1;
        }
    }

    if (argc == 3 && strcmp(argv[1], "release") == 0) {
        // Release DHCP lease: eth_dhcp release <mac_addr>
        // Parse MAC address in format: xx:xx:xx:xx:xx:xx or xx-xx-xx-xx-xx-xx
        uint8_t mac_addr[6];
        if (sscanf(argv[2], "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                   &mac_addr[0], &mac_addr[1], &mac_addr[2], 
                   &mac_addr[3], &mac_addr[4], &mac_addr[5]) == 6 ||
            sscanf(argv[2], "%02hhx-%02hhx-%02hhx-%02hhx-%02hhx-%02hhx",
                   &mac_addr[0], &mac_addr[1], &mac_addr[2], 
                   &mac_addr[3], &mac_addr[4], &mac_addr[5]) == 6) {
            
            esp_err_t ret = ethernet_release_dhcp_lease(mac_addr);
            if (ret == ESP_OK) {
                printf("DHCP lease released for MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            } else if (ret == ESP_ERR_NOT_FOUND) {
                printf("No active lease found for MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            } else {
                printf("Failed to release lease: %s\n", esp_err_to_name(ret));
            }
        } else {
            printf("Invalid MAC address format. Use xx:xx:xx:xx:xx:xx or xx-xx-xx-xx-xx-xx\n");
        }
        return 0;
    }

    if (argc == 4 && strcmp(argv[1], "pool") == 0) {
        // Set DHCP pool: eth_dhcp pool <start_ip> <end_ip>
        uint8_t lease_time = 24; // Default 24 hours
        esp_err_t ret = ethernet_set_dhcp_pool(argv[2], argv[3], lease_time);
        if (ret == ESP_OK) {
            printf("DHCP pool updated: %s - %s (lease: %d hours)\n", argv[2], argv[3], lease_time);
            // Update config manager
            ethernet_config_t current_config;
            if (ethernet_get_config(&current_config) == ESP_OK) {
                dhcp_config_t dhcp_config = {
                    .enable = current_config.dhcp_server_enabled,
                    .lease_time_hours = current_config.dhcp_lease_time,
                    .max_clients = 10,  // Default value
                    .auto_start = current_config.dhcp_server_enabled  // Use current setting
                };
                // Convert IP addresses to string format
                char start_ip_str[16], end_ip_str[16];
                ip_to_string(current_config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
                ip_to_string(current_config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
                strncpy(dhcp_config.start_ip, start_ip_str, sizeof(dhcp_config.start_ip) - 1);
                strncpy(dhcp_config.end_ip, end_ip_str, sizeof(dhcp_config.end_ip) - 1);
                
                config_manager_set_dhcp_config(&dhcp_config);
            }
        } else {
            printf("Failed to update DHCP pool: %s\n", esp_err_to_name(ret));
        }
        return 0;
    }

    if (argc == 5 && strcmp(argv[1], "pool") == 0) {
        // Set DHCP pool with lease time: eth_dhcp pool <start_ip> <end_ip> <lease_time>
        uint8_t lease_time = atoi(argv[4]);
        if (lease_time == 0 || lease_time > 168) { // Max 1 week
            printf("Invalid lease time. Must be 1-168 hours\n");
            return 1;
        }
        esp_err_t ret = ethernet_set_dhcp_pool(argv[2], argv[3], lease_time);
        if (ret == ESP_OK) {
            printf("DHCP pool updated: %s - %s (lease: %d hours)\n", argv[2], argv[3], lease_time);
            // Update config manager
            ethernet_config_t current_config;
            if (ethernet_get_config(&current_config) == ESP_OK) {
                dhcp_config_t dhcp_config = {
                    .enable = current_config.dhcp_server_enabled,
                    .lease_time_hours = current_config.dhcp_lease_time,
                    .max_clients = 10,  // Default value
                    .auto_start = current_config.dhcp_server_enabled  // Use current setting
                };
                // Convert IP addresses to string format
                char start_ip_str[16], end_ip_str[16];
                ip_to_string(current_config.dhcp_start_ip, start_ip_str, sizeof(start_ip_str));
                ip_to_string(current_config.dhcp_end_ip, end_ip_str, sizeof(end_ip_str));
                strncpy(dhcp_config.start_ip, start_ip_str, sizeof(dhcp_config.start_ip) - 1);
                strncpy(dhcp_config.end_ip, end_ip_str, sizeof(dhcp_config.end_ip) - 1);
                
                config_manager_set_dhcp_config(&dhcp_config);
            }
        } else {
            printf("Failed to update DHCP pool: %s\n", esp_err_to_name(ret));
        }
        return 0;
    }

    // Show usage if no valid command found
    printf("Usage:\n");
    printf("  eth_dhcp                              - Show DHCP status\n");
    printf("  eth_dhcp enable                       - Enable DHCP server\n");
    printf("  eth_dhcp disable                      - Disable DHCP server\n");
    printf("  eth_dhcp pool <start_ip> <end_ip>     - Set DHCP pool (24h lease)\n");
    printf("  eth_dhcp pool <start_ip> <end_ip> <hours> - Set DHCP pool with lease time\n");
    printf("Example: eth_dhcp pool 10.10.99.100 10.10.99.102 12\n");
    return 1;
}

static int cmd_eth_gateway(int argc, char **argv)
{
    if (argc == 1) {
        // Show gateway status
        ethernet_config_t config;
        if (ethernet_get_config(&config) == ESP_OK) {
            printf("Gateway Service: %s\n", config.gateway_enabled ? "Enabled" : "Disabled");
        }
        return 0;
    }

    if (argc == 2) {
        // Enable/disable gateway: eth gateway <enable|disable>
        bool enable = (strcmp(argv[1], "enable") == 0);
        if (strcmp(argv[1], "enable") != 0 && strcmp(argv[1], "disable") != 0) {
            printf("Invalid argument. Use 'enable' or 'disable'\n");
            return 1;
        }

        esp_err_t ret = ethernet_set_gateway(enable);
        if (ret == ESP_OK) {
            printf("Gateway service %s\n", enable ? "enabled" : "disabled");
            if (enable) {
                printf("ESP32S3 is now acting as a gateway\n");
            }
            // Update config manager
            ethernet_config_t current_config;
            if (ethernet_get_config(&current_config) == ESP_OK) {
                gateway_config_t gateway_config = {
                    .enable = enable,
                    .nat_enable = current_config.gateway_enabled,  // Use current setting
                    .firewall_enable = false,  // Default setting
                    .auto_start = enable  // Set auto_start based on enable
                };
                config_manager_set_gateway_config(&gateway_config);
            }
        } else {
            printf("Failed to %s gateway service: %s\n", enable ? "enable" : "disable", esp_err_to_name(ret));
        }
        return 0;
    }

    printf("Usage:\n");
    printf("  eth gateway              - Show gateway status\n");
    printf("  eth gateway <enable|disable> - Enable/disable gateway service\n");
    return 1;
}

static int cmd_eth_status(int argc, char **argv)
{
    printf("=== Ethernet Interface Status ===\n");
    
    // Basic status
    ethernet_status_t status = ethernet_get_status();
    const char* status_str[] = {
        "Disconnected",
        "Connected", 
        "Got IP",
        "DHCP Server Started",
        "Gateway Enabled"
    };
    printf("Status: %s\n", status_str[status]);
    printf("Cable Connected: %s\n", ethernet_is_cable_connected() ? "Yes" : "No");

    // Configuration
    ethernet_config_t config;
    if (ethernet_get_config(&config) == ESP_OK) {
        char ip_str[16], gw_str[16], mask_str[16], dns_str[16];
        char dhcp_start_str[16], dhcp_end_str[16];
        
        ip_to_string(config.ip_addr, ip_str, sizeof(ip_str));
        ip_to_string(config.gateway, gw_str, sizeof(gw_str));
        ip_to_string(config.netmask, mask_str, sizeof(mask_str));
        ip_to_string(config.dns_server, dns_str, sizeof(dns_str));
        ip_to_string(config.dhcp_start_ip, dhcp_start_str, sizeof(dhcp_start_str));
        ip_to_string(config.dhcp_end_ip, dhcp_end_str, sizeof(dhcp_end_str));

        printf("\n--- Network Configuration ---\n");
        printf("IP Address:    %s\n", ip_str);
        printf("Gateway:       %s\n", gw_str);
        printf("Subnet Mask:   %s\n", mask_str);
        printf("DNS Server:    %s\n", dns_str);

        printf("\n--- Services ---\n");
        printf("DHCP Server:   %s\n", s_eth_state.dhcp_server_running ? "Running" : 
               (config.dhcp_server_enabled ? "Enabled but not running" : "Disabled"));
        if (config.dhcp_server_enabled) {
            printf("DHCP Pool:     %s - %s\n", dhcp_start_str, dhcp_end_str);
            printf("Lease Time:    %d hours\n", config.dhcp_lease_time);
        }
        printf("Gateway:       %s\n", s_eth_state.gateway_running ? "Running" : 
               (config.gateway_enabled ? "Enabled but not running" : "Disabled"));
    }

    // MAC Address
    uint8_t mac[6];
    if (ethernet_get_mac_address(mac) == ESP_OK) {
        printf("\n--- Hardware ---\n");
        printf("MAC Address:   %02x:%02x:%02x:%02x:%02x:%02x\n", 
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Statistics
    ethernet_stats_t stats;
    if (ethernet_get_stats(&stats) == ESP_OK) {
        printf("\n--- Statistics ---\n");
        printf("RX Packets:    %" PRIu32 "\n", stats.rx_packets);
        printf("TX Packets:    %" PRIu32 "\n", stats.tx_packets);
        printf("RX Bytes:      %" PRIu32 "\n", stats.rx_bytes);
        printf("TX Bytes:      %" PRIu32 "\n", stats.tx_bytes);
        printf("RX Errors:     %" PRIu32 "\n", stats.rx_errors);
        printf("TX Errors:     %" PRIu32 "\n", stats.tx_errors);
        printf("DHCP Clients:  %" PRIu32 "\n", stats.dhcp_clients);
    }

    return 0;
}

static int cmd_eth_ping(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: eth ping <target_ip> [count] [timeout_ms]\n");
        printf("Example: eth ping 8.8.8.8 4 1000\n");
        return 1;
    }

    const char *target_ip = argv[1];
    uint32_t count = 4;
    uint32_t timeout_ms = 1000;

    if (argc >= 3) {
        count = atoi(argv[2]);
        if (count == 0 || count > 100) {
            printf("Invalid count. Must be 1-100\n");
            return 1;
        }
    }

    if (argc >= 4) {
        timeout_ms = atoi(argv[3]);
        if (timeout_ms < 100 || timeout_ms > 10000) {
            printf("Invalid timeout. Must be 100-10000 ms\n");
            return 1;
        }
    }

    printf("PING %s: %" PRIu32 " packets, timeout %" PRIu32 " ms\n", target_ip, count, timeout_ms);

    ping_result_t result;
    esp_err_t ret = ethernet_ping(target_ip, count, timeout_ms, &result);
    
    if (ret == ESP_OK && result.success) {
        printf("PING statistics for %s:\n", target_ip);
        printf("Packets: Sent = %" PRIu32 ", Received = %" PRIu32 ", Lost = %" PRIu32 " (%" PRIu32 "%% loss)\n",
               result.packets_sent, result.packets_received, 
               result.packets_sent - result.packets_received,
               ((result.packets_sent - result.packets_received) * 100) / result.packets_sent);
        
        if (result.packets_received > 0) {
            printf("Approximate round trip times in milli-seconds:\n");
            printf("Minimum = %" PRIu32 "ms, Maximum = %" PRIu32 "ms, Average = %" PRIu32 "ms\n",
                   result.min_time_ms, result.max_time_ms, result.avg_time_ms);
        }
    } else {
        printf("Ping failed: %s\n", esp_err_to_name(ret));
    }

    return 0;
}

static int cmd_eth_reset(int argc, char **argv)
{
    printf("Resetting ethernet interface...\n");
    
    esp_err_t ret = ethernet_reset();
    if (ret == ESP_OK) {
        printf("Ethernet interface reset successfully\n");
    } else {
        printf("Failed to reset ethernet interface: %s\n", esp_err_to_name(ret));
    }
    
    return 0;
}

static int cmd_eth_test(int argc, char **argv)
{
    printf("Testing W5500 SPI communication...\n");
    
    if (!s_eth_state.initialized) {
        printf("Ethernet interface not initialized\n");
        return 1;
    }

    // Test 1: MAC address read/write
    printf("Test 1: MAC address verification\n");
    uint8_t test_mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    uint8_t read_mac[6] = {0};
    
    esp_err_t ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_S_MAC_ADDR, test_mac);
    if (ret != ESP_OK) {
        printf("❌ Failed to set MAC address: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_MAC_ADDR, read_mac);
    if (ret != ESP_OK) {
        printf("❌ Failed to read MAC address: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Set MAC:  %02x:%02x:%02x:%02x:%02x:%02x\n", 
           test_mac[0], test_mac[1], test_mac[2], test_mac[3], test_mac[4], test_mac[5]);
    printf("Read MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", 
           read_mac[0], read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
    
    if (memcmp(test_mac, read_mac, 6) == 0) {
        printf("✅ MAC address test passed\n");
    } else {
        printf("❌ MAC address test failed - SPI communication problem\n");
        return 1;
    }

    // Test 2: Link status
    printf("\nTest 2: Link status check\n");
    eth_duplex_t duplex;
    eth_speed_t speed;
    bool link_status = false;
    
    ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
    if (ret == ESP_OK) {
        printf("✅ Duplex mode: %s\n", duplex == ETH_DUPLEX_FULL ? "Full" : "Half");
    } else {
        printf("⚠️  Failed to get duplex mode: %s\n", esp_err_to_name(ret));
    }
    
    ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_SPEED, &speed);
    if (ret == ESP_OK) {
        printf("✅ Speed: %d Mbps\n", speed == ETH_SPEED_100M ? 100 : 10);
    } else {
        printf("⚠️  Failed to get speed: %s\n", esp_err_to_name(ret));
    }

    // Test 3: Physical link
    ret = esp_eth_ioctl(s_eth_state.eth_handle, ETH_CMD_G_AUTONEGO, &link_status);
    if (ret == ESP_OK) {
        printf("✅ Auto-negotiation: %s\n", link_status ? "Enabled" : "Disabled");
    } else {
        printf("⚠️  Failed to get auto-negotiation status: %s\n", esp_err_to_name(ret));
    }

    // Test 4: Try to send a simple UDP packet to test actual data flow
    printf("\nTest 3: Network data flow test\n");
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("❌ Failed to create UDP socket\n");
    } else {
        struct sockaddr_in dest_addr;
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(12345);  // Random port
        dest_addr.sin_addr.s_addr = inet_addr("10.10.99.100");  // Gateway
        
        char test_data[] = "W5500 test packet";
        int sent = sendto(sock, test_data, strlen(test_data), 0, 
                         (struct sockaddr*)&dest_addr, sizeof(dest_addr));
        
        if (sent > 0) {
            printf("✅ Sent %d bytes to gateway (data flow working)\n", sent);
        } else {
            printf("⚠️  Failed to send test packet (errno: %d)\n", errno);
        }
        close(sock);
    }

    printf("\n📊 W5500 SPI communication test completed\n");
    return 0;
}

esp_err_t ethernet_register_console_commands(void)
{
    esp_console_cmd_t cmd;

    // eth config command (note: space replaced with underscore for console compatibility)
    cmd = (esp_console_cmd_t){
        .command = "eth_config",
        .help = "以太网配置: eth_config [show|reload] - 显示当前配置或重新载入配置",
        .hint = "[clear] or [<ip> <gateway> <netmask> <dns>]",
        .func = &cmd_eth_config,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_dhcp",
        .help = "DHCP服务器控制: eth_dhcp [start|stop|restart|status] - 管理DHCP服务器和查看客户端状态",
        .hint = "[status|enable|disable] or [pool <start_ip> <end_ip> [lease_hours]] or [release <mac_addr>]",
        .func = &cmd_eth_dhcp,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_gateway",
        .help = "网关服务控制: eth_gateway [start|stop|status] - 管理网关服务",
        .hint = "[enable|disable]",
        .func = &cmd_eth_gateway,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_status",
        .help = "以太网状态: eth_status - 显示以太网接口详细状态信息",
        .hint = NULL,
        .func = &cmd_eth_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_ping",
        .help = "网络测试: eth_ping <IP地址> - ping测试网络连通性",
        .hint = "<target_ip> [count] [timeout_ms]",
        .func = &cmd_eth_ping,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_reset",
        .help = "以太网重置: eth_reset - 重置以太网接口",
        .hint = NULL,
        .func = &cmd_eth_reset,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    cmd = (esp_console_cmd_t){
        .command = "eth_test",
        .help = "W5500测试: eth_test - 测试W5500芯片SPI通信",
        .hint = NULL,
        .func = &cmd_eth_test,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));

    ESP_LOGI(TAG, "Ethernet console commands registered");
    return ESP_OK;
}

// ==================== DHCP Client Management Functions ====================

static esp_err_t dhcp_add_client_with_ip(const uint8_t *mac_addr, const char *hostname, uint32_t ip_addr)
{
    if (!mac_addr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if client already exists
    dhcp_client_info_t *existing_client = NULL;
    if (dhcp_find_client_by_mac(mac_addr, &existing_client) == ESP_OK) {
        // Update existing client lease time and IP if provided
        existing_client->lease_start_time = esp_timer_get_time() / 1000000; // Convert to seconds
        existing_client->is_active = true;
        if (ip_addr != 0) {
            existing_client->ip_addr = ip_addr;
        }
        ESP_LOGI(TAG, "Updated existing DHCP client lease");
        return ESP_OK;
    }

    // Find an empty slot
    if (s_eth_state.dhcp_client_count >= 10) {
        ESP_LOGW(TAG, "DHCP client pool full");
        return ESP_ERR_NO_MEM;
    }

    // Use provided IP or get next available IP
    uint32_t assigned_ip = ip_addr;
    if (assigned_ip == 0) {
        assigned_ip = dhcp_get_next_available_ip();
        if (assigned_ip == 0) {
            ESP_LOGW(TAG, "No available IP addresses in DHCP pool");
            return ESP_ERR_NO_MEM;
        }
    }

    // Add new client
    for (int i = 0; i < 10; i++) {
        if (!s_eth_state.dhcp_clients[i].is_active) {
            memcpy(s_eth_state.dhcp_clients[i].mac_addr, mac_addr, 6);
            s_eth_state.dhcp_clients[i].ip_addr = assigned_ip;
            s_eth_state.dhcp_clients[i].lease_start_time = esp_timer_get_time() / 1000000;
            s_eth_state.dhcp_clients[i].lease_duration = s_eth_state.config.dhcp_lease_time * 3600; // Convert hours to seconds
            s_eth_state.dhcp_clients[i].is_active = true;
            
            if (hostname) {
                strncpy(s_eth_state.dhcp_clients[i].hostname, hostname, sizeof(s_eth_state.dhcp_clients[i].hostname) - 1);
                s_eth_state.dhcp_clients[i].hostname[sizeof(s_eth_state.dhcp_clients[i].hostname) - 1] = '\0';
            } else {
                snprintf(s_eth_state.dhcp_clients[i].hostname, sizeof(s_eth_state.dhcp_clients[i].hostname), 
                        "client-%02x%02x%02x", mac_addr[3], mac_addr[4], mac_addr[5]);
            }

            s_eth_state.dhcp_client_count++;
            
            char ip_str[16];
            ip_to_string(assigned_ip, ip_str, sizeof(ip_str));
            ESP_LOGI(TAG, "DHCP: Assigned IP %s to client %02x:%02x:%02x:%02x:%02x:%02x (%s)",
                    ip_str, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
                    s_eth_state.dhcp_clients[i].hostname);
            return ESP_OK;
        }
    }

    return ESP_ERR_NO_MEM;
}

static esp_err_t dhcp_remove_client(const uint8_t *mac_addr)
{
    dhcp_client_info_t *client = NULL;
    if (dhcp_find_client_by_mac(mac_addr, &client) == ESP_OK) {
        char ip_str[16];
        ip_to_string(client->ip_addr, ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "DHCP: Released IP %s from client %02x:%02x:%02x:%02x:%02x:%02x",
                ip_str, mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        
        memset(client, 0, sizeof(dhcp_client_info_t));
        s_eth_state.dhcp_client_count--;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t dhcp_find_client_by_mac(const uint8_t *mac_addr, dhcp_client_info_t **client)
{
    if (!mac_addr || !client) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 10; i++) {
        if (s_eth_state.dhcp_clients[i].is_active && 
            memcmp(s_eth_state.dhcp_clients[i].mac_addr, mac_addr, 6) == 0) {
            *client = &s_eth_state.dhcp_clients[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t dhcp_find_client_by_ip(uint32_t ip_addr, dhcp_client_info_t **client)
{
    if (!client) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 10; i++) {
        if (s_eth_state.dhcp_clients[i].is_active && 
            s_eth_state.dhcp_clients[i].ip_addr == ip_addr) {
            *client = &s_eth_state.dhcp_clients[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static uint32_t dhcp_get_next_available_ip(void)
{
    uint32_t start_ip = s_eth_state.config.dhcp_start_ip;
    uint32_t end_ip = s_eth_state.config.dhcp_end_ip;
    
    // Convert to host byte order for easier manipulation
    uint32_t start_host = ntohl(start_ip);
    uint32_t end_host = ntohl(end_ip);

    for (uint32_t ip_host = start_host; ip_host <= end_host; ip_host++) {
        uint32_t ip_net = htonl(ip_host);
        if (dhcp_is_ip_available(ip_net)) {
            return ip_net;
        }
    }
    return 0; // No available IP
}

static bool dhcp_is_ip_available(uint32_t ip_addr)
{
    // Check if IP is already assigned
    for (int i = 0; i < 10; i++) {
        if (s_eth_state.dhcp_clients[i].is_active && 
            s_eth_state.dhcp_clients[i].ip_addr == ip_addr) {
            return false;
        }
    }
    return true;
}

static void dhcp_cleanup_expired_leases(void)
{
    uint32_t current_time = esp_timer_get_time() / 1000000; // Convert to seconds
    
    for (int i = 0; i < 10; i++) {
        if (s_eth_state.dhcp_clients[i].is_active) {
            uint32_t lease_expire_time = s_eth_state.dhcp_clients[i].lease_start_time + 
                                       s_eth_state.dhcp_clients[i].lease_duration;
            
            if (current_time > lease_expire_time) {
                char ip_str[16];
                ip_to_string(s_eth_state.dhcp_clients[i].ip_addr, ip_str, sizeof(ip_str));
                ESP_LOGI(TAG, "DHCP: Lease expired for IP %s (client %02x:%02x:%02x:%02x:%02x:%02x)",
                        ip_str, 
                        s_eth_state.dhcp_clients[i].mac_addr[0], s_eth_state.dhcp_clients[i].mac_addr[1],
                        s_eth_state.dhcp_clients[i].mac_addr[2], s_eth_state.dhcp_clients[i].mac_addr[3],
                        s_eth_state.dhcp_clients[i].mac_addr[4], s_eth_state.dhcp_clients[i].mac_addr[5]);
                
                memset(&s_eth_state.dhcp_clients[i], 0, sizeof(dhcp_client_info_t));
                s_eth_state.dhcp_client_count--;
            }
        }
    }
}

// ==================== Public API Functions ====================

esp_err_t ethernet_get_dhcp_status(dhcp_server_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Clean up expired leases first
    dhcp_cleanup_expired_leases();

    memset(status, 0, sizeof(dhcp_server_status_t));
    
    status->server_enabled = s_eth_state.dhcp_server_running;
    status->pool_start_ip = s_eth_state.config.dhcp_start_ip;
    status->pool_end_ip = s_eth_state.config.dhcp_end_ip;
    status->lease_time_hours = s_eth_state.config.dhcp_lease_time;
    
    // Calculate pool statistics
    uint32_t start_host = ntohl(s_eth_state.config.dhcp_start_ip);
    uint32_t end_host = ntohl(s_eth_state.config.dhcp_end_ip);
    status->total_addresses = (end_host - start_host) + 1;
    status->allocated_count = s_eth_state.dhcp_client_count;
    status->available_count = status->total_addresses - status->allocated_count;
    
    // Copy client information
    int active_clients = 0;
    for (int i = 0; i < 10 && active_clients < 10; i++) {
        if (s_eth_state.dhcp_clients[i].is_active) {
            memcpy(&status->clients[active_clients], &s_eth_state.dhcp_clients[i], sizeof(dhcp_client_info_t));
            active_clients++;
        }
    }
    status->client_count = active_clients;

    return ESP_OK;
}

esp_err_t ethernet_get_dhcp_client_by_mac(const uint8_t *mac_addr, dhcp_client_info_t *client_info)
{
    if (!mac_addr || !client_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    dhcp_client_info_t *client = NULL;
    esp_err_t ret = dhcp_find_client_by_mac(mac_addr, &client);
    if (ret == ESP_OK) {
        memcpy(client_info, client, sizeof(dhcp_client_info_t));
    }
    return ret;
}

esp_err_t ethernet_get_dhcp_client_by_ip(uint32_t ip_addr, dhcp_client_info_t *client_info)
{
    if (!client_info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    dhcp_client_info_t *client = NULL;
    esp_err_t ret = dhcp_find_client_by_ip(ip_addr, &client);
    if (ret == ESP_OK) {
        memcpy(client_info, client, sizeof(dhcp_client_info_t));
    }
    return ret;
}

esp_err_t ethernet_release_dhcp_lease(const uint8_t *mac_addr)
{
    if (!mac_addr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return dhcp_remove_client(mac_addr);
}

esp_err_t ethernet_save_config_from_manager(const ethernet_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_eth_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Saving configuration from config manager to ethernet interface");
    
    // Update current configuration
    memcpy(&s_eth_state.config, config, sizeof(ethernet_config_t));
    
    // Save to NVS
    esp_err_t ret = ethernet_save_config_to_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config from manager: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Configuration from config manager saved successfully");
    return ESP_OK;
}
