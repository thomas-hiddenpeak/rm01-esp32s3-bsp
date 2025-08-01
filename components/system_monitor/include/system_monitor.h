/**
 * @file system_monitor.h
 * @brief ESP32S3 系统监控组件接口
 * 
 * 提供系统状态监控、内存监控、性能监控等功能
 */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 类型定义 ====================

/**
 * @brief 系统信息结构体
 */
typedef struct {
    char chip_model[32];        /*!< 芯片型号 */
    uint8_t cores;              /*!< CPU核心数 */
    uint32_t cpu_freq_mhz;      /*!< CPU频率 (MHz) */
    uint32_t flash_size_mb;     /*!< Flash大小 (MB) */
    uint32_t free_heap;         /*!< 可用堆内存 (bytes) */
    uint32_t min_free_heap;     /*!< 最小可用堆内存 (bytes) */
    uint64_t uptime_ms;         /*!< 系统运行时间 (ms) */
} system_info_t;

/**
 * @brief 内存监控回调函数类型
 * 
 * @param free_heap 当前可用堆内存
 * @param threshold 内存阈值
 */
typedef void (*memory_warning_cb_t)(uint32_t free_heap, uint32_t threshold);

/**
 * @brief 系统监控配置
 */
typedef struct {
    uint32_t monitor_interval_ms;      /*!< 监控间隔 (ms) */
    uint32_t memory_warning_threshold; /*!< 内存警告阈值 (bytes) */
    bool enable_auto_monitoring;       /*!< 是否启用自动监控 */
    memory_warning_cb_t warning_cb;    /*!< 内存警告回调函数 */
} system_monitor_config_t;

// ==================== 默认配置 ====================

#define SYSTEM_MONITOR_DEFAULT_INTERVAL_MS      30000   /*!< 默认监控间隔 30秒 */
#define SYSTEM_MONITOR_DEFAULT_MEMORY_THRESHOLD 10240   /*!< 默认内存警告阈值 10KB */

// ==================== 初始化接口 ====================

/**
 * @brief 初始化系统监控组件
 * 
 * @param config 监控配置，传入NULL使用默认配置
 * @return
 *     - ESP_OK: 初始化成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t system_monitor_init(const system_monitor_config_t *config);

/**
 * @brief 反初始化系统监控组件
 * 
 * @return
 *     - ESP_OK: 反初始化成功
 *     - ESP_FAIL: 反初始化失败
 */
esp_err_t system_monitor_deinit(void);

/**
 * @brief 获取监控初始化状态
 * 
 * @return true: 已初始化, false: 未初始化
 */
bool system_monitor_is_initialized(void);

// ==================== 系统信息接口 ====================

/**
 * @brief 获取系统信息
 * 
 * @param info 存储系统信息的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t system_get_info(system_info_t *info);

/**
 * @brief 打印系统信息
 * 
 * @return
 *     - ESP_OK: 打印成功
 */
esp_err_t system_print_info(void);

/**
 * @brief 获取芯片信息字符串
 * 
 * @param buffer 存储字符串的缓冲区
 * @param buffer_size 缓冲区大小
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t system_get_chip_info_string(char *buffer, size_t buffer_size);

// ==================== 内存监控接口 ====================

/**
 * @brief 获取当前可用堆内存
 * 
 * @return 可用堆内存大小 (bytes)
 */
uint32_t system_get_free_heap(void);

/**
 * @brief 获取最小可用堆内存
 * 
 * @return 最小可用堆内存大小 (bytes)
 */
uint32_t system_get_min_free_heap(void);

/**
 * @brief 获取堆内存使用率
 * 
 * @return 内存使用率 (0-100%)
 */
uint8_t system_get_heap_usage_percent(void);

/**
 * @brief 检查内存是否低于阈值
 * 
 * @param threshold 内存阈值 (bytes)
 * @return true: 内存不足, false: 内存充足
 */
bool system_is_memory_low(uint32_t threshold);

/**
 * @brief 打印内存状态
 * 
 * @return
 *     - ESP_OK: 打印成功
 */
esp_err_t system_print_memory_status(void);

// ==================== 性能监控接口 ====================

/**
 * @brief 获取系统运行时间
 * 
 * @return 系统运行时间 (ms)
 */
uint64_t system_get_uptime_ms(void);

/**
 * @brief 获取系统运行时间（秒）
 * 
 * @return 系统运行时间 (s)
 */
uint32_t system_get_uptime_seconds(void);

/**
 * @brief 获取CPU频率
 * 
 * @return CPU频率 (Hz)
 */
uint32_t system_get_cpu_freq_hz(void);

/**
 * @brief 获取CPU频率（MHz）
 * 
 * @return CPU频率 (MHz)
 */
uint32_t system_get_cpu_freq_mhz(void);

// ==================== 自动监控接口 ====================

/**
 * @brief 启动自动监控任务
 * 
 * @return
 *     - ESP_OK: 启动成功
 *     - ESP_ERR_INVALID_STATE: 监控未初始化或已启动
 */
esp_err_t system_monitor_start(void);

/**
 * @brief 停止自动监控任务
 * 
 * @return
 *     - ESP_OK: 停止成功
 *     - ESP_ERR_INVALID_STATE: 监控未启动
 */
esp_err_t system_monitor_stop(void);

/**
 * @brief 获取监控任务运行状态
 * 
 * @return true: 正在运行, false: 未运行
 */
bool system_monitor_is_running(void);

/**
 * @brief 设置内存警告阈值
 * 
 * @param threshold 新的内存阈值 (bytes)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t system_monitor_set_memory_threshold(uint32_t threshold);

/**
 * @brief 设置监控间隔
 * 
 * @param interval_ms 新的监控间隔 (ms)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t system_monitor_set_interval(uint32_t interval_ms);

// ==================== 重启控制接口 ====================

/**
 * @brief 重启系统
 * 
 * @param delay_ms 延迟时间 (ms)，0表示立即重启
 * @return 该函数不会返回
 */
void system_restart(uint32_t delay_ms);

/**
 * @brief 安全重启系统（清理资源后重启）
 * 
 * @param delay_ms 延迟时间 (ms)
 * @return 该函数不会返回
 */
void system_safe_restart(uint32_t delay_ms);

// ==================== 统计接口 ====================

/**
 * @brief 获取监控统计信息
 * 
 * @param monitor_count 监控次数指针
 * @param warning_count 警告次数指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t system_monitor_get_stats(uint32_t *monitor_count, uint32_t *warning_count);

/**
 * @brief 重置监控统计信息
 * 
 * @return
 *     - ESP_OK: 重置成功
 */
esp_err_t system_monitor_reset_stats(void);

/**
 * @brief 打印监控统计信息
 * 
 * @return
 *     - ESP_OK: 打印成功
 */
esp_err_t system_monitor_print_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MONITOR_H */
