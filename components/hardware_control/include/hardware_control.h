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
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

// 风扇控制配置
#define FAN_PWM_PIN         41      // 风扇PWM控制引脚
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_MODE        LEDC_LOW_SPEED_MODE
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_RESOLUTION  LEDC_TIMER_8_BIT
#define FAN_PWM_FREQUENCY   25000   // 25kHz PWM频率

// WS2812 LED配置
#define BOARD_WS2812_PIN    42      // 板载WS2812控制引脚
#define BOARD_WS2812_NUM    28      // 板载WS2812数量
#define TOUCH_WS2812_PIN    45      // 触摸开关WS2812引脚
#define TOUCH_WS2812_NUM    1       // 触摸开关WS2812数量

// LED矩阵配置
#define LED_MATRIX_PIN      9       // LED矩阵WS2812控制引脚
#define LED_MATRIX_WIDTH    32      // LED矩阵宽度
#define LED_MATRIX_HEIGHT   32      // LED矩阵高度
#define LED_MATRIX_NUM      (LED_MATRIX_WIDTH * LED_MATRIX_HEIGHT)  // LED总数量

// LED Strip RMT配置
#define LED_RMT_CLK_FREQ    10000000  // 10MHz RMT时钟频率

// 默认值配置
#define DEFAULT_LED_BRIGHTNESS  50  // 默认LED亮度(%)
#define DEFAULT_FAN_SPEED_ON    50  // 默认风扇开启速度(%)

// USB MUX控制引脚
#define ESP32_MUX1_SEL      8       // USB MUX1选择引脚 (GPIO8)
#define ESP32_MUX2_SEL      48      // USB MUX2选择引脚 (GPIO48)

// AGX电源控制引脚
#define AGX_POWER_PIN      3       // AGX关机引脚 (GPIO3)
#define AGX_RESET_PIN      1       // AGX重启引脚 (GPIO1)
#define AGX_RECOVERY_PIN   40      // AGX恢复模式引脚 (GPIO40)

// LPMU电源控制引脚
#define LPMU_POWER_BTN_PIN  46      // LPMU电源按钮引脚 (GPIO46)
#define LPMU_RESET_PIN      2       // LPMU重启引脚 (GPIO2)

// 电源控制时序配置
#define AGX_RESET_PULSE_MS     1000    // AGX重启脉冲持续时间(毫秒)
#define LPMU_POWER_PULSE_MS     300     // LPMU电源按钮脉冲持续时间(毫秒)
#define LPMU_RESET_PULSE_MS     300     // LPMU重启脉冲持续时间(毫秒)

// ==================== 类型定义 ====================

/**
 * @brief LED颜色结构
 */
typedef struct {
    uint8_t red;    ///< 红色分量 (0-255)
    uint8_t green;  ///< 绿色分量 (0-255)
    uint8_t blue;   ///< 蓝色分量 (0-255)
} led_color_t;

/**
 * @brief LED效果枚举
 */
typedef enum {
    LED_EFFECT_SOLID = 0,   ///< 纯色
    LED_EFFECT_RAINBOW      ///< 彩虹渐变
} led_effect_t;

/**
 * @brief GPIO状态枚举
 */
typedef enum {
    GPIO_STATE_LOW = 0,     ///< 低电平
    GPIO_STATE_HIGH = 1     ///< 高电平
} gpio_state_t;

/**
 * @brief USB MUX目标枚举
 */
typedef enum {
    USB_MUX_ESP32S3 = 0,    ///< 连接到ESP32S3
    USB_MUX_AGX = 1,        ///< 连接到AGX
    USB_MUX_LPMU = 2        ///< 连接到LPMU
} usb_mux_target_t;

/**
 * @brief 电源状态枚举
 */
typedef enum {
    POWER_STATE_OFF = 0,        ///< 关机
    POWER_STATE_ON = 1,         ///< 开机
    POWER_STATE_UNKNOWN = 2     ///< 未知状态
} power_state_t;

/**
 * @brief 硬件状态结构
 */
