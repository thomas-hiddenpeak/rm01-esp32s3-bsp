/**
 * @file device_interface.c
 * @brief ESP32S3 设备接口组件实现
 */

#include "device_interface.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "DEVICE_INTERFACE";
static const char *NVS_NAMESPACE = "device_config";

// ==================== 静态变量 ====================

static bool s_initialized = false;
static device_interface_config_t s_config = {0};
static device_event_cb_t s_event_callback = NULL;
static device_status_t s_last_status = {0};

// ==================== 静态函数声明 ====================

static void internal_memory_warning_callback(uint32_t free_heap, uint32_t threshold);
static esp_err_t save_hardware_config_to_nvs(void);
static esp_err_t load_hardware_config_from_nvs(void);
static void trigger_event(device_event_t event, void *data);

// ==================== 初始化接口实现 ====================

esp_err_t device_interface_init(const device_interface_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Device interface already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing device interface v%d.%d.%d", 
             DEVICE_INTERFACE_VERSION_MAJOR, 
             DEVICE_INTERFACE_VERSION_MINOR, 
             DEVICE_INTERFACE_VERSION_PATCH);

    // 使用默认配置或用户提供的配置
    if (config == NULL) {
        s_config = (device_interface_config_t)DEVICE_INTERFACE_DEFAULT_CONFIG();
    } else {
        s_config = *config;
    }

    // 设置内存警告回调为内部回调
    s_config.monitor_config.warning_cb = internal_memory_warning_callback;

    esp_err_t ret = ESP_OK;

    // 初始化硬件控制组件
    if (s_config.enable_hardware_control) {
        ret = hardware_control_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize hardware control: %s", esp_err_to_name(ret));
            s_config.enable_hardware_control = false;
        } else {
            ESP_LOGI(TAG, "Hardware control initialized");
        }
    }

    // 初始化系统监控组件
    if (s_config.enable_system_monitor) {
        ret = system_monitor_init(&s_config.monitor_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize system monitor: %s", esp_err_to_name(ret));
            s_config.enable_system_monitor = false;
        } else {
            ESP_LOGI(TAG, "System monitor initialized");
        }
    }

    s_initialized = true;
    
    // 触发初始化完成事件
    trigger_event(DEVICE_EVENT_INIT_COMPLETE, NULL);
    
    ESP_LOGI(TAG, "Device interface initialized successfully");
    ESP_LOGI(TAG, "Hardware control: %s, System monitor: %s", 
             s_config.enable_hardware_control ? "Enabled" : "Disabled",
             s_config.enable_system_monitor ? "Enabled" : "Disabled");
    
    return ESP_OK;
}

esp_err_t device_interface_deinit(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Device interface not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing device interface");

    // 反初始化系统监控组件
    if (s_config.enable_system_monitor) {
        system_monitor_deinit();
    }

    // 反初始化硬件控制组件
    if (s_config.enable_hardware_control) {
        hardware_control_deinit();
    }

    s_initialized = false;
    s_event_callback = NULL;
    
    ESP_LOGI(TAG, "Device interface deinitialized");
    return ESP_OK;
}

bool device_interface_is_initialized(void)
{
    return s_initialized;
}

esp_err_t device_interface_register_event_callback(device_event_cb_t callback)
{
    if (callback == NULL) {
        ESP_LOGE(TAG, "Event callback is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_event_callback = callback;
    ESP_LOGI(TAG, "Event callback registered");
    return ESP_OK;
}

// ==================== 统一控制接口实现 ====================

esp_err_t device_quick_setup(uint8_t fan_speed, led_color_t board_led_color, led_color_t touch_led_color)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Quick setup: Fan=%d%%, Board LED=(%d,%d,%d), Touch LED=(%d,%d,%d)",
             fan_speed, board_led_color.red, board_led_color.green, board_led_color.blue,
             touch_led_color.red, touch_led_color.green, touch_led_color.blue);

    esp_err_t ret = ESP_OK;

    if (s_config.enable_hardware_control) {
        // 设置风扇速度
        ret = fan_set_speed(fan_speed);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set fan speed");
            return ret;
        }

        // 设置板载LED
        ret = board_led_set_color(board_led_color);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set board LED color");
            return ret;
        }

        // 设置触摸LED
        ret = touch_led_set_color(touch_led_color);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set touch LED color");
            return ret;
        }
    }

    ESP_LOGI(TAG, "Quick setup completed successfully");
    return ESP_OK;
}

esp_err_t device_shutdown_all(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Shutting down all devices");

    if (s_config.enable_hardware_control) {
        fan_stop();
        board_led_turn_off();
        touch_led_turn_off();
    }

    ESP_LOGI(TAG, "All devices shut down");
    return ESP_OK;
}

esp_err_t device_reset_to_default(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting devices to default state");

    // 默认状态：风扇关闭，LED关闭
    return device_quick_setup(0, LED_COLOR_OFF, LED_COLOR_OFF);
}

