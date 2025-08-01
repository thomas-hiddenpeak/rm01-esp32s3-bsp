/**
 * @file system_monitor.c
 * @brief ESP32S3 系统监控组件实现
 */

#include "system_monitor.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_clk_tree.h"

static const char *TAG = "SYSTEM_MONITOR";

// ==================== 静态变量 ====================

static bool s_initialized = false;
static bool s_monitoring_running = false;
static TaskHandle_t s_monitor_task_handle = NULL;
static system_monitor_config_t s_config = {0};
static uint32_t s_monitor_count = 0;
static uint32_t s_warning_count = 0;

// ==================== 静态函数声明 ====================

static void monitor_task(void *pvParameters);
static void default_memory_warning_callback(uint32_t free_heap, uint32_t threshold);
static esp_err_t get_flash_size(uint32_t *size_mb);

// ==================== 初始化接口实现 ====================

esp_err_t system_monitor_init(const system_monitor_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "System monitor already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing system monitor component");

    // 使用默认配置或用户提供的配置
    if (config == NULL) {
        s_config.monitor_interval_ms = SYSTEM_MONITOR_DEFAULT_INTERVAL_MS;
        s_config.memory_warning_threshold = SYSTEM_MONITOR_DEFAULT_MEMORY_THRESHOLD;
        s_config.enable_auto_monitoring = true;
        s_config.warning_cb = default_memory_warning_callback;
    } else {
        s_config = *config;
        if (s_config.warning_cb == NULL) {
            s_config.warning_cb = default_memory_warning_callback;
        }
    }

    // 重置统计信息
    s_monitor_count = 0;
    s_warning_count = 0;

    s_initialized = true;
    
    ESP_LOGI(TAG, "System monitor initialized - Interval: %" PRIu32 "ms, Threshold: %" PRIu32 " bytes", 
             s_config.monitor_interval_ms, s_config.memory_warning_threshold);
    
    // 如果启用自动监控，立即启动
    if (s_config.enable_auto_monitoring) {
        return system_monitor_start();
    }
    
    return ESP_OK;
}

esp_err_t system_monitor_deinit(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "System monitor not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing system monitor component");

    // 停止监控任务
    if (s_monitoring_running) {
        system_monitor_stop();
    }

    s_initialized = false;
    
    ESP_LOGI(TAG, "System monitor deinitialized");
    return ESP_OK;
}

bool system_monitor_is_initialized(void)
{
    return s_initialized;
}

// ==================== 系统信息接口实现 ====================

esp_err_t system_get_info(system_info_t *info)
{
    if (info == NULL) {
        ESP_LOGE(TAG, "Info pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    // 获取芯片型号
    strncpy(info->chip_model, CONFIG_IDF_TARGET, sizeof(info->chip_model) - 1);
    info->chip_model[sizeof(info->chip_model) - 1] = '\0';
    
    info->cores = chip_info.cores;
    
    // 获取CPU频率
    uint32_t cpu_freq;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &cpu_freq);
    info->cpu_freq_mhz = cpu_freq / 1000000;
    
    // 获取Flash大小
    uint32_t flash_size;
    if (get_flash_size(&flash_size) == ESP_OK) {
        info->flash_size_mb = flash_size;
    } else {
        info->flash_size_mb = 0;
    }
    
    info->free_heap = esp_get_free_heap_size();
    info->min_free_heap = esp_get_minimum_free_heap_size();
    info->uptime_ms = esp_timer_get_time() / 1000;
    
    return ESP_OK;
}

esp_err_t system_print_info(void)
{
    system_info_t info;
    esp_err_t ret = system_get_info(&info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get system info: %s", esp_err_to_name(ret));
        return ret;
    }

    printf("\n=== 系统信息 ===\n");
    printf("芯片型号: %s\n", info.chip_model);
    printf("CPU核心数: %d\n", info.cores);
    printf("CPU频率: %" PRIu32 " MHz\n", info.cpu_freq_mhz);
    printf("Flash大小: %" PRIu32 " MB\n", info.flash_size_mb);
    printf("可用堆内存: %" PRIu32 " bytes\n", info.free_heap);
    printf("最小可用堆内存: %" PRIu32 " bytes\n", info.min_free_heap);
    printf("系统运行时间: %" PRIu64 " ms\n", info.uptime_ms);
    printf("================\n");
    
    return ESP_OK;
}

esp_err_t system_get_chip_info_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        ESP_LOGE(TAG, "Invalid buffer parameters");
        return ESP_ERR_INVALID_ARG;
    }

    system_info_t info;
    esp_err_t ret = system_get_info(&info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buffer, buffer_size, "%s %dCores %" PRIu32 " MHz %" PRIu32 "MB", 
             info.chip_model, info.cores, info.cpu_freq_mhz, info.flash_size_mb);
    
    return ESP_OK;
}