typedef struct {
    bool initialized;                   ///< 初始化状态
    uint8_t fan_speed;                  ///< 风扇速度 (0-100%)
    led_color_t board_led_color;        ///< 板载LED颜色
    uint8_t board_led_brightness;       ///< 板载LED亮度 (0-100%)
    led_color_t touch_led_color;        ///< 触摸LED颜色
    uint8_t touch_led_brightness;       ///< 触摸LED亮度 (0-100%)
    usb_mux_target_t usb_mux_target;    ///< USB MUX目标
    power_state_t agx_power_state;     ///< AGX电源状态
    power_state_t lpmu_power_state;     ///< LPMU电源状态
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
 */
esp_err_t hardware_control_deinit(void);

/**
 * @brief 检查硬件控制组件是否已初始化
 * 
 * @return
 *     - true: 已初始化
 *     - false: 未初始化
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
 * @param effect LED效果
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
 * @return 当前LED亮度 (0-100%)
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
 * @return 当前LED亮度 (0-100%)
 */
uint8_t touch_led_get_brightness(void);

// ==================== LED矩阵控制接口 ====================

/**
 * @brief LED矩阵动画点结构
 */
typedef struct {
    uint8_t x;      ///< X坐标 (0-31)
    uint8_t y;      ///< Y坐标 (0-31)
    uint8_t r;      ///< 红色分量 (0-255)
    uint8_t g;      ///< 绿色分量 (0-255)
    uint8_t b;      ///< 蓝色分量 (0-255)
} led_matrix_point_t;

/**
 * @brief 初始化LED矩阵
 * 
 * @return
 *     - ESP_OK: 初始化成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t led_matrix_init(void);

/**
 * @brief 设置LED矩阵单个像素颜色
 * 
 * @param x X坐标 (0-31)
 * @param y Y坐标 (0-31)
 * @param color LED颜色
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 坐标无效
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 */
esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color);

/**
 * @brief 清空LED矩阵（全部关闭）
 * 
 * @return
 *     - ESP_OK: 清空成功
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 */
esp_err_t led_matrix_clear(void);

/**
 * @brief 刷新LED矩阵显示
 * 
 * @return
 *     - ESP_OK: 刷新成功
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 */
esp_err_t led_matrix_refresh(void);

/**
 * @brief 设置LED矩阵亮度
 * 
 * @param brightness 亮度 (0-100%)
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 */
esp_err_t led_matrix_set_brightness(uint8_t brightness);

/**
 * @brief 从JSON文件加载并显示动画
 * 
 * @param animation_name 动画名称
 * @return
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_NOT_FOUND: 动画未找到
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 *     - ESP_FAIL: 文件读取失败
 */
esp_err_t led_matrix_load_animation(const char *animation_name);

/**
 * @brief 显示测试图案
 * 
 * @return
 *     - ESP_OK: 显示成功
 *     - ESP_ERR_INVALID_STATE: 矩阵未初始化
 */
esp_err_t led_matrix_test_pattern(void);

// ==================== GPIO控制接口 ====================

/**
 * @brief 设置GPIO为输出模式并设置电平
 * 
 * @param pin GPIO引脚号
 * @param state GPIO状态
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t gpio_set_output(uint8_t pin, gpio_state_t state);

/**
 * @brief 读取GPIO当前电平（不改变方向，但可能干扰状态）
 * 
 * ⚠️ 警告：此函数可能干扰输出模式的GPIO状态！
 * 
 * 直接读取GPIO的当前电平状态，无论GPIO是输出还是输入模式
 * 这个函数不会改变GPIO的方向配置，但根据测试发现可能会干扰GPIO的输出状态
 * 
 * @param pin GPIO引脚号
 * @param state 存储GPIO状态的指针
 * @return
 *     - ESP_OK: 读取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 * 
 * @warning 避免在关键的GPIO操作（如恢复模式）中使用此函数
 * @note 建议在输出模式下不进行状态验证，以确保GPIO状态稳定
 */
esp_err_t gpio_read_input(uint8_t pin, gpio_state_t *state);

