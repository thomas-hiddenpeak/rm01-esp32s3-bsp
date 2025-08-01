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
#include "hardware_config.h"

static const char *TAG = "ESP32S3_MAIN";

// 函数声明
static void device_event_handler(device_event_t event, void *data);
static void console_event_handler(console_event_t event, const char *data);

// 控制台命令函数声明 - 已移至控制台组件

void app_main(void)
{
    // 设置日志级别
    esp_log_level_set("*", ESP_LOG_WARN);
    
    printf("\n=== ESP32S3 组件化控制台程序启动 ===\n");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化设备接口（包含硬件控制和系统监控）
    device_interface_config_t device_config = DEVICE_INTERFACE_DEFAULT_CONFIG();
    ret = device_interface_init(&device_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设备接口初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "设备接口初始化成功");
    }

    // 注册设备事件回调
    device_interface_register_event_callback(device_event_handler);

    // 初始化控制台接口
    console_interface_config_t console_config = CONSOLE_INTERFACE_DEFAULT_CONFIG();
    ret = console_interface_init(&console_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "控制台接口初始化失败: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "控制台接口初始化成功");
    }

    // 注册控制台事件回调
    console_interface_register_event_callback(console_event_handler);

    // 注册所有控制台命令
    console_interface_register_system_commands();
    console_interface_register_device_commands();
    console_interface_register_config_commands();

    // 短暂延迟让系统稳定
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 显示系统信息
    device_print_full_status();

    // 启动控制台任务
    ret = console_interface_start(4096, 5);
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

/* 
 * 所有控制台命令实现已迁移到 console_interface 组件
 * 主程序现在专注于系统初始化和事件处理
 */