// ==================== 内存监控接口实现 ====================

uint32_t system_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t system_get_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

uint8_t system_get_heap_usage_percent(void)
{
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    
    if (min_free_heap == 0) {
        return 0;
    }
    
    // 估算总堆大小（这是一个近似值）
    uint32_t estimated_total_heap = free_heap + (min_free_heap * 2);
    uint32_t used_heap = estimated_total_heap - free_heap;
    
    return (used_heap * 100) / estimated_total_heap;
}

bool system_is_memory_low(uint32_t threshold)
{
    return esp_get_free_heap_size() < threshold;
}

esp_err_t system_print_memory_status(void)
{
    uint32_t free_heap = system_get_free_heap();
    uint32_t min_free_heap = system_get_min_free_heap();
    uint8_t usage_percent = system_get_heap_usage_percent();
    
    printf("\n=== 内存状态 ===\n");
    printf("可用堆内存: %" PRIu32 " bytes\n", free_heap);
    printf("最小可用堆内存: %" PRIu32 " bytes\n", min_free_heap);
    printf("内存使用率: %d%%\n", usage_percent);
    printf("内存状态: %s\n", system_is_memory_low(s_config.memory_warning_threshold) ? "不足" : "正常");
    printf("================\n");
    
    return ESP_OK;
}

// ==================== 性能监控接口实现 ====================

uint64_t system_get_uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

uint32_t system_get_uptime_seconds(void)
{
    return (uint32_t)(system_get_uptime_ms() / 1000);
}

uint32_t system_get_cpu_freq_hz(void)
{
    uint32_t cpu_freq;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &cpu_freq);
    return cpu_freq;
}

uint32_t system_get_cpu_freq_mhz(void)
{
    return system_get_cpu_freq_hz() / 1000000;
}

// ==================== 自动监控接口实现 ====================

esp_err_t system_monitor_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "System monitor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_monitoring_running) {
        ESP_LOGW(TAG, "System monitor already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting system monitor task");
    
    BaseType_t ret = xTaskCreate(monitor_task, "sys_monitor", 2048, NULL, 3, &s_monitor_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_FAIL;
    }

    s_monitoring_running = true;
    ESP_LOGI(TAG, "System monitor task started");
    return ESP_OK;
}

esp_err_t system_monitor_stop(void)
{
    if (!s_monitoring_running) {
        ESP_LOGW(TAG, "System monitor not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping system monitor task");
    
    s_monitoring_running = false;
    
    if (s_monitor_task_handle != NULL) {
        vTaskDelete(s_monitor_task_handle);
        s_monitor_task_handle = NULL;
    }

    ESP_LOGI(TAG, "System monitor task stopped");
    return ESP_OK;
}

bool system_monitor_is_running(void)
{
    return s_monitoring_running;
}

esp_err_t system_monitor_set_memory_threshold(uint32_t threshold)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "System monitor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (threshold == 0) {
        ESP_LOGE(TAG, "Invalid memory threshold: 0");
        return ESP_ERR_INVALID_ARG;
    }

    s_config.memory_warning_threshold = threshold;
    ESP_LOGI(TAG, "Memory warning threshold set to %" PRIu32 " bytes", threshold);
    return ESP_OK;
}

