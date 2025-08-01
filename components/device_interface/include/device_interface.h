/**
 * @file device_interface.h
 * @brief ESP32S3 设备接口组件 - 统一的设备控制接口
 * 
 * 提供统一的设备控制接口，集成硬件控制和系统监控功能
 */

#ifndef DEVICE_INTERFACE_H
#define DEVICE_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hardware_control.h"
#include "system_monitor.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 版本信息 ====================

#define DEVICE_INTERFACE_VERSION_MAJOR 1
#define DEVICE_INTERFACE_VERSION_MINOR 0
#define DEVICE_INTERFACE_VERSION_PATCH 0

// ==================== 类型定义 ====================

/**
 * @brief 设备接口配置
 */
typedef struct {
    bool enable_hardware_control;       /*!< 是否启用硬件控制 */
    bool enable_system_monitor;         /*!< 是否启用系统监控 */
    system_monitor_config_t monitor_config; /*!< 系统监控配置 */
} device_interface_config_t;

/**
 * @brief 设备完整状态
 */
typedef struct {
    hardware_status_t hardware;     /*!< 硬件状态 */
    system_info_t system;           /*!< 系统信息 */
    bool hardware_available;        /*!< 硬件控制是否可用 */
    bool monitor_available;         /*!< 系统监控是否可用 */
    uint32_t interface_version;     /*!< 接口版本 */
} device_status_t;

/**
 * @brief 设备事件类型
 */
typedef enum {
    DEVICE_EVENT_INIT_COMPLETE = 0,    /*!< 初始化完成 */
    DEVICE_EVENT_HARDWARE_ERROR,       /*!< 硬件错误 */
    DEVICE_EVENT_MEMORY_WARNING,       /*!< 内存警告 */
    DEVICE_EVENT_SYSTEM_RESTART,       /*!< 系统重启 */
    DEVICE_EVENT_MAX
} device_event_t;

/**
 * @brief 设备事件回调函数类型
 * 
 * @param event 事件类型
 * @param data 事件数据指针
 */
typedef void (*device_event_cb_t)(device_event_t event, void *data);

// ==================== 默认配置 ====================

#define DEVICE_INTERFACE_DEFAULT_CONFIG() { \
    .enable_hardware_control = true, \
    .enable_system_monitor = true, \
    .monitor_config = { \
        .monitor_interval_ms = SYSTEM_MONITOR_DEFAULT_INTERVAL_MS, \
        .memory_warning_threshold = SYSTEM_MONITOR_DEFAULT_MEMORY_THRESHOLD, \
        .enable_auto_monitoring = true, \
        .warning_cb = NULL \
    } \
}

// ==================== 初始化接口 ====================

/**
 * @brief 初始化设备接口
 * 
 * @param config 设备接口配置，传入NULL使用默认配置
 * @return
 *     - ESP_OK: 初始化成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t device_interface_init(const device_interface_config_t *config);

/**
 * @brief 反初始化设备接口
 * 
 * @return
 *     - ESP_OK: 反初始化成功
 *     - ESP_FAIL: 反初始化失败
 */
esp_err_t device_interface_deinit(void);

/**
 * @brief 获取设备接口初始化状态
 * 
 * @return true: 已初始化, false: 未初始化
 */
bool device_interface_is_initialized(void);

/**
 * @brief 注册设备事件回调函数
 * 
 * @param callback 回调函数指针
 * @return
 *     - ESP_OK: 注册成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t device_interface_register_event_callback(device_event_cb_t callback);

// ==================== 统一控制接口 ====================

/**
 * @brief 快速设置设备（一键配置）
 * 
 * @param fan_speed 风扇速度 (0-100%)
 * @param board_led_color 板载LED颜色
 * @param touch_led_color 触摸LED颜色
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_FAIL: 设置失败
 */
esp_err_t device_quick_setup(uint8_t fan_speed, led_color_t board_led_color, led_color_t touch_led_color);

/**
 * @brief 关闭所有设备
 * 
 * @return
 *     - ESP_OK: 关闭成功
 *     - ESP_FAIL: 关闭失败
 */
esp_err_t device_shutdown_all(void);

/**
 * @brief 重置所有设备到默认状态
 * 
 * @return
 *     - ESP_OK: 重置成功
 *     - ESP_FAIL: 重置失败
 */
esp_err_t device_reset_to_default(void);

/**
 * @brief 设备睡眠模式（关闭非必要设备）
 * 
 * @return
 *     - ESP_OK: 进入睡眠模式成功
 *     - ESP_FAIL: 进入睡眠模式失败
 */