esp_err_t device_enter_sleep_mode(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Entering sleep mode");

    // 保存当前状态
    device_get_full_status(&s_last_status);

    // 关闭所有设备
    device_shutdown_all();

    // 停止系统监控以节省电源
    if (s_config.enable_system_monitor && system_monitor_is_running()) {
        system_monitor_stop();
    }

    ESP_LOGI(TAG, "Sleep mode activated");
    return ESP_OK;
}

esp_err_t device_wake_up(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Waking up from sleep mode");

    // 重启系统监控
    if (s_config.enable_system_monitor && !system_monitor_is_running()) {
        system_monitor_start();
    }

    // 恢复之前的设备状态
    if (s_config.enable_hardware_control && s_last_status.hardware_available) {
        fan_set_speed(s_last_status.hardware.fan_speed);
        board_led_set_color(s_last_status.hardware.board_led_color);
        board_led_set_brightness(s_last_status.hardware.board_led_brightness);
        touch_led_set_color(s_last_status.hardware.touch_led_color);
        touch_led_set_brightness(s_last_status.hardware.touch_led_brightness);
    }

    ESP_LOGI(TAG, "Wake up completed");
    return ESP_OK;
}

// ==================== 状态查询接口实现 ====================

esp_err_t device_get_full_status(device_status_t *status)
{
    if (status == NULL) {
        ESP_LOGE(TAG, "Status pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 清零状态结构体
    memset(status, 0, sizeof(device_status_t));

    // 设置接口版本
    status->interface_version = device_get_interface_version();

    // 获取硬件状态
    status->hardware_available = s_config.enable_hardware_control && hardware_control_is_initialized();
    if (status->hardware_available) {
        esp_err_t ret = hardware_get_status(&status->hardware);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get hardware status");
            status->hardware_available = false;
        }
    }

    // 获取系统信息
    status->monitor_available = s_config.enable_system_monitor && system_monitor_is_initialized();
    if (status->monitor_available) {
        esp_err_t ret = system_get_info(&status->system);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to get system info");
            status->monitor_available = false;
        }
    }

    return ESP_OK;
}

esp_err_t device_print_full_status(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    device_status_t status;
    esp_err_t ret = device_get_full_status(&status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device status");
        return ret;
    }

    printf("\n=== 设备完整状态 ===\n");
    printf("接口版本: %d.%d.%d\n", 
           (int)((status.interface_version >> 16) & 0xFF),
           (int)((status.interface_version >> 8) & 0xFF),
           (int)(status.interface_version & 0xFF));

    if (status.hardware_available) {
        printf("\n硬件状态:\n");
        printf("  风扇速度: %d%%\n", status.hardware.fan_speed);
        printf("  板载LED: R:%d G:%d B:%d (亮度:%d%%)\n", 
               status.hardware.board_led_color.red,
               status.hardware.board_led_color.green,
               status.hardware.board_led_color.blue,
               status.hardware.board_led_brightness);
        printf("  触摸LED: R:%d G:%d B:%d (亮度:%d%%)\n", 
               status.hardware.touch_led_color.red,
               status.hardware.touch_led_color.green,
               status.hardware.touch_led_color.blue,
               status.hardware.touch_led_brightness);
    } else {
        printf("\n硬件状态: 不可用\n");
    }

    if (status.monitor_available) {
        printf("\n系统信息:\n");
        printf("  芯片型号: %s\n", status.system.chip_model);
        printf("  CPU核心数: %d\n", status.system.cores);
        printf("  CPU频率: %" PRIu32 " MHz\n", status.system.cpu_freq_mhz);
        printf("  Flash大小: %" PRIu32 " MB\n", status.system.flash_size_mb);
        printf("  可用堆内存: %" PRIu32 " bytes\n", status.system.free_heap);
        printf("  最小可用堆内存: %" PRIu32 " bytes\n", status.system.min_free_heap);
        printf("  系统运行时间: %" PRIu64 " ms\n", status.system.uptime_ms);
    } else {
        printf("\n系统信息: 不可用\n");
    }

    printf("====================\n");
    return ESP_OK;
}

uint32_t device_get_interface_version(void)
{
    return (DEVICE_INTERFACE_VERSION_MAJOR << 16) | 
           (DEVICE_INTERFACE_VERSION_MINOR << 8) | 
           DEVICE_INTERFACE_VERSION_PATCH;
}

esp_err_t device_get_version_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer parameters");
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(buffer, buffer_size, "%d.%d.%d", 
             DEVICE_INTERFACE_VERSION_MAJOR, 
             DEVICE_INTERFACE_VERSION_MINOR, 
             DEVICE_INTERFACE_VERSION_PATCH);
    
    return ESP_OK;
}

// ==================== 测试接口实现 ====================

esp_err_t device_run_full_test(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting full device test");

    esp_err_t ret = ESP_OK;

    if (s_config.enable_hardware_control) {
        ret = hardware_test_all();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Hardware test failed");
            return ret;
        }
    }

    // 测试系统监控功能
    if (s_config.enable_system_monitor) {
        system_info_t info;
        ret = system_get_info(&info);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "System monitor test failed");
            return ret;
        }
        ESP_LOGI(TAG, "System monitor test passed");
    }

    ESP_LOGI(TAG, "Full device test completed successfully");
    return ESP_OK;
}