esp_err_t system_monitor_set_interval(uint32_t interval_ms)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "System monitor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (interval_ms < 1000) {
        ESP_LOGE(TAG, "Invalid monitor interval: %" PRIu32 "ms (minimum 1000ms)", interval_ms);
        return ESP_ERR_INVALID_ARG;
    }

    s_config.monitor_interval_ms = interval_ms;
    ESP_LOGI(TAG, "Monitor interval set to %" PRIu32 " ms", interval_ms);
    return ESP_OK;
}

// ==================== 重启控制接口实现 ====================

void system_restart(uint32_t delay_ms)
{
    if (delay_ms > 0) {
        ESP_LOGI(TAG, "System will restart in %" PRIu32 " ms", delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    ESP_LOGI(TAG, "Restarting system...");
    esp_restart();
}

void system_safe_restart(uint32_t delay_ms)
{
    ESP_LOGI(TAG, "Preparing for safe restart...");
    
    // 停止监控任务
    if (s_monitoring_running) {
        system_monitor_stop();
    }
    
    // 等待其他任务完成清理工作
    if (delay_ms > 0) {
        ESP_LOGI(TAG, "Waiting %" PRIu32 " ms for cleanup...", delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    
    system_restart(0);
}

// ==================== 统计接口实现 ====================

esp_err_t system_monitor_get_stats(uint32_t *monitor_count, uint32_t *warning_count)
{
    if (monitor_count == NULL || warning_count == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    *monitor_count = s_monitor_count;
    *warning_count = s_warning_count;
    return ESP_OK;
}

esp_err_t system_monitor_reset_stats(void)
{
    s_monitor_count = 0;
    s_warning_count = 0;
    ESP_LOGI(TAG, "Monitor statistics reset");
    return ESP_OK;
}

esp_err_t system_monitor_print_stats(void)
{
    printf("\n=== 监控统计 ===\n");
    printf("监控次数: %" PRIu32 "\n", s_monitor_count);
    printf("警告次数: %" PRIu32 "\n", s_warning_count);
    printf("监控状态: %s\n", s_monitoring_running ? "运行中" : "已停止");
    printf("监控间隔: %" PRIu32 " ms\n", s_config.monitor_interval_ms);
    printf("内存阈值: %" PRIu32 " bytes\n", s_config.memory_warning_threshold);
    printf("================\n");
    return ESP_OK;
}

// ==================== 静态函数实现 ====================

static void monitor_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "Monitor task started");
    
    while (s_monitoring_running) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(s_config.monitor_interval_ms));
        
        if (!s_monitoring_running) {
            break;
        }
        
        s_monitor_count++;
        
        uint32_t free_heap = system_get_free_heap();
        
        // 检查内存是否低于阈值
        if (system_is_memory_low(s_config.memory_warning_threshold)) {
            s_warning_count++;
            if (s_config.warning_cb != NULL) {
                s_config.warning_cb(free_heap, s_config.memory_warning_threshold);
            }
        }
        
        #ifdef CONFIG_LOG_DEFAULT_LEVEL_DEBUG
        ESP_LOGD(TAG, "Monitor cycle %" PRIu32 " - Free heap: %" PRIu32 " bytes, Uptime: %" PRIu64 " ms", 
                 s_monitor_count, free_heap, system_get_uptime_ms());
        #endif
    }
    
    ESP_LOGI(TAG, "Monitor task ended");
    s_monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

static void default_memory_warning_callback(uint32_t free_heap, uint32_t threshold)
{
    ESP_LOGW(TAG, "Memory warning: Free heap %" PRIu32 " bytes < threshold %" PRIu32 " bytes", 
             free_heap, threshold);
    printf("⚠️  内存警告: 可用堆内存 %" PRIu32 " bytes 低于阈值 %" PRIu32 " bytes\n", 
           free_heap, threshold);
}

static esp_err_t get_flash_size(uint32_t *size_mb)
{
    if (size_mb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t flash_size;
    esp_err_t ret = esp_flash_get_size(NULL, &flash_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get flash size: %s", esp_err_to_name(ret));
        return ret;
    }
    
    *size_mb = flash_size / (1024 * 1024);
    return ESP_OK;
}
