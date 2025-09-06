/*
 * ESP32S3 Console Control Program with Component-based Architecture
 * Features: Component-based Device Control, System Monitoring, Console Interface
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// 组件头文件
#include "device_interface.h"
#include "console_interface.h"
#include "ethernet_interface.h"
#include "config_manager.h"
#include "hardware_config.h"
#include "hardware_control.h"
#include "console_ping.h"
#include "sdcard_interface.h"
#include "web_server.h"
#include "color_correction.h"

static const char *TAG = "ESP32S3_MAIN";

// 函数声明
static void device_event_handler(device_event_t event, void *data);
static void console_event_handler(console_event_t event, const char *data);
static void ethernet_event_handler(ethernet_status_t status, void *data);

// 控制台命令函数声明 - 已移至控制台组件

void app_main(void)
{
    // 设置日志级别
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set("CONFIG_MANAGER", ESP_LOG_INFO);  // 启用配置管理器详细日志
    
    printf("\n=== ESP32S3 控制台程序启动 ===\n");

    // 1. 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS初始化完成");

    // 2. 初始化配置管理器
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置管理器初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "配置管理器初始化成功");
    }

    // 3. 尝试加载保存的配置
    ret = config_manager_load();
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "未找到保存的配置，使用默认配置");
        config_manager_reset_to_defaults();
        // 保存默认配置到NVS
        esp_err_t save_ret = config_manager_save();
        if (save_ret == ESP_OK) {
            ESP_LOGI(TAG, "默认配置已保存到NVS");
        } else {
            ESP_LOGW(TAG, "保存默认配置失败: %s", esp_err_to_name(save_ret));
        }
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "配置加载失败，使用默认配置: %s", esp_err_to_name(ret));
        config_manager_reset_to_defaults();
        // 保存默认配置到NVS
        esp_err_t save_ret = config_manager_save();
        if (save_ret == ESP_OK) {
            ESP_LOGI(TAG, "默认配置已保存到NVS");
        } else {
            ESP_LOGW(TAG, "保存默认配置失败: %s", esp_err_to_name(save_ret));
        }
    } else {
        ESP_LOGI(TAG, "配置加载成功");
        
        // 检查是否启用了启动时加载配置
        const complete_config_t *loaded_config = config_manager_get_config();
        if (loaded_config && !loaded_config->system.startup_load_config) {
            ESP_LOGW(TAG, "系统配置禁用了启动时加载配置，重置为默认配置");
            config_manager_reset_to_defaults();
        } else {
            ESP_LOGI(TAG, "✅ 启动时配置加载已启用，将使用保存的配置");
        }
    }

    // 4. 初始化设备接口（包含硬件控制和系统监控）
    device_interface_config_t device_config = DEVICE_INTERFACE_DEFAULT_CONFIG();
    ret = device_interface_init(&device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设备接口初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "设备接口初始化成功");
    }

    // 4.1 初始化色彩校正系统
    ret = color_correction_init_hardware();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "色彩校正系统初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "色彩校正系统初始化成功");
    }

    // 5. 注册设备事件回调
    device_interface_register_event_callback(device_event_handler);

    // 6. 应用加载的配置到所有子系统
    ret = config_manager_apply_config();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "配置应用失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "配置已应用到所有子系统");
    }

    // 7. 初始化控制台接口
    console_interface_config_t console_config = CONSOLE_INTERFACE_DEFAULT_CONFIG();
    ret = console_interface_init(&console_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "控制台接口初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "控制台接口初始化成功");
    }

    // 8. 注册控制台事件回调
    console_interface_register_event_callback(console_event_handler);

    // 9. 初始化以太网接口
    // 使用配置管理器中的以太网配置，而不是默认配置
    const ethernet_config_t *saved_eth_config = config_manager_get_ethernet_config();
    ethernet_config_t ethernet_config;
    if (saved_eth_config) {
        // 使用已保存的配置
        memcpy(&ethernet_config, saved_eth_config, sizeof(ethernet_config_t));
        ESP_LOGI(TAG, "使用配置管理器中的以太网配置");
    } else {
        // 回退到默认配置
        ethernet_config_t default_config = ETHERNET_DEFAULT_CONFIG();
        memcpy(&ethernet_config, &default_config, sizeof(ethernet_config_t));
        // 启用DHCP服务器和网关功能
        ethernet_config.dhcp_server_enabled = true;
        ethernet_config.gateway_enabled = true;
        ESP_LOGW(TAG, "配置管理器未初始化，使用默认以太网配置");
    }
    ret = ethernet_interface_init(&ethernet_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "以太网接口初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "以太网接口初始化成功");
    }

    // 10. 注册以太网事件回调
    ethernet_register_event_callback(ethernet_event_handler);

    // 11. 自动检测并挂载SD卡
    ESP_LOGI(TAG, "尝试自动挂载SD卡...");
    ret = sdcard_auto_mount(NULL);  // 使用默认挂载点 /sdcard
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SD卡自动挂载成功");
        
        // SD卡挂载成功后，检查并启动LED矩阵
        ESP_LOGI(TAG, "检查LED矩阵自动启动配置...");
        uint8_t matrix_brightness;
        char animation_name[64];
        bool auto_start, enable;
        
        ret = config_manager_get_matrix_config(&matrix_brightness, animation_name, &auto_start, &enable);
        if (ret == ESP_OK && enable && auto_start) {
            ESP_LOGI(TAG, "配置为自动启动LED矩阵: 亮度=%d%%, 动画='%s'", matrix_brightness, animation_name);
            
            // 初始化LED矩阵
            ret = led_matrix_init();
            if (ret == ESP_OK) {
                // 设置亮度
                ret = led_matrix_set_brightness(matrix_brightness);
                if (ret == ESP_OK) {
                    // 加载启动动画
                    ret = led_matrix_load_animation(animation_name);
                    if (ret == ESP_OK) {
                        ESP_LOGI(TAG, "LED矩阵自动启动成功");
                    } else if (ret == ESP_ERR_NOT_FOUND) {
                        ESP_LOGW(TAG, "启动动画 '%s' 未找到，显示测试图案", animation_name);
                        led_matrix_test_pattern();
                    } else {
                        ESP_LOGW(TAG, "加载启动动画失败: %s，显示测试图案", esp_err_to_name(ret));
                        led_matrix_test_pattern();
                    }
                } else {
                    ESP_LOGW(TAG, "设置LED矩阵亮度失败: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGW(TAG, "LED矩阵初始化失败: %s", esp_err_to_name(ret));
            }
        } else if (ret == ESP_OK && enable && !auto_start) {
            ESP_LOGI(TAG, "LED矩阵已启用但未配置为自动启动");
        } else if (ret == ESP_OK && !enable) {
            ESP_LOGI(TAG, "LED矩阵未启用");
        } else {
            ESP_LOGW(TAG, "获取LED矩阵配置失败: %s", esp_err_to_name(ret));
        }
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "未检测到SD卡，跳过挂载");
        ESP_LOGI(TAG, "未检测到SD卡，LED矩阵自动启动已跳过");
    } else {
        ESP_LOGW(TAG, "SD卡自动挂载失败: %s", esp_err_to_name(ret));
        ESP_LOGI(TAG, "SD卡挂载失败，LED矩阵自动启动已跳过");
    }

    // 12. 注册所有控制台命令
    console_interface_register_system_commands();
    console_interface_register_device_commands();
    console_interface_register_config_commands();
    console_interface_register_ethernet_commands();
    console_interface_register_sdcard_commands();
    console_interface_register_web_server_commands();
    
    // 注册官方ping命令 - 重新启用，现在网络硬件确认工作正常
    ret = console_cmd_ping_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ping命令注册失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ping命令注册成功");
    }

    // 13. 短暂延迟让系统稳定
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 14. 启动以太网接口
    ret = ethernet_interface_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "以太网接口启动失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "以太网接口启动成功");
    }

    // 15. 初始化Web服务器
    ESP_LOGI(TAG, "检查Web服务器自动启动配置...");
    
    // 从配置管理器检查是否自动启动
    const web_server_config_t *web_config = config_manager_get_web_server_config();
    if (web_config && web_config->auto_start) {
        ESP_LOGI(TAG, "配置为自动启动Web服务器");
        ret = web_server_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Web服务器自动启动成功");
            ESP_LOGI(TAG, "访问地址: http://10.10.99.97:%d/", web_config->port);
        } else {
            ESP_LOGE(TAG, "Web服务器自动启动失败: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "Web服务器未配置为自动启动，请使用命令 'web start' 手动启动");
    }

    // 16. 显示系统信息
    device_print_full_status();

    // 17. 启动控制台任务
    ret = console_interface_start(8192, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "控制台任务启动失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "控制台任务启动成功");
    }

    printf("系统初始化完成！\n");
    
    // 主任务现在可以自由运行，不会阻塞
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // 可以在这里添加其他主循环逻辑
    }
}

// 设备事件处理器
static void device_event_handler(device_event_t event, void *data)
{
    switch (event) {
        case DEVICE_EVENT_INIT_COMPLETE:
            printf("✅ 设备初始化完成\n");
            break;
        case DEVICE_EVENT_HARDWARE_ERROR:
            printf("❌ 硬件错误\n");
            break;
        case DEVICE_EVENT_MEMORY_WARNING:
            printf("⚠️ 内存警告: %" PRIu32 " bytes\n", *(uint32_t*)data);
            break;
        case DEVICE_EVENT_SYSTEM_RESTART:
            printf("🔄 系统即将重启\n");
            break;
        default:
            break;
    }
}

// 控制台事件处理器
static void console_event_handler(console_event_t event, const char *data)
{
    switch (event) {
        case CONSOLE_EVENT_READY:
            ESP_LOGI(TAG, "控制台准备就绪");
            break;
        case CONSOLE_EVENT_COMMAND_SUCCESS:
            ESP_LOGD(TAG, "命令执行成功: %s", data ? data : "unknown");
            break;
        case CONSOLE_EVENT_COMMAND_ERROR:
            ESP_LOGW(TAG, "命令执行错误: %s", data ? data : "unknown");
            break;
        case CONSOLE_EVENT_SHUTDOWN:
            ESP_LOGI(TAG, "控制台关闭");
            break;
        default:
            break;
    }
}

// 以太网事件处理器
static void ethernet_event_handler(ethernet_status_t status, void *data)
{
    switch (status) {
        case ETH_STATUS_DISCONNECTED:
            printf("🔌 以太网断开连接\n");
            break;
        case ETH_STATUS_CONNECTED:
            printf("🔌 以太网已连接\n");
            break;
        case ETH_STATUS_GOT_IP:
            printf("🌐 以太网已获取IP地址\n");
            break;
        case ETH_STATUS_DHCP_SERVER_STARTED:
            printf("📡 DHCP服务器已启动\n");
            break;
        case ETH_STATUS_GATEWAY_ENABLED:
            printf("🛡️ 网关服务已启用\n");
            break;
        default:
            break;
    }
}

/* 
 * 所有控制台命令实现已迁移到 console_interface 组件
 * 主程序现在专注于系统初始化和事件处理
 */