esp_err_t device_run_quick_test(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting quick device test");

    esp_err_t ret = ESP_OK;

    if (s_config.enable_hardware_control) {
        // 快速测试：设置风扇和LED
        ret = device_quick_setup(50, LED_COLOR_RED, LED_COLOR_BLUE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Quick hardware test failed");
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        ret = device_reset_to_default();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Reset to default failed");
            return ret;
        }
    }

    ESP_LOGI(TAG, "Quick device test completed successfully");
    return ESP_OK;
}

esp_err_t device_run_stress_test(uint32_t duration_ms)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting stress test for %" PRIu32 " ms", duration_ms);

    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t cycle_count = 0;

    while ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) < duration_ms) {
        cycle_count++;
        
        if (s_config.enable_hardware_control) {
            // 循环测试不同的设置
            uint8_t fan_speed = (cycle_count * 25) % 101;
            led_color_t color = {
                .red = (cycle_count * 50) % 256,
                .green = (cycle_count * 75) % 256,
                .blue = (cycle_count * 100) % 256
            };
            
            esp_err_t ret = device_quick_setup(fan_speed, color, color);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Stress test failed at cycle %" PRIu32, cycle_count);
                return ret;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 恢复默认状态
    device_reset_to_default();

    ESP_LOGI(TAG, "Stress test completed: %" PRIu32 " cycles in %" PRIu32 " ms", 
             cycle_count, duration_ms);
    return ESP_OK;
}

// ==================== 配置管理接口实现 ====================

esp_err_t device_save_config(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Saving device configuration to NVS");
    return save_hardware_config_to_nvs();
}

esp_err_t device_load_config(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Device interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Loading device configuration from NVS");
    return load_hardware_config_from_nvs();
}

esp_err_t device_clear_config(void)
{
    ESP_LOGI(TAG, "Clearing device configuration from NVS");

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device configuration cleared from NVS");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

// ==================== 静态函数实现 ====================

static void internal_memory_warning_callback(uint32_t free_heap, uint32_t threshold)
{
    ESP_LOGW(TAG, "Memory warning: %" PRIu32 " bytes free < %" PRIu32 " bytes threshold", 
             free_heap, threshold);
    
    // 触发内存警告事件
    trigger_event(DEVICE_EVENT_MEMORY_WARNING, &free_heap);
}

static esp_err_t save_hardware_config_to_nvs(void)
{
    if (!s_config.enable_hardware_control) {
        ESP_LOGW(TAG, "Hardware control disabled, skipping config save");
        return ESP_OK;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // 获取当前硬件状态
    hardware_status_t status;
    ret = hardware_get_status(&status);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }

    // 保存配置
    ret = nvs_set_u8(nvs_handle, "fan_speed", status.fan_speed);
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "board_led_r", status.board_led_color.red);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "board_led_g", status.board_led_color.green);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "board_led_b", status.board_led_color.blue);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "board_bright", status.board_led_brightness);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "touch_led_r", status.touch_led_color.red);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "touch_led_g", status.touch_led_color.green);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "touch_led_b", status.touch_led_color.blue);
    }
    if (ret == ESP_OK) {
        ret = nvs_set_u8(nvs_handle, "touch_bright", status.touch_led_brightness);
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Hardware configuration saved to NVS");
        }
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

static esp_err_t load_hardware_config_from_nvs(void)
{
    if (!s_config.enable_hardware_control) {
        ESP_LOGW(TAG, "Hardware control disabled, skipping config load");
        return ESP_OK;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t fan_speed = 0;
    led_color_t board_color = {0};
    led_color_t touch_color = {0};
    uint8_t board_brightness = 50;
    uint8_t touch_brightness = 50;

    // 加载配置
    nvs_get_u8(nvs_handle, "fan_speed", &fan_speed);
    nvs_get_u8(nvs_handle, "board_led_r", &board_color.red);
    nvs_get_u8(nvs_handle, "board_led_g", &board_color.green);
    nvs_get_u8(nvs_handle, "board_led_b", &board_color.blue);
    nvs_get_u8(nvs_handle, "board_bright", &board_brightness);
    nvs_get_u8(nvs_handle, "touch_led_r", &touch_color.red);
    nvs_get_u8(nvs_handle, "touch_led_g", &touch_color.green);
    nvs_get_u8(nvs_handle, "touch_led_b", &touch_color.blue);
    nvs_get_u8(nvs_handle, "touch_bright", &touch_brightness);

    nvs_close(nvs_handle);

    // 应用配置
    fan_set_speed(fan_speed);
    board_led_set_brightness(board_brightness);
    board_led_set_color(board_color);
    touch_led_set_brightness(touch_brightness);
    touch_led_set_color(touch_color);

    ESP_LOGI(TAG, "Hardware configuration loaded from NVS");
    return ESP_OK;
}

static void trigger_event(device_event_t event, void *data)
{
    if (s_event_callback != NULL) {
        s_event_callback(event, data);
    }
}