esp_err_t device_enter_sleep_mode(void);

/**
 * @brief 设备唤醒模式（恢复设备状态）
 * 
 * @return
 *     - ESP_OK: 唤醒成功
 *     - ESP_FAIL: 唤醒失败
 */
esp_err_t device_wake_up(void);

// ==================== 状态查询接口 ====================

/**
 * @brief 获取完整设备状态
 * 
 * @param status 存储设备状态的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 设备接口未初始化
 */
esp_err_t device_get_full_status(device_status_t *status);

/**
 * @brief 打印完整设备状态
 * 
 * @return
 *     - ESP_OK: 打印成功
 *     - ESP_ERR_INVALID_STATE: 设备接口未初始化
 */
esp_err_t device_print_full_status(void);

/**
 * @brief 获取设备接口版本
 * 
 * @return 版本号 (格式: 0xMMmmpp - MM主版本, mm次版本, pp补丁版本)
 */
uint32_t device_get_interface_version(void);

/**
 * @brief 获取设备接口版本字符串
 * 
 * @param buffer 存储版本字符串的缓冲区
 * @param buffer_size 缓冲区大小
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t device_get_version_string(char *buffer, size_t buffer_size);

// ==================== 测试接口 ====================

/**
 * @brief 运行完整设备测试
 * 
 * @return
 *     - ESP_OK: 所有测试通过
 *     - ESP_FAIL: 有测试失败
 */
esp_err_t device_run_full_test(void);

/**
 * @brief 运行快速设备测试
 * 
 * @return
 *     - ESP_OK: 快速测试通过
 *     - ESP_FAIL: 快速测试失败
 */
esp_err_t device_run_quick_test(void);

/**
 * @brief 运行设备压力测试
 * 
 * @param duration_ms 测试持续时间 (ms)
 * @return
 *     - ESP_OK: 压力测试通过
 *     - ESP_FAIL: 压力测试失败
 */
esp_err_t device_run_stress_test(uint32_t duration_ms);

// ==================== 配置管理接口 ====================

/**
 * @brief 保存当前设备配置到NVS
 * 
 * @return
 *     - ESP_OK: 保存成功
 *     - ESP_FAIL: 保存失败
 */
esp_err_t device_save_config(void);

/**
 * @brief 从NVS加载设备配置
 * 
 * @return
 *     - ESP_OK: 加载成功
 *     - ESP_FAIL: 加载失败
 */
esp_err_t device_load_config(void);

/**
 * @brief 清除NVS中的设备配置
 * 
 * @return
 *     - ESP_OK: 清除成功
 *     - ESP_FAIL: 清除失败
 */
esp_err_t device_clear_config(void);

// ==================== 便捷宏定义 ====================

/**
 * @brief 便捷宏：设置风扇速度
 */
#define DEVICE_SET_FAN(speed) fan_set_speed(speed)

/**
 * @brief 便捷宏：设置板载LED颜色
 */
#define DEVICE_SET_BOARD_LED(r, g, b) board_led_set_color((led_color_t){r, g, b})

/**
 * @brief 便捷宏：设置触摸LED颜色
 */
#define DEVICE_SET_TOUCH_LED(r, g, b) touch_led_set_color((led_color_t){r, g, b})

/**
 * @brief 便捷宏：设置GPIO输出
 */
#define DEVICE_SET_GPIO(pin, state) gpio_set_output(pin, state)

/**
 * @brief 便捷宏：读取GPIO输入
 */
#define DEVICE_READ_GPIO(pin, state_ptr) gpio_read_input(pin, state_ptr)

/**
 * @brief 便捷宏：获取系统运行时间
 */
#define DEVICE_GET_UPTIME() system_get_uptime_ms()

/**
 * @brief 便捷宏：获取可用内存
 */
#define DEVICE_GET_FREE_MEMORY() system_get_free_heap()

// ==================== 预定义颜色 ====================

#define LED_COLOR_RED       ((led_color_t){255, 0, 0})
#define LED_COLOR_GREEN     ((led_color_t){0, 255, 0})
#define LED_COLOR_BLUE      ((led_color_t){0, 0, 255})
#define LED_COLOR_WHITE     ((led_color_t){255, 255, 255})
#define LED_COLOR_YELLOW    ((led_color_t){255, 255, 0})
#define LED_COLOR_CYAN      ((led_color_t){0, 255, 255})
#define LED_COLOR_MAGENTA   ((led_color_t){255, 0, 255})
#define LED_COLOR_OFF       ((led_color_t){0, 0, 0})

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_INTERFACE_H */
