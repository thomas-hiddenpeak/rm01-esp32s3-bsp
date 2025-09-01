/**
 * @file task_manager.h
 * @brief 任务管理器 - 基于NVS配置的任务调度系统
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

// 常量定义
#define MAX_MANAGED_TASKS               8
#define TASK_NAME_MAX_LEN              32
#define TASK_DEPENDENCIES_MAX_LEN      128
#define TASK_MANAGER_NAMESPACE         "task_mgr"

// 任务类型枚举
typedef enum {
    TASK_TYPE_SERVICE = 0,
    TASK_TYPE_BACKGROUND,
    TASK_TYPE_INTERFACE,
    TASK_TYPE_MONITOR
} task_type_t;

// 任务优先级枚举
typedef enum {
    TASK_PRIORITY_LOW = 1,
    TASK_PRIORITY_NORMAL = 3,
    TASK_PRIORITY_HIGH = 5,
    TASK_PRIORITY_CRITICAL = 7
} task_priority_level_t;

// 任务状态枚举
typedef enum {
    TASK_STATUS_UNINITIALIZED = 0,
    TASK_STATUS_INITIALIZED,
    TASK_STATUS_STARTING,
    TASK_STATUS_RUNNING,
    TASK_STATUS_STOPPING,
    TASK_STATUS_STOPPED,
    TASK_STATUS_ERROR
} task_status_t;

// 前向声明
typedef esp_err_t (*task_init_func_t)(void);
typedef esp_err_t (*task_start_func_t)(void);
typedef esp_err_t (*task_stop_func_t)(void);
typedef esp_err_t (*task_deinit_func_t)(void);

// 任务配置结构
typedef struct {
    char name[TASK_NAME_MAX_LEN];
    task_type_t type;
    task_priority_level_t priority;
    bool auto_start;
    bool blocking;
    uint32_t stack_size;
    uint32_t freertos_priority;
    char dependencies[TASK_DEPENDENCIES_MAX_LEN];
    task_init_func_t init_func;
    task_start_func_t start_func;
    task_stop_func_t stop_func;
    task_deinit_func_t deinit_func;
} task_config_t;

// 任务信息结构
typedef struct {
    task_config_t config;
    task_status_t status;
    uint32_t start_time;
    esp_err_t last_error;
    char error_message[128];
} task_info_t;

// 任务管理器配置结构
typedef struct {
    bool auto_start_enabled;
    uint32_t startup_timeout_ms;
    uint32_t monitor_interval_ms;
    bool dependency_check_enabled;
    uint32_t task_count;
    tmgr_task_config_t tasks[MAX_MANAGED_TASKS];
} tmgr_task_manager_config_t;

// 任务管理器统计信息结构
typedef struct {
    uint32_t total_tasks;
    uint32_t running_tasks;
    uint32_t failed_tasks;
    uint32_t startup_time_ms;
} tmgr_task_manager_stats_t;

/**
 * @brief 任务统计信息结构
 */
typedef struct {
    uint32_t total_tasks;                           ///< 总任务数
    uint32_t running_tasks;                         ///< 运行中任务数
    uint32_t failed_tasks;                          ///< 失败任务数
    uint32_t enabled_tasks;                         ///< 启用任务数
    uint32_t auto_start_tasks;                      ///< 自动启动任务数
    uint32_t total_starts;                          ///< 总启动次数
    uint32_t total_errors;                          ///< 总错误次数
    uint32_t memory_usage;                          ///< 内存使用量(字节)
    uint32_t startup_time_ms;                       ///< 系统启动时间(毫秒)
} tm_task_manager_stats_t;

// =============================================================================
// 核心管理API
// =============================================================================

/**
 * @brief 初始化任务管理器
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_init(void);

/**
 * @brief 反初始化任务管理器
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_deinit(void);

/**
 * @brief 注册任务
 * 
 * @param config 任务配置
 * @param func 任务函数
 * @param arg 任务参数
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_register_task(const tm_task_config_t *config, tm_task_func_t func, void *arg);

/**
 * @brief 注销任务
 * 
 * @param task_name 任务名称
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_unregister_task(const char *task_name);

/**
 * @brief 启动指定任务
 * 
 * @param task_name 任务名称
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_start_task(const char *task_name);

/**
 * @brief 停止指定任务
 * 
 * @param task_name 任务名称
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_stop_task(const char *task_name);

/**
 * @brief 获取任务信息
 * 
 * @param task_name 任务名称
 * @param info 任务信息结构体指针
 * @return esp_err_t 操作结果
 */
esp_err_t task_manager_get_task_info(const char *task_name, tm_task_info_t *info);

