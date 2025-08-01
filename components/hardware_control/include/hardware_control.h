/**
 * @file hardware_control.h
 * @brief ESP32S3 硬件控制组件 - 风扇、LED、GPIO控制接口
 * 
 * 这个组件提供了对ESP32S3板子上各种硬件设备的统一控制接口
 */

#ifndef HARDWARE_CONTROL_H
#define HARDWARE_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 类型定义 ====================

/**
 * @brief LED颜色结构体
 */
typedef struct {
    uint8_t red;    /*!< 红色分量 (0-255) */
    uint8_t green;  /*!< 绿色分量 (0-255) */
    uint8_t blue;   /*!< 蓝色分量 (0-255) */
} led_color_t;

/**
 * @brief GPIO状态枚举
 */
typedef enum {
    GPIO_STATE_LOW = 0,  /*!< 低电平 */
    GPIO_STATE_HIGH = 1  /*!< 高电平 */
} gpio_state_t;

/**
 * @brief LED效果类型
 */
typedef enum {
    LED_EFFECT_SOLID = 0,    /*!< 纯色 */
    LED_EFFECT_RAINBOW,      /*!< 彩虹效果 */
    LED_EFFECT_BREATHING,    /*!< 呼吸效果 */
    LED_EFFECT_GRADIENT      /*!< 渐变效果 */
} led_effect_t;

/**
 * @brief 硬件状态结构体
 */
typedef struct {
    uint8_t fan_speed;                /*!< 风扇速度 (0-100%) */
    uint8_t board_led_brightness;     /*!< 板载LED亮度 (0-100%) */
    uint8_t touch_led_brightness;     /*!< 触摸LED亮度 (0-100%) */
    led_color_t board_led_color;      /*!< 板载LED颜色 */
    led_color_t touch_led_color;      /*!< 触摸LED颜色 */
    bool initialized;                 /*!< 硬件是否已初始化 */
} hardware_status_t;

// ==================== 初始化接口 ====================

/**
 * @brief 初始化硬件控制组件
 * 
 * @return
 *     - ESP_OK: 初始化成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t hardware_control_init(void);

/**
 * @brief 反初始化硬件控制组件
 * 
 * @return
 *     - ESP_OK: 反初始化成功
 *     - ESP_FAIL: 反初始化失败
 */
esp_err_t hardware_control_deinit(void);

/**
 * @brief 获取硬件初始化状态
 * 
 * @return true: 已初始化, false: 未初始化
 */
bool hardware_control_is_initialized(void);

// ==================== 风扇控制接口 ====================

/**
 * @brief 设置风扇速度
 * 
 * @param speed 风扇速度 (0-100%)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t fan_set_speed(uint8_t speed);

/**
 * @brief 获取当前风扇速度
 * 
 * @return 当前风扇速度 (0-100%)
 */
uint8_t fan_get_speed(void);

/**
 * @brief 启动风扇（默认速度）
 * 
 * @return
 *     - ESP_OK: 启动成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t fan_start(void);

/**
 * @brief 停止风扇
 * 
 * @return
 *     - ESP_OK: 停止成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t fan_stop(void);

// ==================== 板载LED控制接口 ====================

/**
 * @brief 设置板载LED颜色
 * 
 * @param color LED颜色
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t board_led_set_color(led_color_t color);

/**
 * @brief 设置板载LED亮度
 * 
 * @param brightness 亮度 (0-100%)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t board_led_set_brightness(uint8_t brightness);

/**
 * @brief 设置板载LED效果
 * 
 * @param effect 效果类型
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t board_led_set_effect(led_effect_t effect);

/**
 * @brief 关闭板载LED
 * 
 * @return
 *     - ESP_OK: 关闭成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t board_led_turn_off(void);

/**
 * @brief 获取板载LED当前颜色
 * 
 * @return 当前LED颜色
 */
led_color_t board_led_get_color(void);

/**
 * @brief 获取板载LED当前亮度
 * 
 * @return 当前亮度 (0-100%)
 */
uint8_t board_led_get_brightness(void);

// ==================== 触摸LED控制接口 ====================

/**
 * @brief 设置触摸LED颜色
 * 
 * @param color LED颜色
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t touch_led_set_color(led_color_t color);

/**
 * @brief 设置触摸LED亮度
 * 
 * @param brightness 亮度 (0-100%)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t touch_led_set_brightness(uint8_t brightness);

/**
 * @brief 关闭触摸LED
 * 
 * @return
 *     - ESP_OK: 关闭成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t touch_led_turn_off(void);

/**
 * @brief 获取触摸LED当前颜色
 * 
 * @return 当前LED颜色
 */
led_color_t touch_led_get_color(void);

/**
 * @brief 获取触摸LED当前亮度
 * 
 * @return 当前亮度 (0-100%)
 */
uint8_t touch_led_get_brightness(void);

// ==================== GPIO控制接口 ====================

/**
 * @brief 设置GPIO输出状态
 * 
 * @param pin GPIO引脚号
 * @param state GPIO状态
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t gpio_set_output(uint8_t pin, gpio_state_t state);

/**
 * @brief 读取GPIO输入状态
 * 
 * @param pin GPIO引脚号
 * @param state 存储读取状态的指针
 * @return
 *     - ESP_OK: 读取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t gpio_read_input(uint8_t pin, gpio_state_t *state);

/**
 * @brief 切换GPIO输出状态
 * 
 * @param pin GPIO引脚号
 * @return
 *     - ESP_OK: 切换成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t gpio_toggle_output(uint8_t pin);

// ==================== 测试接口 ====================

/**
 * @brief 测试风扇功能
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_fan(void);

/**
 * @brief 测试板载LED功能
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_board_led(void);

/**
 * @brief 测试触摸LED功能
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_touch_led(void);

/**
 * @brief 测试GPIO功能
 * 
 * @param pin 要测试的GPIO引脚
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_gpio(uint8_t pin);

/**
 * @brief 测试所有硬件功能
 * 
 * @return
 *     - ESP_OK: 所有测试通过
 *     - ESP_FAIL: 有测试失败
 */
esp_err_t hardware_test_all(void);

// ==================== 状态查询接口 ====================

/**
 * @brief 获取硬件状态
 * 
 * @param status 存储状态信息的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t hardware_get_status(hardware_status_t *status);

/**
 * @brief 打印硬件状态信息
 * 
 * @return
 *     - ESP_OK: 打印成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t hardware_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_CONTROL_H */