/**
 * @brief 将GPIO设置为输入模式并读取状态
 * 
 * 先将GPIO配置为输入模式，然后读取电平状态
 * 这个函数会改变GPIO的方向配置
 * 
 * @param pin GPIO引脚号
 * @param state 存储GPIO状态的指针
 * @return
 *     - ESP_OK: 读取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t gpio_read_input_mode(uint8_t pin, gpio_state_t *state);

/**
 * @brief 切换GPIO输出状态（已弃用）
 * 
 * ⚠️ 此函数已弃用，不建议使用！
 * 
 * 由于需要读取当前状态来切换，可能会干扰GPIO的输出状态
 * 建议使用 gpio_set_output() 并明确指定目标状态
 * 
 * @param pin GPIO引脚号
 * @return
 *     - ESP_OK: 切换成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 * 
 * @deprecated 使用 gpio_set_output() 代替
 * @warning 避免在关键的GPIO操作中使用
 */
esp_err_t gpio_toggle_output(uint8_t pin);

// ==================== USB MUX控制接口 ====================

/**
 * @brief 设置USB MUX目标
 * 
 * @param target USB MUX目标设备
 * @return
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t usb_mux_set_target(usb_mux_target_t target);

/**
 * @brief 获取当前USB MUX目标
 * 
 * @param target 存储目标的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t usb_mux_get_target(usb_mux_target_t *target);

/**
 * @brief 获取USB MUX目标名称
 * 
 * @param target USB MUX目标
 * @return 目标名称字符串
 */
const char *usb_mux_get_target_name(usb_mux_target_t target);

// ==================== 电源控制接口 ====================

/**
 * @brief AGX设备开机
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t agx_power_on(void);

/**
 * @brief AGX设备关机
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t agx_power_off(void);

/**
 * @brief AGX设备重启
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t agx_reset(void);

/**
 * @brief AGX设备进入恢复模式
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t agx_enter_recovery_mode(void);

/**
 * @brief LPMU设备电源切换
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t lpmu_power_toggle(void);

/**
 * @brief LPMU设备重启
 * 
 * @return
 *     - ESP_OK: 操作成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t lpmu_reset(void);

/**
 * @brief 获取AGX电源状态
 * 
 * @param state 存储状态的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t agx_get_power_state(power_state_t *state);

/**
 * @brief 获取LPMU电源状态
 * 
 * @param state 存储状态的指针
 * @return
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t lpmu_get_power_state(power_state_t *state);

/**
 * @brief 获取电源状态名称
 * 
 * @param state 电源状态
 * @return 状态名称字符串
 */
const char *power_state_get_name(power_state_t state);

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
 * @brief 测试GPIO输出功能（安全模式，不进行读取验证）
 * 
 * @param pin GPIO引脚号
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 * 
 * @note 此函数仅测试GPIO输出功能，不进行状态读取以避免干扰GPIO状态
 */
esp_err_t hardware_test_gpio(uint8_t pin);

/**
 * @brief 测试GPIO输入功能
 * 
 * @param pin GPIO引脚号
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 * 
 * @note 此函数专门用于测试GPIO输入功能，会将GPIO设置为输入模式
 */
esp_err_t hardware_test_gpio_input(uint8_t pin);

/**
 * @brief 测试所有硬件功能
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_all(void);

/**
 * @brief 测试AGX电源控制
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_agx_power(void);

/**
 * @brief 测试LPMU电源控制
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_lpmu_power(void);

/**
 * @brief 测试AGX恢复模式GPIO引脚
 * 
 * 专门测试GPIO40（AGX_RECOVERY_PIN）的输出功能
 * 包括拉高、保持、拉低等操作的验证
 * 
 * @return
 *     - ESP_OK: 测试通过
 *     - ESP_FAIL: 测试失败
 */
esp_err_t hardware_test_orin_recovery_gpio(void);

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
 * @brief 打印硬件状态
 * 
 * @return
 *     - ESP_OK: 打印成功
 *     - ESP_ERR_INVALID_STATE: 硬件未初始化
 */
esp_err_t hardware_print_status(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_CONTROL_H