/**
 * @brief 列出所有任务
 * 
 * @param tasks 输出任务信息数组
 * @param max_tasks 最大任务数量
 * @param actual_count 实际任务数量输出
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_list_tasks(tm_task_info_t *tasks, uint32_t max_tasks, uint32_t *actual_count);

/**
 * @brief 获取任务统计信息
 * 
 * @param stats 统计信息输出
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_get_stats(tm_task_manager_stats_t *stats);

/**
 * @brief 启动所有自动启动任务
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_start_all_tasks(void);

// =============================================================================
// 配置管理API
// =============================================================================

/**
 * @brief 加载任务管理器配置(从NVS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_load_config(void);

/**
 * @brief 保存任务管理器配置(到NVS)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_save_config(void);

/**
 * @brief 重置任务管理器配置为默认值
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_reset_config(void);

/**
 * @brief 设置任务管理器配置
 * 
 * @param config 新配置
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_set_config(const tm_task_manager_config_t *config);

/**
 * @brief 获取任务管理器配置
 * 
 * @param config 配置结构体指针
 * @return esp_err_t 操作结果
 */
esp_err_t task_manager_get_config(tm_task_manager_config_t *config);

/**
 * @brief 启用/禁用指定任务
 * 
 * @param task_name 任务名称
 * @param enabled 是否启用
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_set_task_enabled(const char *task_name, bool enabled);

/**
 * @brief 重启指定任务
 * 
 * @param task_name 任务名称
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_manager_restart_task(const char *task_name);

/**
 * @brief 转换任务类型为字符串
 * 
 * @param type 任务类型
 * @return const char* 类型字符串
 */
const char* task_type_to_string(tm_task_type_t type);

/**
 * @brief 转换任务优先级为字符串
 * 
 * @param priority 任务优先级
 * @return const char* 优先级字符串
 */
const char* task_priority_to_string(tm_task_priority_level_t priority);

/**
 * @brief 转换任务状态为字符串
 * 
 * @param status 任务状态
 * @return const char* 状态字符串
 */
const char* task_status_to_string(tm_task_status_t status);

// =============================================================================
// 预定义任务ID常量
// =============================================================================

#define TASK_ID_CONFIG_MANAGER      "config_manager"
#define TASK_ID_DEVICE_INTERFACE    "device_interface"
#define TASK_ID_ETHERNET_INTERFACE  "ethernet_interface"
#define TASK_ID_HARDWARE_CONTROL    "hardware_control"
#define TASK_ID_CONSOLE_INTERFACE   "console_interface"
#define TASK_ID_SDCARD_INTERFACE    "sdcard_interface"
#define TASK_ID_SYSTEM_MONITOR      "system_monitor"

// =============================================================================
// 兼容性类型别名 (为了保持现有代码兼容)
// =============================================================================

// 类型别名
typedef tm_task_type_t task_type_t;
typedef tm_task_priority_level_t task_priority_level_t;
typedef tm_task_status_t task_status_t;
typedef tm_task_func_t task_func_t;
typedef tm_task_config_t task_config_t;
typedef tm_task_info_t task_info_t;
typedef tm_task_manager_config_t task_manager_config_t;
typedef tm_task_manager_stats_t task_manager_stats_t;

// 枚举值别名
#define TASK_TYPE_BLOCKING          TM_TASK_TYPE_BLOCKING
#define TASK_TYPE_INDEPENDENT       TM_TASK_TYPE_INDEPENDENT
#define TASK_TYPE_BACKGROUND        TM_TASK_TYPE_BACKGROUND
#define TASK_TYPE_ONE_SHOT          TM_TASK_TYPE_ONE_SHOT
#define TASK_TYPE_SERVICE           TM_TASK_TYPE_SERVICE
#define TASK_TYPE_INTERFACE         TM_TASK_TYPE_INTERFACE
#define TASK_TYPE_MONITOR           TM_TASK_TYPE_MONITOR

#define TASK_PRIORITY_LOW           TM_TASK_PRIORITY_LOW
#define TASK_PRIORITY_NORMAL        TM_TASK_PRIORITY_NORMAL
#define TASK_PRIORITY_HIGH          TM_TASK_PRIORITY_HIGH
#define TASK_PRIORITY_CRITICAL      TM_TASK_PRIORITY_CRITICAL

#define TASK_STATUS_UNINITIALIZED   TM_TASK_STATUS_UNINITIALIZED
#define TASK_STATUS_INITIALIZED     TM_TASK_STATUS_INITIALIZED
#define TASK_STATUS_STARTING        TM_TASK_STATUS_STARTING
#define TASK_STATUS_RUNNING         TM_TASK_STATUS_RUNNING
#define TASK_STATUS_STOPPING        TM_TASK_STATUS_STOPPING
#define TASK_STATUS_STOPPED         TM_TASK_STATUS_STOPPED
#define TASK_STATUS_ERROR           TM_TASK_STATUS_ERROR
#define TASK_STATUS_DISABLED        TM_TASK_STATUS_DISABLED

#ifdef __cplusplus
}
#endif

#endif // TASK_MANAGER_H
