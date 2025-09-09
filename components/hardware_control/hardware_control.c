/**
 * @file hardware_control.c
 * @brief ESP32S3 硬件控制组件实现
 */

#include "hardware_control.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "cJSON.h"
#include "ethernet_interface.h"
#include "color_correction.h"
#include "hardware_config.h"

static const char *TAG = "HARDWARE_CONTROL";

// ==================== 静态变量 ====================

static bool s_initialized = false;
static hardware_status_t s_hardware_status = {0};
static led_strip_handle_t s_board_led_strip = NULL;
static led_strip_handle_t s_touch_led_strip = NULL;
static led_strip_handle_t s_matrix_led_strip = NULL;
static uint8_t s_matrix_brightness = DEFAULT_LED_BRIGHTNESS;
static led_color_t s_matrix_buffer[LED_MATRIX_NUM]; // 添加矩阵缓冲区

// 电源监控相关变量
static adc_oneshot_unit_handle_t s_adc2_handle = NULL;
static adc_cali_handle_t s_adc2_cali_handle = NULL;
static bool s_power_monitor_initialized = false;
static bool s_power_uart_initialized = false;
static TaskHandle_t s_power_monitor_task_handle = NULL;
static float s_voltage_threshold = VOLTAGE_CHANGE_THRESHOLD;
static float s_last_supply_voltage = 0.0;

// ==================== 静态函数声明 ====================

static esp_err_t init_fan_pwm(void);
static esp_err_t init_ws2812(void);
static esp_err_t init_usb_mux_gpio(void);
static esp_err_t init_power_control_gpio(void);
static esp_err_t disable_jtag_for_gpio40(void);
static esp_err_t apply_led_color(led_strip_handle_t strip, led_color_t color, uint8_t brightness, uint8_t num_leds);
static void hsv_to_rgb(int hue, int saturation, int value, uint8_t *r, uint8_t *g, uint8_t *b);

// 电源监控相关静态函数声明
static esp_err_t init_power_monitor_adc(void);
static esp_err_t init_power_chip_uart(void);
static esp_err_t deinit_power_monitor_adc(void);
static esp_err_t deinit_power_chip_uart(void);
static void power_monitor_task(void *pvParameters);
static esp_err_t parse_power_chip_data(const uint8_t *raw_data, size_t data_len, power_chip_data_t *data);
static uint8_t calculate_crc8(const uint8_t *data, size_t length);

// ==================== 初始化接口实现 ====================

esp_err_t hardware_control_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Hardware control already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing hardware control component");

    // 初始化状态结构体
    memset(&s_hardware_status, 0, sizeof(hardware_status_t));
    s_hardware_status.board_led_brightness = DEFAULT_LED_BRIGHTNESS;
    s_hardware_status.touch_led_brightness = DEFAULT_LED_BRIGHTNESS;
    s_hardware_status.usb_mux_target = USB_MUX_ESP32S3; // 默认连接到ESP32S3
    s_hardware_status.agx_power_state = POWER_STATE_UNKNOWN;
    s_hardware_status.lpmu_power_state = POWER_STATE_UNKNOWN;
    
    // 初始化AGX监控状态
    s_hardware_status.agx_monitor.network_status = AGX_NET_STATUS_UNKNOWN;
    s_hardware_status.agx_monitor.metrics_available = false;

    // 初始化风扇PWM
    esp_err_t ret = init_fan_pwm();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize fan PWM: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化WS2812
    ret = init_ws2812();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WS2812: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化USB MUX GPIO
    ret = init_usb_mux_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB MUX GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化电源控制GPIO
    ret = init_power_control_gpio();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power control GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化电源监控功能
    ret = power_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power monitor: %s", esp_err_to_name(ret));
        // 电源监控失败不影响整个系统的初始化，只记录警告
    }

    s_initialized = true;
    s_hardware_status.initialized = true;
    
    // 在系统启动时自动进行一次LPMU power toggle，实现随系统启动开机
    // 注意：这必须在设置初始化标志之后执行，因为lpmu_power_toggle()会检查s_initialized状态
    ESP_LOGI(TAG, "Performing startup LPMU power toggle...");
    ret = lpmu_power_toggle();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Startup LPMU power toggle failed: %s", esp_err_to_name(ret));
        // LPMU toggle失败不影响整个系统的初始化，只记录警告
    } else {
        ESP_LOGI(TAG, "Startup LPMU power toggle completed successfully");
    }
    
    ESP_LOGI(TAG, "Hardware control component initialized successfully");
    return ESP_OK;
}

esp_err_t hardware_control_deinit(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "Hardware control not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing hardware control component");

    // 关闭所有设备
    fan_stop();
    board_led_turn_off();
    touch_led_turn_off();

    // 反初始化电源监控功能
    power_monitor_deinit();

    // 释放LED strip资源
    if (s_board_led_strip) {
        led_strip_del(s_board_led_strip);
        s_board_led_strip = NULL;
    }
    if (s_touch_led_strip) {
        led_strip_del(s_touch_led_strip);
        s_touch_led_strip = NULL;
    }

    s_initialized = false;
    s_hardware_status.initialized = false;
    
    ESP_LOGI(TAG, "Hardware control component deinitialized");
    return ESP_OK;
}

bool hardware_control_is_initialized(void)
{
    return s_initialized;
}

// ==================== 风扇控制接口实现 ====================

esp_err_t fan_set_speed(uint8_t speed)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (speed > 100) {
        ESP_LOGE(TAG, "Invalid fan speed: %d (must be 0-100)", speed);
        return ESP_ERR_INVALID_ARG;
    }

    s_hardware_status.fan_speed = speed;
    uint32_t duty = (speed * 255) / 100;
    
    ESP_ERROR_CHECK(ledc_set_duty(FAN_PWM_MODE, FAN_PWM_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(FAN_PWM_MODE, FAN_PWM_CHANNEL));
    
    ESP_LOGI(TAG, "Fan speed set to %d%% (PWM: %" PRIu32 "/255)", speed, duty);
    return ESP_OK;
}

uint8_t fan_get_speed(void)
{
    return s_hardware_status.fan_speed;
}

esp_err_t fan_start(void)
{
    return fan_set_speed(DEFAULT_FAN_SPEED_ON);
}

esp_err_t fan_stop(void)
{
    return fan_set_speed(0);
}

// ==================== 板载LED控制接口实现 ====================

esp_err_t board_led_set_color(led_color_t color)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_hardware_status.board_led_color = color;
    
    // 应用色彩校正
    led_color_t corrected_color;
    esp_err_t correction_ret = apply_color_correction_to_led(color, &corrected_color);
    if (correction_ret != ESP_OK) {
        ESP_LOGW(TAG, "Color correction failed for board LED, using original color");
        corrected_color = color;
    }
    
    esp_err_t ret = apply_led_color(s_board_led_strip, corrected_color, 
                                   s_hardware_status.board_led_brightness, 
                                   BOARD_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set board LED color: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Board LED color set to R:%d G:%d B:%d (corrected: R:%d G:%d B:%d)", 
             color.red, color.green, color.blue,
             corrected_color.red, corrected_color.green, corrected_color.blue);
    return ESP_OK;
}

esp_err_t board_led_set_brightness(uint8_t brightness)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        ESP_LOGE(TAG, "Invalid brightness: %d (must be 0-100)", brightness);
        return ESP_ERR_INVALID_ARG;
    }

    s_hardware_status.board_led_brightness = brightness;
    
    // 重新应用当前颜色以更新亮度
    esp_err_t ret = apply_led_color(s_board_led_strip, s_hardware_status.board_led_color, 
                                   brightness, BOARD_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set board LED brightness: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Board LED brightness set to %d%%", brightness);
    return ESP_OK;
}

esp_err_t board_led_set_effect(led_effect_t effect)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    switch (effect) {
        case LED_EFFECT_RAINBOW:
            for (int i = 0; i < BOARD_WS2812_NUM; i++) {
                int hue = (i * 360) / BOARD_WS2812_NUM;
                uint8_t r, g, b;
                hsv_to_rgb(hue, 100, 100, &r, &g, &b);
                
                uint8_t final_r = (r * s_hardware_status.board_led_brightness) / 100;
                uint8_t final_g = (g * s_hardware_status.board_led_brightness) / 100;
                uint8_t final_b = (b * s_hardware_status.board_led_brightness) / 100;
                
                ESP_ERROR_CHECK(led_strip_set_pixel(s_board_led_strip, i, final_r, final_g, final_b));
            }
            ESP_ERROR_CHECK(led_strip_refresh(s_board_led_strip));
            ESP_LOGI(TAG, "Board LED rainbow effect applied");
            break;
            
        case LED_EFFECT_SOLID:
            return board_led_set_color(s_hardware_status.board_led_color);
            
        default:
            ESP_LOGE(TAG, "Unsupported LED effect: %d", effect);
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t board_led_turn_off(void)
{
    led_color_t off_color = {0, 0, 0};
    return board_led_set_color(off_color);
}

led_color_t board_led_get_color(void)
{
    return s_hardware_status.board_led_color;
}

uint8_t board_led_get_brightness(void)
{
    return s_hardware_status.board_led_brightness;
}

// ==================== 触摸LED控制接口实现 ====================

esp_err_t touch_led_set_color(led_color_t color)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    s_hardware_status.touch_led_color = color;
    
    // 应用色彩校正
    led_color_t corrected_color;
    esp_err_t correction_ret = apply_color_correction_to_led(color, &corrected_color);
    if (correction_ret != ESP_OK) {
        ESP_LOGW(TAG, "Color correction failed for touch LED, using original color");
        corrected_color = color;
    }
    
    esp_err_t ret = apply_led_color(s_touch_led_strip, corrected_color, 
                                   s_hardware_status.touch_led_brightness, 
                                   TOUCH_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch LED color: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Touch LED color set to R:%d G:%d B:%d (corrected: R:%d G:%d B:%d)", 
             color.red, color.green, color.blue,
             corrected_color.red, corrected_color.green, corrected_color.blue);
    return ESP_OK;
}

esp_err_t touch_led_set_brightness(uint8_t brightness)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        ESP_LOGE(TAG, "Invalid brightness: %d (must be 0-100)", brightness);
        return ESP_ERR_INVALID_ARG;
    }

    s_hardware_status.touch_led_brightness = brightness;
    
    // 重新应用当前颜色以更新亮度
    esp_err_t ret = apply_led_color(s_touch_led_strip, s_hardware_status.touch_led_color, 
                                   brightness, TOUCH_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch LED brightness: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Touch LED brightness set to %d%%", brightness);
    return ESP_OK;
}

esp_err_t touch_led_turn_off(void)
{
    led_color_t off_color = {0, 0, 0};
    return touch_led_set_color(off_color);
}

led_color_t touch_led_get_color(void)
{
    return s_hardware_status.touch_led_color;
}

uint8_t touch_led_get_brightness(void)
{
    return s_hardware_status.touch_led_brightness;
}

// ==================== GPIO控制接口实现 ====================

esp_err_t gpio_set_output(uint8_t pin, gpio_state_t state)
{
    esp_err_t ret = gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d as output: %s", pin, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_level(pin, state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d level: %s", pin, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "GPIO%d set to %s", pin, state ? "HIGH" : "LOW");
    return ESP_OK;
}

esp_err_t gpio_read_input(uint8_t pin, gpio_state_t *state)
{
    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 警告：此函数可能干扰输出模式的GPIO状态！
    // 建议仅在确需读取当前电平且明确理解风险时使用
    // 对于关键的GPIO操作（如恢复模式），应避免使用此函数
    ESP_LOGW(TAG, "gpio_read_input() on GPIO%d - may interfere with output state!", pin);
    
    // 直接读取GPIO电平，不改变方向
    // gpio_get_level() 可以在输出模式下读取实际的输出电平
    int level = gpio_get_level(pin);
    *state = (level == 0) ? GPIO_STATE_LOW : GPIO_STATE_HIGH;

    ESP_LOGI(TAG, "GPIO%d current level: %s", pin, *state ? "HIGH" : "LOW");
    return ESP_OK;
}

esp_err_t gpio_read_input_mode(uint8_t pin, gpio_state_t *state)
{
    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // 这个函数专门用于将GPIO设置为输入模式并读取
    esp_err_t ret = gpio_set_direction(pin, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d as input: %s", pin, esp_err_to_name(ret));
        return ret;
    }

    int level = gpio_get_level(pin);
    *state = (level == 0) ? GPIO_STATE_LOW : GPIO_STATE_HIGH;

    ESP_LOGI(TAG, "GPIO%d input state: %s", pin, *state ? "HIGH" : "LOW");
    return ESP_OK;
}

esp_err_t gpio_toggle_output(uint8_t pin)
{
    // 对于输出引脚的切换，我们不应该读取当前状态，因为这可能干扰GPIO
    // 相反，我们维护一个简单的状态管理或要求调用者指定目标状态
    ESP_LOGW(TAG, "gpio_toggle_output() is deprecated - use gpio_set_output() with explicit state instead");
    ESP_LOGW(TAG, "Avoid using toggle for critical GPIO operations like recovery mode");
    
    // 作为临时解决方案，我们设置为LOW状态
    // 实际应用中建议调用者使用 gpio_set_output() 并明确指定状态
    return gpio_set_output(pin, GPIO_STATE_LOW);
}

// ==================== USB MUX控制接口实现 ====================

esp_err_t usb_mux_set_target(usb_mux_target_t target)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    gpio_state_t mux1_state, mux2_state;

    // 根据目标设备设置MUX引脚状态
    switch (target) {
        case USB_MUX_ESP32S3:  // mux1=0, mux2=0
            mux1_state = GPIO_STATE_LOW;
            mux2_state = GPIO_STATE_LOW;
            break;
        case USB_MUX_AGX:      // mux1=1, mux2=0
            mux1_state = GPIO_STATE_HIGH;
            mux2_state = GPIO_STATE_LOW;
            break;
        case USB_MUX_LPMU:     // mux1=1, mux2=1
            mux1_state = GPIO_STATE_HIGH;
            mux2_state = GPIO_STATE_HIGH;
            break;
        default:
            ESP_LOGE(TAG, "Invalid USB MUX target: %d", target);
            return ESP_ERR_INVALID_ARG;
    }

    // 设置MUX1引脚
    ret = gpio_set_output(ESP32_MUX1_SEL, mux1_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MUX1 GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置MUX2引脚
    ret = gpio_set_output(ESP32_MUX2_SEL, mux2_state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MUX2 GPIO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 更新状态
    s_hardware_status.usb_mux_target = target;
    
    ESP_LOGI(TAG, "USB MUX switched to %s (MUX1=%d, MUX2=%d)", 
             usb_mux_get_target_name(target), mux1_state, mux2_state);
    
    return ESP_OK;
}

esp_err_t usb_mux_get_target(usb_mux_target_t *target)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (target == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *target = s_hardware_status.usb_mux_target;
    return ESP_OK;
}

const char *usb_mux_get_target_name(usb_mux_target_t target)
{
    switch (target) {
        case USB_MUX_ESP32S3:
            return "ESP32S3";
        case USB_MUX_AGX:
            return "AGX";
        case USB_MUX_LPMU:
            return "LPMU";
        default:
            return "Unknown";
    }
}

// ==================== 电源控制接口实现 ====================

esp_err_t agx_power_on(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = gpio_set_output(AGX_POWER_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power on AGX: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_hardware_status.agx_power_state = POWER_STATE_ON;
    ESP_LOGI(TAG, "AGX powered on (GPIO%d set to LOW)", AGX_POWER_PIN);
    return ESP_OK;
}

esp_err_t agx_power_off(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = gpio_set_output(AGX_POWER_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power off AGX: %s", esp_err_to_name(ret));
        return ret;
    }

    s_hardware_status.agx_power_state = POWER_STATE_OFF;
    ESP_LOGI(TAG, "AGX powered off (GPIO%d set to HIGH)", AGX_POWER_PIN);
    return ESP_OK;
}

esp_err_t agx_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting AGX device");
    
    // 拉高重启引脚
    esp_err_t ret = gpio_set_output(AGX_RESET_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AGX reset pin high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持1000ms
    vTaskDelay(pdMS_TO_TICKS(AGX_RESET_PULSE_MS));

    // 拉低重启引脚
    ret = gpio_set_output(AGX_RESET_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AGX reset pin low: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "AGX reset completed");
    return ESP_OK;
}

esp_err_t agx_enter_recovery_mode(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Entering AGX recovery mode");
    
    // 步骤1: 将GPIO40拉高并保持1000ms
    ESP_LOGI(TAG, "Step 1: Setting GPIO%d (recovery pin) HIGH", AGX_RECOVERY_PIN);
    // esp_err_t ret = gpio_set_direction(AGX_RECOVERY_PIN, GPIO_MODE_OUTPUT);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to configure GPIO%d as output: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
    //     return ret;
    // }
    
    esp_err_t ret = gpio_set_level(AGX_RECOVERY_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d level HIGH: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ret;
    }
    
    // 注意：不进行状态验证，避免干扰GPIO状态
    ESP_LOGI(TAG, "GPIO%d set to HIGH, holding for 1000ms...", AGX_RECOVERY_PIN);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 步骤2: 重启AGX并等待1000ms
    ESP_LOGI(TAG, "Step 2: Executing AGX reset");
    ret = agx_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset AGX during recovery mode entry");
        return ret;
    }
    ESP_LOGI(TAG, "AGX reset completed, waiting 1000ms");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 步骤3: 将GPIO40拉低
    ESP_LOGI(TAG, "Step 3: Setting GPIO%d (recovery pin) LOW", AGX_RECOVERY_PIN);
    ret = gpio_set_level(AGX_RECOVERY_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d level LOW: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ret;
    }
    
    // 注意：不进行状态验证，避免干扰GPIO状态
    ESP_LOGI(TAG, "GPIO%d set to LOW", AGX_RECOVERY_PIN);

    // 步骤4: 切换USB MUX到AGX
    ESP_LOGI(TAG, "Step 4: Switching USB MUX to AGX");
    ret = usb_mux_set_target(USB_MUX_AGX);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch USB MUX to AGX during recovery mode");
        return ret;
    }

    ESP_LOGI(TAG, "AGX recovery mode entry completed successfully");
    return ESP_OK;
}

esp_err_t lpmu_power_toggle(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Toggling LPMU power");
    
    // 拉高电源按钮引脚
    esp_err_t ret = gpio_set_output(LPMU_POWER_BTN_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU power button high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持300ms
    vTaskDelay(pdMS_TO_TICKS(LPMU_POWER_PULSE_MS));

    // 拉低电源按钮引脚
    ret = gpio_set_output(LPMU_POWER_BTN_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU power button low: %s", esp_err_to_name(ret));
        return ret;
    }

    // 切换电源状态
    if (s_hardware_status.lpmu_power_state == POWER_STATE_ON) {
        s_hardware_status.lpmu_power_state = POWER_STATE_OFF;
        ESP_LOGI(TAG, "LPMU power toggled to OFF");
    } else {
        s_hardware_status.lpmu_power_state = POWER_STATE_ON;
        ESP_LOGI(TAG, "LPMU power toggled to ON");
    }

    return ESP_OK;
}

esp_err_t lpmu_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting LPMU device");
    
    // 拉高重启引脚
    esp_err_t ret = gpio_set_output(LPMU_RESET_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU reset pin high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持300ms
    vTaskDelay(pdMS_TO_TICKS(LPMU_RESET_PULSE_MS));

    // 拉低重启引脚
    ret = gpio_set_output(LPMU_RESET_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU reset pin low: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LPMU reset completed");
    return ESP_OK;
}

esp_err_t agx_get_power_state(power_state_t *state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_hardware_status.agx_power_state;
    return ESP_OK;
}

esp_err_t lpmu_get_power_state(power_state_t *state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_hardware_status.lpmu_power_state;
    return ESP_OK;
}

const char *power_state_get_name(power_state_t state)
{
    switch (state) {
        case POWER_STATE_OFF:
            return "OFF";
        case POWER_STATE_ON:
            return "ON";
        case POWER_STATE_UNKNOWN:
            return "UNKNOWN";
        default:
            return "INVALID";
    }
}

// ==================== 测试接口实现 ====================

esp_err_t hardware_test_fan(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting fan test");
    
    for (int speed = 0; speed <= 100; speed += 25) {
        ESP_LOGI(TAG, "Testing fan at %d%% speed", speed);
        esp_err_t ret = fan_set_speed(speed);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Fan test failed at speed %d%%", speed);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    fan_stop();
    ESP_LOGI(TAG, "Fan test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_board_led(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting board LED test");
    
    led_color_t colors[] = {
        {255, 0, 0},    // Red
        {0, 255, 0},    // Green
        {0, 0, 255},    // Blue
        {255, 255, 255} // White
    };
    
    for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        ESP_LOGI(TAG, "Testing board LED color R:%d G:%d B:%d", 
                 colors[i].red, colors[i].green, colors[i].blue);
        esp_err_t ret = board_led_set_color(colors[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Board LED test failed");
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    board_led_turn_off();
    ESP_LOGI(TAG, "Board LED test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_touch_led(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting touch LED test");
    
    led_color_t colors[] = {
        {255, 0, 0},    // Red
        {0, 255, 0},    // Green
        {0, 0, 255},    // Blue
        {255, 255, 255} // White
    };
    
    for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        ESP_LOGI(TAG, "Testing touch LED color R:%d G:%d B:%d", 
                 colors[i].red, colors[i].green, colors[i].blue);
        esp_err_t ret = touch_led_set_color(colors[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch LED test failed");
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    touch_led_turn_off();
    ESP_LOGI(TAG, "Touch LED test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_gpio(uint8_t pin)
{
    ESP_LOGI(TAG, "Starting GPIO%d safe output test (no reading)", pin);
    
    // 仅测试输出模式，不进行任何读取操作以避免干扰
    ESP_LOGI(TAG, "Testing GPIO%d output mode - HIGH", pin);
    esp_err_t ret = gpio_set_output(pin, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d output HIGH test failed", pin);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPIO%d set to HIGH, waiting 1000ms", pin);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "Testing GPIO%d output mode - LOW", pin);
    ret = gpio_set_output(pin, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d output LOW test failed", pin);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPIO%d set to LOW, waiting 1000ms", pin);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "GPIO%d safe output test completed successfully", pin);
    ESP_LOGW(TAG, "Note: No state verification performed to avoid GPIO interference");
    return ESP_OK;
}

esp_err_t hardware_test_gpio_input(uint8_t pin)
{
    ESP_LOGI(TAG, "Starting GPIO%d input mode test", pin);
    
    // 专门用于测试输入模式的函数
    gpio_state_t state;
    esp_err_t ret = gpio_read_input_mode(pin, &state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d input mode test failed", pin);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPIO%d input state: %s", pin, state ? "HIGH" : "LOW");
    
    ESP_LOGI(TAG, "GPIO%d input test completed successfully", pin);
    return ESP_OK;
}

esp_err_t hardware_test_all(void)
{
    ESP_LOGI(TAG, "Starting comprehensive hardware test");
    
    esp_err_t ret = hardware_test_fan();
    if (ret != ESP_OK) return ret;
    
    ret = hardware_test_board_led();
    if (ret != ESP_OK) return ret;
    
    ret = hardware_test_touch_led();
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "All hardware tests completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_agx_power(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting AGX power control test");
    
    // 测试开机
    ESP_LOGI(TAG, "Testing AGX power on");
    esp_err_t ret = agx_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AGX power on test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试关机
    ESP_LOGI(TAG, "Testing AGX power off");
    ret = agx_power_off();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AGX power off test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "AGX power control test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_lpmu_power(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting LPMU power control test");
    
    // 测试电源切换
    ESP_LOGI(TAG, "Testing LPMU power toggle");
    esp_err_t ret = lpmu_power_toggle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LPMU power toggle test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 再次切换
    ESP_LOGI(TAG, "Testing LPMU power toggle again");
    ret = lpmu_power_toggle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LPMU power toggle test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "LPMU power control test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_agx_recovery_gpio(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting comprehensive GPIO%d diagnostics", AGX_RECOVERY_PIN);
    
    // 步骤1: 检查GPIO有效性
    if (AGX_RECOVERY_PIN < 0 || AGX_RECOVERY_PIN >= SOC_GPIO_PIN_COUNT) {
        ESP_LOGE(TAG, "GPIO%d is out of valid range (0-%d)", AGX_RECOVERY_PIN, SOC_GPIO_PIN_COUNT-1);
        return ESP_FAIL;
    }
    
    // 步骤2: 如果是GPIO40，特别处理JTAG问题
    if (AGX_RECOVERY_PIN == 40) {
        ESP_LOGI(TAG, "GPIO40 detected - performing JTAG disable and verification");
        esp_err_t ret = disable_jtag_for_gpio40();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable JTAG for GPIO40: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }
    } else {
        // 步骤2: 重置GPIO配置
        ESP_LOGI(TAG, "Resetting GPIO%d configuration", AGX_RECOVERY_PIN);
        esp_err_t ret = gpio_reset_pin(AGX_RECOVERY_PIN);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reset GPIO%d: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 给硬件一点时间
    }
    
    // 步骤3: 配置为输出模式（带详细配置）
    ESP_LOGI(TAG, "Configuring GPIO%d as output with detailed settings", AGX_RECOVERY_PIN);
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << AGX_RECOVERY_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "GPIO%d configured successfully", AGX_RECOVERY_PIN);
    
    // 步骤4: 测试LOW状态
    ESP_LOGI(TAG, "Testing LOW state on GPIO%d", AGX_RECOVERY_PIN);
    ret = gpio_set_level(AGX_RECOVERY_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d LOW: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    int level = gpio_get_level(AGX_RECOVERY_PIN);
    ESP_LOGI(TAG, "GPIO%d LOW test - Expected: 0, Got: %d %s", 
             AGX_RECOVERY_PIN, level, (level == 0) ? "[PASS]" : "[FAIL]");
    
    // 步骤5: 测试HIGH状态
    ESP_LOGI(TAG, "Testing HIGH state on GPIO%d", AGX_RECOVERY_PIN);
    ret = gpio_set_level(AGX_RECOVERY_PIN, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d HIGH: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    level = gpio_get_level(AGX_RECOVERY_PIN);
    ESP_LOGI(TAG, "GPIO%d HIGH test - Expected: 1, Got: %d %s", 
             AGX_RECOVERY_PIN, level, (level == 1) ? "[PASS]" : "[FAIL]");
    
    if (level != 1) {
        ESP_LOGE(TAG, "GPIO%d HIGH state failed! This may indicate:", AGX_RECOVERY_PIN);
        ESP_LOGE(TAG, "1. Hardware short to ground");
        ESP_LOGE(TAG, "2. External pull-down resistor");
        ESP_LOGE(TAG, "3. GPIO%d connected to low-impedance load", AGX_RECOVERY_PIN);
        ESP_LOGE(TAG, "4. GPIO%d multiplexed with other functions", AGX_RECOVERY_PIN);
        
        // 尝试使能内部上拉
        ESP_LOGI(TAG, "Attempting to enable internal pull-up on GPIO%d", AGX_RECOVERY_PIN);
        gpio_pullup_en(AGX_RECOVERY_PIN);
        vTaskDelay(pdMS_TO_TICKS(100));
        
        level = gpio_get_level(AGX_RECOVERY_PIN);
        ESP_LOGI(TAG, "GPIO%d with pull-up - Got: %d %s", 
                 AGX_RECOVERY_PIN, level, (level == 1) ? "[PASS]" : "[STILL FAIL]");
        
        if (level == 1) {
            ESP_LOGW(TAG, "GPIO%d works with internal pull-up. External load may be too strong.", AGX_RECOVERY_PIN);
        }
        
        return ESP_FAIL;
    }
    
    // 步骤6: 测试持续时间
    ESP_LOGI(TAG, "Testing 1000ms HIGH duration on GPIO%d", AGX_RECOVERY_PIN);
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        level = gpio_get_level(AGX_RECOVERY_PIN);
        if (level != 1) {
            ESP_LOGE(TAG, "GPIO%d lost HIGH state after %dms! Got: %d", AGX_RECOVERY_PIN, (i+1)*100, level);
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "GPIO%d maintained HIGH for 1000ms [PASS]", AGX_RECOVERY_PIN);
    
    // 步骤7: 恢复LOW状态
    ESP_LOGI(TAG, "Setting GPIO%d back to LOW", AGX_RECOVERY_PIN);
    ret = gpio_set_level(AGX_RECOVERY_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d LOW: %s", AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    level = gpio_get_level(AGX_RECOVERY_PIN);
    ESP_LOGI(TAG, "Final GPIO%d LOW test - Expected: 0, Got: %d %s", 
             AGX_RECOVERY_PIN, level, (level == 0) ? "[PASS]" : "[FAIL]");
    
    ESP_LOGI(TAG, "GPIO%d comprehensive diagnostics completed", AGX_RECOVERY_PIN);
    return ESP_OK;
}

// ==================== 状态查询接口实现 ====================

esp_err_t hardware_get_status(hardware_status_t *status)
{
    if (status == NULL) {
        ESP_LOGE(TAG, "Status pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    *status = s_hardware_status;
    return ESP_OK;
}

esp_err_t hardware_print_status(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    printf("\n=== 硬件状态 ===\n");
    printf("风扇速度: %d%%\n", s_hardware_status.fan_speed);
    printf("板载LED: R:%d G:%d B:%d (亮度:%d%%)\n", 
           s_hardware_status.board_led_color.red,
           s_hardware_status.board_led_color.green,
           s_hardware_status.board_led_color.blue,
           s_hardware_status.board_led_brightness);
    printf("触摸LED: R:%d G:%d B:%d (亮度:%d%%)\n", 
           s_hardware_status.touch_led_color.red,
           s_hardware_status.touch_led_color.green,
           s_hardware_status.touch_led_color.blue,
           s_hardware_status.touch_led_brightness);
    printf("USB MUX目标: %s\n", usb_mux_get_target_name(s_hardware_status.usb_mux_target));
    printf("AGX电源状态: %s\n", power_state_get_name(s_hardware_status.agx_power_state));
    printf("LPMU电源状态: %s\n", power_state_get_name(s_hardware_status.lpmu_power_state));
    printf("初始化状态: %s\n", s_hardware_status.initialized ? "已初始化" : "未初始化");
    printf("================\n");
    
    return ESP_OK;
}

// ==================== 静态函数实现 ====================

static esp_err_t init_fan_pwm(void)
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = FAN_PWM_RESOLUTION,
        .freq_hz = FAN_PWM_FREQUENCY,
        .speed_mode = FAN_PWM_MODE,
        .timer_num = FAN_PWM_TIMER,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        return ret;
    }

    ledc_channel_config_t ledc_channel = {
        .channel = FAN_PWM_CHANNEL,
        .duty = 0,
        .gpio_num = FAN_PWM_PIN,
        .speed_mode = FAN_PWM_MODE,
        .timer_sel = FAN_PWM_TIMER,
        .intr_type = LEDC_INTR_DISABLE
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ESP_LOGI(TAG, "Fan PWM initialized on GPIO%d", FAN_PWM_PIN);
    return ESP_OK;
}

static esp_err_t init_ws2812(void)
{
    // Board LED strip configuration
    led_strip_config_t board_strip_config = {
        .strip_gpio_num = BOARD_WS2812_PIN,
        .max_leds = BOARD_WS2812_NUM,
    };
    
    led_strip_rmt_config_t board_rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_CLK_FREQ,
        .flags.with_dma = false,
    };
    
    esp_err_t ret = led_strip_new_rmt_device(&board_strip_config, &board_rmt_config, &s_board_led_strip);
    if (ret != ESP_OK) {
        return ret;
    }

    // Touch LED strip configuration
    led_strip_config_t touch_strip_config = {
        .strip_gpio_num = TOUCH_WS2812_PIN,
        .max_leds = TOUCH_WS2812_NUM,
    };
    
    led_strip_rmt_config_t touch_rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_CLK_FREQ,
        .flags.with_dma = false,
    };
    
    ret = led_strip_new_rmt_device(&touch_strip_config, &touch_rmt_config, &s_touch_led_strip);
    if (ret != ESP_OK) {
        led_strip_del(s_board_led_strip);
        s_board_led_strip = NULL;
        return ret;
    }

    // LED Matrix strip configuration
    led_strip_config_t matrix_strip_config = {
        .strip_gpio_num = LED_MATRIX_PIN,
        .max_leds = LED_MATRIX_NUM,
    };
    
    led_strip_rmt_config_t matrix_rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = LED_RMT_CLK_FREQ,
        .flags.with_dma = false,
    };
    
    ret = led_strip_new_rmt_device(&matrix_strip_config, &matrix_rmt_config, &s_matrix_led_strip);
    if (ret != ESP_OK) {
        led_strip_del(s_board_led_strip);
        led_strip_del(s_touch_led_strip);
        s_board_led_strip = NULL;
        s_touch_led_strip = NULL;
        return ret;
    }

    // Clear all LED strips
    ESP_ERROR_CHECK(led_strip_clear(s_board_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_touch_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_matrix_led_strip));
    
    ESP_LOGI(TAG, "WS2812 initialized - Board: GPIO%d (%d LEDs), Touch: GPIO%d (%d LEDs), Matrix: GPIO%d (%d LEDs)", 
             BOARD_WS2812_PIN, BOARD_WS2812_NUM, TOUCH_WS2812_PIN, TOUCH_WS2812_NUM, LED_MATRIX_PIN, LED_MATRIX_NUM);
    return ESP_OK;
}

static esp_err_t init_usb_mux_gpio(void)
{
    // 配置MUX1 GPIO
    esp_err_t ret = gpio_set_direction(ESP32_MUX1_SEL, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MUX1 GPIO%d as output: %s", 
                 ESP32_MUX1_SEL, esp_err_to_name(ret));
        return ret;
    }

    // 配置MUX2 GPIO
    ret = gpio_set_direction(ESP32_MUX2_SEL, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure MUX2 GPIO%d as output: %s", 
                 ESP32_MUX2_SEL, esp_err_to_name(ret));
        return ret;
    }

    // 设置默认状态 - 连接到ESP32S3 (mux1=0, mux2=0)
    ret = gpio_set_level(ESP32_MUX1_SEL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MUX1 initial level: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gpio_set_level(ESP32_MUX2_SEL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MUX2 initial level: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新状态（这里可以直接设置，因为还在初始化过程中）
    s_hardware_status.usb_mux_target = USB_MUX_ESP32S3;

    ESP_LOGI(TAG, "USB MUX GPIO initialized - MUX1: GPIO%d, MUX2: GPIO%d", 
             ESP32_MUX1_SEL, ESP32_MUX2_SEL);
    return ESP_OK;
}

static esp_err_t init_power_control_gpio(void)
{
    esp_err_t ret;

    // 如果使用GPIO40，需要先禁用JTAG功能
    if (AGX_RECOVERY_PIN == 40) {
        ret = disable_jtag_for_gpio40();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to disable JTAG for GPIO40: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 配置AGX电源控制引脚
    ret = gpio_set_direction(AGX_POWER_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AGX power GPIO%d as output: %s", 
                 AGX_POWER_PIN, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_direction(AGX_RESET_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AGX reset GPIO%d as output: %s", 
                 AGX_RESET_PIN, esp_err_to_name(ret));
        return ret;
    }

    // 特别配置GPIO40，确保完全作为普通GPIO使用
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << AGX_RECOVERY_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure AGX recovery GPIO%d as output: %s", 
                 AGX_RECOVERY_PIN, esp_err_to_name(ret));
        return ret;
    }

    // 配置LPMU电源控制引脚
    ret = gpio_set_direction(LPMU_POWER_BTN_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LPMU power button GPIO%d as output: %s", 
                 LPMU_POWER_BTN_PIN, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_direction(LPMU_RESET_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LPMU reset GPIO%d as output: %s", 
                 LPMU_RESET_PIN, esp_err_to_name(ret));
        return ret;
    }

    // 设置初始状态
    // AGX默认开机状态 (GPIO3 = LOW)
    ret = gpio_set_level(AGX_POWER_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AGX power pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // AGX重启引脚默认为低
    ret = gpio_set_level(AGX_RESET_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AGX reset pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // AGX恢复模式引脚默认为低
    ret = gpio_set_level(AGX_RECOVERY_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AGX recovery pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // LPMU电源按钮默认为低
    ret = gpio_set_level(LPMU_POWER_BTN_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU power button initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // LPMU重启引脚默认为低
    ret = gpio_set_level(LPMU_RESET_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LPMU reset pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // 更新状态（默认AGX开机状态）
    s_hardware_status.agx_power_state = POWER_STATE_ON;
    s_hardware_status.lpmu_power_state = POWER_STATE_UNKNOWN;

    ESP_LOGI(TAG, "Power control GPIO initialized");
    ESP_LOGI(TAG, "AGX - Power: GPIO%d, Reset: GPIO%d, Recovery: GPIO%d", 
             AGX_POWER_PIN, AGX_RESET_PIN, AGX_RECOVERY_PIN);
    ESP_LOGI(TAG, "LPMU - Power: GPIO%d, Reset: GPIO%d", 
             LPMU_POWER_BTN_PIN, LPMU_RESET_PIN);
    
    return ESP_OK;
}

static esp_err_t apply_led_color(led_strip_handle_t strip, led_color_t color, uint8_t brightness, uint8_t num_leds)
{
    if (strip == NULL) {
        ESP_LOGE(TAG, "LED strip handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t final_r = (color.red * brightness) / 100;
    uint8_t final_g = (color.green * brightness) / 100;
    uint8_t final_b = (color.blue * brightness) / 100;
    
    for (int i = 0; i < num_leds; i++) {
        esp_err_t ret = led_strip_set_pixel(strip, i, final_r, final_g, final_b);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set LED pixel %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
    }
    
    esp_err_t ret = led_strip_refresh(strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to refresh LED strip: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static void hsv_to_rgb(int hue, int saturation, int value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int c = (value * saturation) / 100;
    int x = c * (60 - abs((hue % 120) - 60)) / 60;
    int m = value - c;

    if (hue < 60) {
        *r = c + m;
        *g = x + m;
        *b = m;
    } else if (hue < 120) {
        *r = x + m;
        *g = c + m;
        *b = m;
    } else if (hue < 180) {
        *r = m;
        *g = c + m;
        *b = x + m;
    } else if (hue < 240) {
        *r = m;
        *g = x + m;
        *b = c + m;
    } else if (hue < 300) {
        *r = x + m;
        *g = m;
        *b = c + m;
    } else {
        *r = c + m;
        *g = m;
        *b = x + m;
    }

    // Convert to 0-255 range
    *r = (*r * 255) / 100;
    *g = (*g * 255) / 100;
    *b = (*b * 255) / 100;
}

static esp_err_t disable_jtag_for_gpio40(void)
{
    ESP_LOGI(TAG, "Disabling JTAG functionality for GPIO40");
    
    // 步骤1: 重置GPIO40，清除所有之前的配置包括JTAG功能
    esp_err_t ret = gpio_reset_pin(40);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset GPIO40: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 步骤2: 等待硬件稳定
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 步骤3: 显式地设置GPIO40为输出模式，覆盖JTAG功能
    ret = gpio_set_direction(40, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO40 direction: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 步骤4: 设置默认电平为低
    ret = gpio_set_level(40, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO40 level: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 步骤5: 验证GPIO40可以正常工作
    vTaskDelay(pdMS_TO_TICKS(10));
    int level = gpio_get_level(40);
    if (level != 0) {
        ESP_LOGW(TAG, "GPIO40 level verification failed - expected 0, got %d", level);
        ESP_LOGW(TAG, "This may indicate JTAG is still active or hardware conflict");
    } else {
        ESP_LOGI(TAG, "GPIO40 successfully configured as standard GPIO (level verified: %d)", level);
    }
    
    ESP_LOGI(TAG, "JTAG disable procedure completed for GPIO40");
    ESP_LOGI(TAG, "Note: USB Serial JTAG is disabled in sdkconfig (CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n)");
    return ESP_OK;
}

// ==================== LED矩阵控制接口实现 ====================

esp_err_t led_matrix_init(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_matrix_led_strip == NULL) {
        ESP_LOGE(TAG, "LED matrix strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // 初始化矩阵缓冲区为黑色
    memset(s_matrix_buffer, 0, sizeof(s_matrix_buffer));

    ESP_LOGI(TAG, "LED matrix initialized successfully");
    return ESP_OK;
}

static esp_err_t led_matrix_apply_buffer(void)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < LED_MATRIX_NUM; i++) {
        // 应用色彩校正
        led_color_t corrected_color;
        esp_err_t correction_ret = apply_color_correction_to_led(s_matrix_buffer[i], &corrected_color);
        if (correction_ret != ESP_OK) {
            // 如果色彩校正失败，使用原始颜色
            corrected_color = s_matrix_buffer[i];
        }
        
        // 应用亮度调整
        uint8_t r = (corrected_color.red * s_matrix_brightness) / 100;
        uint8_t g = (corrected_color.green * s_matrix_brightness) / 100;
        uint8_t b = (corrected_color.blue * s_matrix_brightness) / 100;
        
        esp_err_t ret = led_strip_set_pixel(s_matrix_led_strip, i, r, g, b);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return led_strip_refresh(s_matrix_led_strip);
}

esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (x >= LED_MATRIX_WIDTH || y >= LED_MATRIX_HEIGHT) {
        return ESP_ERR_INVALID_ARG;
    }

    // 计算线性索引 (行主序)
    uint32_t index = y * LED_MATRIX_WIDTH + x;
    
    // 保存原始颜色到缓冲区
    s_matrix_buffer[index] = color;
    
    // 应用色彩校正
    led_color_t corrected_color;
    esp_err_t correction_ret = apply_color_correction_to_led(color, &corrected_color);
    if (correction_ret != ESP_OK) {
        ESP_LOGW(TAG, "Color correction failed for pixel (%d,%d), using original color", x, y);
        corrected_color = color;
    }
    
    // 应用亮度调整并设置到LED strip
    uint8_t r = (corrected_color.red * s_matrix_brightness) / 100;
    uint8_t g = (corrected_color.green * s_matrix_brightness) / 100;
    uint8_t b = (corrected_color.blue * s_matrix_brightness) / 100;

    return led_strip_set_pixel(s_matrix_led_strip, index, r, g, b);
}

esp_err_t led_matrix_clear(void)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // 清空缓冲区
    memset(s_matrix_buffer, 0, sizeof(s_matrix_buffer));

    // 清空LED strip并刷新
    esp_err_t ret = led_strip_clear(s_matrix_led_strip);
    if (ret == ESP_OK) {
        ret = led_strip_refresh(s_matrix_led_strip);
    }
    return ret;
}

esp_err_t led_matrix_refresh(void)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return led_strip_refresh(s_matrix_led_strip);
}

esp_err_t led_matrix_set_brightness(uint8_t brightness)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (brightness > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    s_matrix_brightness = brightness;
    ESP_LOGI(TAG, "LED matrix brightness set to %d%%", brightness);
    
    // 立即应用新的亮度到当前显示的内容
    return led_matrix_apply_buffer();
}

uint8_t led_matrix_get_brightness(void)
{
    return s_matrix_brightness;
}

esp_err_t led_matrix_test_pattern(void)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Displaying LED matrix test pattern");

    // 清空矩阵
    led_matrix_clear();

    // 创建简单的测试图案
    for (int y = 0; y < LED_MATRIX_HEIGHT; y++) {
        for (int x = 0; x < LED_MATRIX_WIDTH; x++) {
            led_color_t color = {0, 0, 0};
            
            // 边框
            if (x == 0 || x == LED_MATRIX_WIDTH - 1 || 
                y == 0 || y == LED_MATRIX_HEIGHT - 1) {
                color.red = 255;
            }
            // 对角线
            else if (x == y || x == LED_MATRIX_WIDTH - 1 - y) {
                color.green = 255;
            }
            // 中心十字
            else if (x == LED_MATRIX_WIDTH / 2 || y == LED_MATRIX_HEIGHT / 2) {
                color.blue = 255;
            }

            if (color.red || color.green || color.blue) {
                led_matrix_set_pixel(x, y, color);
            }
        }
    }

    return led_matrix_refresh();
}

esp_err_t led_matrix_load_animation(const char *animation_name)
{
    if (!s_initialized || s_matrix_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!animation_name) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading animation: %s", animation_name);

    // 读取JSON文件
    FILE *file = fopen("/sdcard/matrix.json", "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open matrix.json file");
        return ESP_FAIL;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 100000) { // 限制文件大小
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        fclose(file);
        return ESP_FAIL;
    }

    // 读取文件内容
    char *json_string = malloc(file_size + 1);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to allocate memory for JSON");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_size = fread(json_string, 1, file_size, file);
    fclose(file);
    json_string[read_size] = '\0';

    // 解析JSON
    cJSON *json = cJSON_Parse(json_string);
    free(json_string);

    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    // 查找animations数组
    cJSON *animations = cJSON_GetObjectItem(json, "animations");
    if (!animations || !cJSON_IsArray(animations)) {
        ESP_LOGE(TAG, "No animations array found");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // 查找指定的动画
    cJSON *animation = NULL;
    cJSON *anim_item = NULL;
    cJSON_ArrayForEach(anim_item, animations) {
        cJSON *name = cJSON_GetObjectItem(anim_item, "name");
        if (name && cJSON_IsString(name) && 
            strcmp(cJSON_GetStringValue(name), animation_name) == 0) {
            animation = anim_item;
            break;
        }
    }

    if (!animation) {
        ESP_LOGE(TAG, "Animation '%s' not found", animation_name);
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }

    // 获取points数组
    cJSON *points = cJSON_GetObjectItem(animation, "points");
    if (!points || !cJSON_IsArray(points)) {
        ESP_LOGE(TAG, "No points array found in animation");
        cJSON_Delete(json);
        return ESP_FAIL;
    }

    // 清空矩阵
    led_matrix_clear();

    // 绘制所有点
    int point_count = 0;
    cJSON *point = NULL;
    cJSON_ArrayForEach(point, points) {
        cJSON *x_json = cJSON_GetObjectItem(point, "x");
        cJSON *y_json = cJSON_GetObjectItem(point, "y");
        cJSON *r_json = cJSON_GetObjectItem(point, "r");
        cJSON *g_json = cJSON_GetObjectItem(point, "g");
        cJSON *b_json = cJSON_GetObjectItem(point, "b");

        if (cJSON_IsNumber(x_json) && cJSON_IsNumber(y_json) &&
            cJSON_IsNumber(r_json) && cJSON_IsNumber(g_json) && cJSON_IsNumber(b_json)) {
            
            int x = cJSON_GetNumberValue(x_json);
            int y = cJSON_GetNumberValue(y_json);
            int r = cJSON_GetNumberValue(r_json);
            int g = cJSON_GetNumberValue(g_json);
            int b = cJSON_GetNumberValue(b_json);

            if (x >= 0 && x < LED_MATRIX_WIDTH && y >= 0 && y < LED_MATRIX_HEIGHT &&
                r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                
                led_color_t color = {r, g, b};
                led_matrix_set_pixel(x, y, color);
                point_count++;
            }
        }
    }

    cJSON_Delete(json);

    // 刷新显示
    esp_err_t ret = led_matrix_refresh();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Animation '%s' loaded successfully (%d points)", animation_name, point_count);
    }

    return ret;
}

// ==================== AGX系统监控接口实现 ====================

// AGX设备配置
#define AGX_IP_ADDRESS          "10.10.99.98"
#define AGX_METRICS_URL         "http://10.10.99.98:59100/metrics"
#define AGX_PING_TIMEOUT_MS     3000
#define AGX_HTTP_TIMEOUT_MS     10000
#define AGX_HTTP_BUFFER_SIZE    8192

/**
 * @brief HTTP事件处理器
 */
static esp_err_t agx_http_event_handler(esp_http_client_event_t *evt)
{
    char **response_buffer = (char **)evt->user_data;
    
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR - Connection or protocol error");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED - Successfully connected to server");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT - Request headers sent");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA - Received %d bytes", evt->data_len);
            if (*response_buffer == NULL) {
                *response_buffer = malloc(AGX_HTTP_BUFFER_SIZE);
                if (*response_buffer == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
                    return ESP_ERR_NO_MEM;
                }
                memset(*response_buffer, 0, AGX_HTTP_BUFFER_SIZE);
            }
            
            int current_len = strlen(*response_buffer);
            int remaining_space = AGX_HTTP_BUFFER_SIZE - current_len - 1;
            
            if (evt->data_len <= remaining_space) {
                strncat(*response_buffer, (char*)evt->data, evt->data_len);
            } else {
                ESP_LOGW(TAG, "Response buffer full, truncating data");
                strncat(*response_buffer, (char*)evt->data, remaining_space);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH - Request completed");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED - Connection closed");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT - Redirect response received");
            break;
    }
    return ESP_OK;
}

/**
 * @brief 解析metrics数据中的浮点数值
 */
static float parse_metrics_value(const char *metrics_data, const char *metric_name, const char *label_filter)
{
    char search_pattern[256];
    if (label_filter && strlen(label_filter) > 0) {
        snprintf(search_pattern, sizeof(search_pattern), "%s{%s}", metric_name, label_filter);
    } else {
        snprintf(search_pattern, sizeof(search_pattern), "%s ", metric_name);
    }
    
    char *line_start = strstr(metrics_data, search_pattern);
    if (line_start == NULL) {
        return -1.0f;
    }
    
    // 找到行末或者空格后的数值
    char *value_start = strchr(line_start, ' ');
    if (value_start == NULL) {
        return -1.0f;
    }
    value_start++; // 跳过空格
    
    return strtof(value_start, NULL);
}

/**
 * @brief 解析CPU使用率（计算多核平均值）
 */
static float parse_cpu_usage(const char *metrics_data)
{
    float total_idle = 0.0f;
    int core_count = 0;
    
    // 查找所有CPU核心的空闲值 (statistic="val")
    const char *search_pos = metrics_data;
    char search_pattern[] = "cpu_Hz{core=\"";
    
    while ((search_pos = strstr(search_pos, search_pattern)) != NULL) {
        // 检查是否是val统计 (空闲率)
        char *val_pos = strstr(search_pos, "statistic=\"val\"");
        if (val_pos != NULL && (val_pos - search_pos) < 100) { // 确保在同一行
            // 找到数值
            char *value_start = strchr(val_pos, ' ');
            if (value_start != NULL) {
                value_start++;
                float core_idle = strtof(value_start, NULL);
                // 确保空闲率在合理范围内 (0-100%)
                if (core_idle >= 0.0f && core_idle <= 100.0f) {
                    total_idle += core_idle;
                    core_count++;
                    ESP_LOGD(TAG, "Core %d idle: %.1f%%, usage: %.1f%%", 
                             core_count-1, core_idle, 100.0f - core_idle);
                }
            }
        }
        search_pos++;
    }
    
    if (core_count > 0) {
        float avg_idle = total_idle / core_count;
        float avg_usage = 100.0f - avg_idle;  // 使用率 = 100% - 空闲率
        ESP_LOGI(TAG, "Parsed CPU usage: %.1f%% (100%% - %.1f%% idle, average of %d cores)", 
                 avg_usage, avg_idle, core_count);
        return avg_usage;
    } else {
        ESP_LOGW(TAG, "No CPU usage data found in metrics");
        return -1.0f;
    }
}

esp_err_t agx_check_network_status(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Checking AGX network status...");
    
    ping_result_t ping_result = {0};
    esp_err_t ret = ethernet_ping(AGX_IP_ADDRESS, 1, AGX_PING_TIMEOUT_MS, &ping_result);
    
    if (ret == ESP_OK && ping_result.success && ping_result.packets_received > 0) {
        s_hardware_status.agx_monitor.network_status = AGX_NET_STATUS_UP;
        s_hardware_status.agx_monitor.last_ping_time_ms = ping_result.avg_time_ms;
        ESP_LOGI(TAG, "AGX network is UP (ping: %lu ms)", ping_result.avg_time_ms);
    } else {
        s_hardware_status.agx_monitor.network_status = AGX_NET_STATUS_DOWN;
        s_hardware_status.agx_monitor.network_error_count++;
        ESP_LOGW(TAG, "AGX network is DOWN");
    }
    
    s_hardware_status.agx_monitor.last_check_time = esp_timer_get_time() / 1000; // 转换为毫秒
    s_hardware_status.agx_monitor.check_count++;
    
    return ret;
}

esp_err_t agx_get_metrics(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查以太网状态
    ethernet_status_t eth_status = ethernet_get_status();
    ESP_LOGI(TAG, "Current ethernet status: %d", eth_status);
    
    if (eth_status < ETH_STATUS_GOT_IP) {
        ESP_LOGE(TAG, "Ethernet not ready (status %d), cannot get metrics", eth_status);
        printf("错误: 以太网未获取IP地址，当前状态: %d\n", eth_status);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Getting AGX metrics from %s...", AGX_METRICS_URL);
    
    char *response_buffer = NULL;
    esp_err_t ret = ESP_FAIL;

    esp_http_client_config_t config = {
        .url = AGX_METRICS_URL,
        .timeout_ms = 15000,  // 增加超时时间到15秒
        .event_handler = agx_http_event_handler,
        .user_data = &response_buffer,
        .buffer_size = AGX_HTTP_BUFFER_SIZE,
        .buffer_size_tx = 512,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .keep_alive_enable = false,
        .disable_auto_redirect = true,
    };

    ESP_LOGI(TAG, "Initializing HTTP client with config: timeout=%d, buffer=%d", 15000, AGX_HTTP_BUFFER_SIZE);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        printf("错误: 无法初始化HTTP客户端\n");
        return ESP_FAIL;
    }
    
    // 设置简单的HTTP头
    esp_http_client_set_header(client, "User-Agent", "ESP32S3");
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Connection", "close");
    
    ESP_LOGI(TAG, "Starting HTTP GET request to %s", AGX_METRICS_URL);
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int64_t content_length = esp_http_client_get_content_length(client);
        
        ESP_LOGI(TAG, "HTTP Response: Status=%d, Content-Length=%lld", status_code, content_length);
        
        if (status_code == 200 && response_buffer != NULL && strlen(response_buffer) > 0) {
            ESP_LOGI(TAG, "Successfully received metrics data (%d bytes)", strlen(response_buffer));
            
            // 解析metrics数据
            s_hardware_status.agx_monitor.metrics_available = true;
            
            // CPU使用率 - 解析所有核心的平均使用率 (100% - 空闲率)
            s_hardware_status.agx_monitor.cpu_usage_percent = parse_cpu_usage(response_buffer);
            
            // GPU频率 - 注意这是频率而不是使用率
            s_hardware_status.agx_monitor.gpu_usage_percent = 
                parse_metrics_value(response_buffer, "gpu_utilization_percentage_Hz", "nvidia_gpu=\"freq\",statistic=\"gpu\"");
            
            // 内存信息
            s_hardware_status.agx_monitor.memory_total_kb = 
                parse_metrics_value(response_buffer, "ram_kB", "statistic=\"total\"");
            s_hardware_status.agx_monitor.memory_used_kb = 
                parse_metrics_value(response_buffer, "ram_kB", "statistic=\"used\"");
            
            if (s_hardware_status.agx_monitor.memory_total_kb > 0) {
                s_hardware_status.agx_monitor.memory_usage_percent = 
                    (s_hardware_status.agx_monitor.memory_used_kb / s_hardware_status.agx_monitor.memory_total_kb) * 100.0f;
            }
            
            // 磁盘信息
            s_hardware_status.agx_monitor.disk_total_gb = 
                parse_metrics_value(response_buffer, "disk_GB", "mountpoint=\"total\"");
            s_hardware_status.agx_monitor.disk_used_gb = 
                parse_metrics_value(response_buffer, "disk_GB", "mountpoint=\"used\"");
            
            if (s_hardware_status.agx_monitor.disk_total_gb > 0) {
                s_hardware_status.agx_monitor.disk_usage_percent = 
                    (s_hardware_status.agx_monitor.disk_used_gb / s_hardware_status.agx_monitor.disk_total_gb) * 100.0f;
            }
            
            // 温度信息
            s_hardware_status.agx_monitor.temperature_cpu = 
                parse_metrics_value(response_buffer, "temperature_C", "statistic=\"cpu\"");
            s_hardware_status.agx_monitor.temperature_gpu = 
                parse_metrics_value(response_buffer, "temperature_C", "statistic=\"gpu\"");
            
            // 功耗信息
            s_hardware_status.agx_monitor.total_power_mw = 
                parse_metrics_value(response_buffer, "integrated_power_mW", "statistic=\"power\"");
            
            // 运行时间（保持为浮点数秒）
            s_hardware_status.agx_monitor.uptime_seconds = 
                parse_metrics_value(response_buffer, "uptime_s", "statistic=\"alive\"");
            
            ESP_LOGI(TAG, "AGX metrics updated successfully");
            ret = ESP_OK;
        } else if (status_code == 404) {
            ESP_LOGE(TAG, "Metrics endpoint not found (HTTP 404)");
            printf("错误: Metrics API端点不存在 (HTTP 404)\n");
            printf("检查: AGX设备上是否运行了metrics服务在59100端口\n");
            s_hardware_status.agx_monitor.metrics_available = false;
            s_hardware_status.agx_monitor.metrics_error_count++;
            ret = ESP_ERR_NOT_FOUND;
        } else if (status_code == 403 || status_code == 401) {
            ESP_LOGE(TAG, "Access denied to metrics endpoint (HTTP %d)", status_code);
            printf("错误: 访问metrics API被拒绝 (HTTP %d)\n", status_code);
            printf("检查: metrics服务是否需要认证或权限设置\n");
            s_hardware_status.agx_monitor.metrics_available = false;
            s_hardware_status.agx_monitor.metrics_error_count++;
            ret = ESP_ERR_NOT_ALLOWED;
        } else if (status_code >= 500) {
            ESP_LOGE(TAG, "Server error (HTTP %d)", status_code);
            printf("错误: AGX设备服务器错误 (HTTP %d)\n", status_code);
            printf("建议: 检查AGX设备上的metrics服务状态\n");
            s_hardware_status.agx_monitor.metrics_available = false;
            s_hardware_status.agx_monitor.metrics_error_count++;
            ret = ESP_FAIL;
        } else {
            ESP_LOGE(TAG, "HTTP GET failed with status: %d", status_code);
            printf("错误: HTTP请求失败，状态码: %d\n", status_code);
            if (response_buffer == NULL || strlen(response_buffer) == 0) {
                printf("提示: 服务器没有返回数据\n");
            }
            s_hardware_status.agx_monitor.metrics_available = false;
            s_hardware_status.agx_monitor.metrics_error_count++;
            ret = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        printf("错误: HTTP连接失败 - %s\n", esp_err_to_name(err));
        
        // 提供更具体的错误信息
        switch (err) {
            case ESP_ERR_HTTP_CONNECT:
                printf("原因: 无法连接到AGX设备的59100端口\n");
                printf("检查: 1) AGX设备是否开机 2) metrics服务是否运行 3) 防火墙设置\n");
                break;
            case ESP_ERR_TIMEOUT:
                printf("原因: 连接或响应超时\n");
                printf("检查: 网络连接质量和AGX设备响应速度\n");
                break;
            case ESP_ERR_HTTP_INVALID_TRANSPORT:
                printf("原因: 传输协议错误\n");
                printf("检查: URL格式是否正确\n");
                break;
            default:
                printf("建议: 使用 'agx diagnose' 命令进行详细诊断\n");
                break;
        }
        
        s_hardware_status.agx_monitor.metrics_available = false;
        s_hardware_status.agx_monitor.metrics_error_count++;
        ret = err;
    }
    
    esp_http_client_cleanup(client);
    
    if (response_buffer) {
        free(response_buffer);
    }
    
    return ret;
}

esp_err_t agx_monitor_check(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting AGX monitor check...");
    
    esp_err_t network_ret = agx_check_network_status();
    
    // 只有网络连通才尝试获取metrics
    if (s_hardware_status.agx_monitor.network_status == AGX_NET_STATUS_UP) {
        esp_err_t metrics_ret = agx_get_metrics();
        (void)metrics_ret; // 避免未使用变量警告
    } else {
        ESP_LOGW(TAG, "Network down, skipping metrics collection");
        s_hardware_status.agx_monitor.metrics_available = false;
    }
    
    // 只要网络检查成功就认为监控检查成功
    return network_ret;
}

esp_err_t agx_get_monitor_status(agx_monitor_status_t *monitor_status)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (monitor_status == NULL) {
        ESP_LOGE(TAG, "Invalid parameter: monitor_status is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(monitor_status, &s_hardware_status.agx_monitor, sizeof(agx_monitor_status_t));
    return ESP_OK;
}

esp_err_t agx_print_monitor_status(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    const agx_monitor_status_t *monitor = &s_hardware_status.agx_monitor;
    
    printf("\n=== AGX系统监控状态 ===\n");
    printf("网络状态: %s\n", agx_get_network_status_name(monitor->network_status));
    
    if (monitor->network_status == AGX_NET_STATUS_UP) {
        printf("Ping响应时间: %lu ms\n", monitor->last_ping_time_ms);
    }
    
    printf("Metrics可用: %s\n", monitor->metrics_available ? "是" : "否");
    printf("检查次数: %lu\n", monitor->check_count);
    printf("网络错误: %lu\n", monitor->network_error_count);
    printf("Metrics错误: %lu\n", monitor->metrics_error_count);
    
    if (monitor->last_check_time > 0) {
        printf("最后检查: %llu ms前\n", (esp_timer_get_time() / 1000) - monitor->last_check_time);
    }
    
    if (monitor->metrics_available) {
        printf("\n--- 系统信息 ---\n");
        if (monitor->cpu_usage_percent >= 0) {
            printf("CPU使用率: %.1f%%\n", monitor->cpu_usage_percent);
        }
        if (monitor->gpu_usage_percent >= 0) {
            // 将Hz转换为MHz显示GPU频率
            printf("GPU频率: %.0f MHz\n", monitor->gpu_usage_percent / 1000000.0f);
        }
        if (monitor->memory_total_kb > 0) {
            printf("内存使用: %.1f%% (%.1f/%.1f MB)\n", 
                   monitor->memory_usage_percent,
                   monitor->memory_used_kb / 1024.0f,
                   monitor->memory_total_kb / 1024.0f);
        }
        if (monitor->disk_total_gb > 0) {
            printf("磁盘使用: %.1f%% (%.1f/%.1f GB)\n",
                   monitor->disk_usage_percent,
                   monitor->disk_used_gb,
                   monitor->disk_total_gb);
        }
        if (monitor->temperature_cpu >= 0) {
            printf("CPU温度: %.1f°C\n", monitor->temperature_cpu);
        }
        if (monitor->temperature_gpu >= 0) {
            printf("GPU温度: %.1f°C\n", monitor->temperature_gpu);
        }
        if (monitor->total_power_mw > 0) {
            printf("总功耗: %.1f W\n", monitor->total_power_mw / 1000.0f);
        }
        if (monitor->uptime_seconds > 0) {
            // 精确显示运行时间到秒
            int total_seconds = (int)monitor->uptime_seconds;
            int hours = total_seconds / 3600;
            int minutes = (total_seconds % 3600) / 60;
            int seconds = total_seconds % 60;
            
            if (hours > 0) {
                printf("运行时间: %d小时%d分%d秒\n", hours, minutes, seconds);
            } else if (minutes > 0) {
                printf("运行时间: %d分%d秒\n", minutes, seconds);
            } else {
                printf("运行时间: %d秒\n", seconds);
            }
        }
    }
    
    printf("=====================\n\n");
    
    return ESP_OK;
}

const char* agx_get_network_status_name(agx_net_status_t status)
{
    switch (status) {
        case AGX_NET_STATUS_UNKNOWN: return "未知";
        case AGX_NET_STATUS_DOWN:    return "断开";
        case AGX_NET_STATUS_UP:      return "连通";
        case AGX_NET_STATUS_ERROR:   return "错误";
        default:                     return "无效";
    }
}

esp_err_t agx_test_port(uint16_t port)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Testing AGX port %d connection...", port);
    
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    
    // 设置连接超时
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(AGX_IP_ADDRESS);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    
    if (err == 0) {
        ESP_LOGI(TAG, "Port %d: Connection successful", port);
        
        // 对metrics端口(59100)进行HTTP测试
        if (port == 59100) {
            char http_request[] = 
                "GET /metrics HTTP/1.1\r\n"
                "Host: " AGX_IP_ADDRESS ":59100\r\n"
                "User-Agent: ESP32S3\r\n"
                "Accept: */*\r\n"
                "Connection: close\r\n"
                "\r\n";
            
            int sent = send(sock, http_request, strlen(http_request), 0);
            if (sent > 0) {
                ESP_LOGI(TAG, "HTTP request sent to metrics service");
                
                char response[256];
                int received = recv(sock, response, sizeof(response) - 1, 0);
                if (received > 0) {
                    response[received] = '\0';
                    ESP_LOGI(TAG, "Received HTTP response: %.50s...", response);
                    // 检查是否是有效的HTTP响应
                    if (strstr(response, "HTTP/1.1 200") || strstr(response, "HTTP/1.0 200")) {
                        ESP_LOGI(TAG, "Metrics service responded with HTTP 200 OK");
                    }
                }
            }
        }
        
        close(sock);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Port %d: Connection failed - errno %d", port, errno);
        close(sock);
        return ESP_FAIL;
    }
}

esp_err_t agx_diagnose_connection(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    printf("\n=== AGX状态诊断 ===\n");
    
    // 1. 网络连通性测试
    printf("1. 网络连通性测试...\n");
    ping_result_t ping_result = {0};
    esp_err_t ping_ret = ethernet_ping(AGX_IP_ADDRESS, 3, 2000, &ping_result);
    
    if (ping_ret == ESP_OK && ping_result.success && ping_result.packets_received > 0) {
        printf("   ✓ 网络连通 (avg: %lu ms, loss: %lu%%)\n", 
               ping_result.avg_time_ms,
               ((ping_result.packets_sent - ping_result.packets_received) * 100) / ping_result.packets_sent);
    } else {
        printf("   ✗ 网络不通 - 请检查AGX设备是否开机及网络连接\n");
        printf("================\n\n");
        return ESP_FAIL;
    }
    
    // 2. Metrics服务测试
    printf("2. Metrics服务测试...\n");
    esp_err_t metrics_ret = agx_test_port(59100);
    
    if (metrics_ret == ESP_OK) {
        printf("   ✓ Metrics服务端口59100可连接\n");
        
        // 尝试获取实际的metrics数据
        printf("3. Metrics数据获取测试...\n");
        esp_err_t get_ret = agx_get_metrics();
        if (get_ret == ESP_OK) {
            printf("   ✓ Metrics数据获取成功\n");
            printf("   AGX系统监控已就绪\n");
        } else {
            printf("   ✗ Metrics数据获取失败 - %s\n", esp_err_to_name(get_ret));
            printf("   建议：检查metrics数据格式或网络稳定性\n");
        }
    } else {
        printf("   ✗ Metrics服务端口59100无法连接\n");
        printf("   建议：检查AGX设备metrics服务是否运行\n");
    }
    
    // 3. 诊断总结
    printf("4. 诊断总结...\n");
    if (ping_ret == ESP_OK && metrics_ret == ESP_OK) {
        printf("   ✓ AGX设备状态监控功能正常\n");
        printf("   可以使用 'agx monitor' 或 'agx metrics' 命令获取监控数据\n");
    } else if (ping_ret == ESP_OK) {
        printf("   AGX设备网络正常，但metrics服务不可用\n");
        printf("   请检查AGX设备上的监控服务配置\n");
    } else {
        printf("   AGX设备网络不可达\n");
        printf("   请检查设备电源和网络连接\n");
    }
    
    printf("================\n\n");
    
    return ESP_OK;
}

// ==================== 色彩校正接口实现 ====================

esp_err_t color_correction_init_hardware(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = color_correction_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize color correction: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Color correction system initialized for hardware control");
    return ESP_OK;
}

esp_err_t apply_color_correction_to_led(led_color_t input_color, led_color_t *output_color)
{
    if (output_color == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    rgb_color_t corrected;
    esp_err_t ret = color_correction_apply(input_color.red, input_color.green, input_color.blue, &corrected);
    if (ret != ESP_OK) {
        // 如果校正失败，返回原始颜色
        *output_color = input_color;
        return ret;
    }

    output_color->red = corrected.r;
    output_color->green = corrected.g;
    output_color->blue = corrected.b;

    return ESP_OK;
}

esp_err_t led_set_pixel_with_correction(led_strip_handle_t strip, uint32_t index, 
                                        led_color_t color, uint8_t brightness)
{
    if (strip == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 应用色彩校正
    led_color_t corrected_color;
    esp_err_t ret = apply_color_correction_to_led(color, &corrected_color);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Color correction failed, using original color");
        corrected_color = color;
    }

    // 应用亮度调整
    uint8_t final_r = (corrected_color.red * brightness) / 100;
    uint8_t final_g = (corrected_color.green * brightness) / 100;
    uint8_t final_b = (corrected_color.blue * brightness) / 100;

    return led_strip_set_pixel(strip, index, final_r, final_g, final_b);
}

esp_err_t update_led_color_correction(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Updating LED color correction configuration...");

    // 重新应用当前的LED矩阵缓冲区（如果有内容的话）
    if (s_matrix_led_strip != NULL) {
        esp_err_t ret = led_matrix_apply_buffer();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update matrix color correction: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    // 重新应用当前的板载LED和触摸LED颜色
    if (s_board_led_strip != NULL) {
        led_color_t corrected_color;
        esp_err_t ret = apply_color_correction_to_led(s_hardware_status.board_led_color, &corrected_color);
        if (ret == ESP_OK) {
            apply_led_color(s_board_led_strip, corrected_color, 
                           s_hardware_status.board_led_brightness, BOARD_WS2812_NUM);
        }
    }

    if (s_touch_led_strip != NULL) {
        led_color_t corrected_color;
        esp_err_t ret = apply_color_correction_to_led(s_hardware_status.touch_led_color, &corrected_color);
        if (ret == ESP_OK) {
            apply_led_color(s_touch_led_strip, corrected_color, 
                           s_hardware_status.touch_led_brightness, TOUCH_WS2812_NUM);
        }
    }

    ESP_LOGI(TAG, "LED color correction updated successfully");
    return ESP_OK;
}

// ==================== 电源监控接口实现 ====================

esp_err_t power_monitor_init(void)
{
    if (s_power_monitor_initialized) {
        ESP_LOGW(TAG, "Power monitor already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing power monitor");

    // 初始化ADC
    esp_err_t ret = init_power_monitor_adc();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power monitor ADC: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化UART
    ret = init_power_chip_uart();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize power chip UART: %s", esp_err_to_name(ret));
        deinit_power_monitor_adc();
        return ret;
    }

    // 初始化电源数据结构
    memset(&s_hardware_status.power_chip_data, 0, sizeof(power_chip_data_t));
    memset(&s_hardware_status.voltage_data, 0, sizeof(voltage_monitor_data_t));

    s_power_monitor_initialized = true;
    ESP_LOGI(TAG, "Power monitor initialized successfully");

    // 启动监控任务
    power_monitor_start_task();

    return ESP_OK;
}

esp_err_t power_monitor_deinit(void)
{
    if (!s_power_monitor_initialized) {
        ESP_LOGW(TAG, "Power monitor not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing power monitor");

    // 停止监控任务
    power_monitor_stop_task();

    // 反初始化UART
    deinit_power_chip_uart();

    // 反初始化ADC
    deinit_power_monitor_adc();

    s_power_monitor_initialized = false;
    ESP_LOGI(TAG, "Power monitor deinitialized");

    return ESP_OK;
}

float power_get_supply_voltage(void)
{
    if (!s_power_monitor_initialized || s_adc2_handle == NULL) {
        ESP_LOGW(TAG, "Power monitor not initialized");
        return 0.0;
    }

    int raw_adc;
    int voltage_mv;

    // 读取ADC原始值 (GPIO18 -> ADC2_CHANNEL_7)
    esp_err_t ret = adc_oneshot_read(s_adc2_handle, ADC_CHANNEL_7, &raw_adc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read supply voltage ADC: %s", esp_err_to_name(ret));
        return 0.0;
    }

    // 校准到电压值
    if (s_adc2_cali_handle != NULL) {
        ret = adc_cali_raw_to_voltage(s_adc2_cali_handle, raw_adc, &voltage_mv);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calibrate supply voltage: %s", esp_err_to_name(ret));
            return 0.0;
        }
    } else {
        // 如果没有校准，使用默认的线性转换
        voltage_mv = (raw_adc * 3300) / 4095;
    }

    // 根据分压电路计算实际电压
    // 实际测试：ADC测量2.43V对应实际27.8V，分压比约为11.4
    // 调整分压比从4.0到11.4
    float actual_voltage = (voltage_mv / 1000.0) * 11.4;

    ESP_LOGD(TAG, "Supply voltage: raw=%d, mv=%d, actual=%.2fV", raw_adc, voltage_mv, actual_voltage);
    return actual_voltage;
}

esp_err_t power_chip_read_data(uint32_t timeout_ms)
{
    if (!s_power_uart_initialized) {
        ESP_LOGE(TAG, "Power chip UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting power chip data read with %lu ms timeout", timeout_ms);

    uint8_t data[256];
    size_t length = 0;

    // 清空接收缓冲区
    uart_flush_input(POWER_CHIP_UART_NUM);
    ESP_LOGI(TAG, "UART input buffer flushed");

    // 等待一小段时间让数据到达
    vTaskDelay(pdMS_TO_TICKS(100));

    // 检查是否有数据可读
    size_t buffered_size;
    uart_get_buffered_data_len(POWER_CHIP_UART_NUM, &buffered_size);
    ESP_LOGI(TAG, "Buffered data size before read: %d bytes", buffered_size);

    // 读取数据
    int len = uart_read_bytes(POWER_CHIP_UART_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(timeout_ms));
    ESP_LOGI(TAG, "UART read returned: %d", len);
    
    if (len > 0) {
        length = len;
        data[length] = '\0'; // 确保字符串结束

        ESP_LOGI(TAG, "Power chip UART received %d bytes", length);
        ESP_LOG_BUFFER_HEX(TAG, data, length);
        
        // 也以ASCII形式显示，以防是文本数据
        ESP_LOGI(TAG, "Data as ASCII: %.*s", length, data);

        // 解析数据
        power_chip_data_t parsed_data;
        esp_err_t ret = parse_power_chip_data(data, length, &parsed_data);
        if (ret == ESP_OK) {
            // 更新全局数据
            s_hardware_status.power_chip_data = parsed_data;
            ESP_LOGI(TAG, "Power chip data: %.2fV, %.3fA, %.2fW", 
                     parsed_data.voltage, parsed_data.current, parsed_data.power);
        } else {
            ESP_LOGW(TAG, "Failed to parse power chip data: %s", esp_err_to_name(ret));
        }
        return ret;
    } else if (len == 0) {
        ESP_LOGW(TAG, "Power chip UART read timeout - no data received");
        return ESP_ERR_TIMEOUT;
    } else {
        ESP_LOGE(TAG, "Power chip UART read error: %d", len);
        return ESP_FAIL;
    }
}

esp_err_t power_get_chip_data(power_chip_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_hardware_status.power_chip_data.valid) {
        return ESP_ERR_NOT_FOUND;
    }

    *data = s_hardware_status.power_chip_data;
    return ESP_OK;
}

esp_err_t power_get_voltage_data(voltage_monitor_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 实时读取电压值
    data->supply_voltage = power_get_supply_voltage();
    data->timestamp = esp_log_timestamp();

    // 更新全局状态
    s_hardware_status.voltage_data = *data;

    return ESP_OK;
}

esp_err_t power_monitor_start_task(void)
{
    if (s_power_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "Power monitor task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(power_monitor_task,
                                "power_monitor",
                                4096,
                                NULL,
                                5,
                                &s_power_monitor_task_handle);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create power monitor task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Power monitor task started");
    return ESP_OK;
}

esp_err_t power_monitor_stop_task(void)
{
    if (s_power_monitor_task_handle != NULL) {
        vTaskDelete(s_power_monitor_task_handle);
        s_power_monitor_task_handle = NULL;
        ESP_LOGI(TAG, "Power monitor task stopped");
    }
    return ESP_OK;
}

bool power_check_voltage_change(void)
{
    float current_supply_voltage = power_get_supply_voltage();

    bool voltage_changed = false;

    // 检查供电电压变化
    if (s_last_supply_voltage > 0 && 
        fabsf(current_supply_voltage - s_last_supply_voltage) > s_voltage_threshold) {
        ESP_LOGW(TAG, "🔋 电压变化触发! %.2fV -> %.2fV (阈值: %.2fV, 变化: %.2fV)",
                 s_last_supply_voltage, current_supply_voltage, s_voltage_threshold,
                 fabsf(current_supply_voltage - s_last_supply_voltage));
        printf("⚠️  电压变化检测: %.2fV -> %.2fV (超过阈值%.2fV)\n", 
               s_last_supply_voltage, current_supply_voltage, s_voltage_threshold);
        voltage_changed = true;
    }

    // 更新记录的电压值
    s_last_supply_voltage = current_supply_voltage;

    return voltage_changed;
}

esp_err_t power_set_voltage_threshold(float threshold)
{
    if (threshold <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_voltage_threshold = threshold;
    ESP_LOGI(TAG, "Voltage change threshold set to %.2fV", threshold);
    return ESP_OK;
}

esp_err_t power_print_status(void)
{
    printf("==================== 电源监控状态 ====================\n");
    printf("初始化状态: %s\n", s_power_monitor_initialized ? "已初始化" : "未初始化");
    printf("UART状态: %s\n", s_power_uart_initialized ? "已初始化" : "未初始化");
    printf("监控任务: %s\n", s_power_monitor_task_handle ? "运行中" : "已停止");
    printf("电压阈值: %.2fV\n", s_voltage_threshold);

    // 电压数据
    voltage_monitor_data_t voltage_data;
    esp_err_t ret = power_get_voltage_data(&voltage_data);
    if (ret == ESP_OK) {
        printf("供电电压 (GPIO18): %.2fV\n", voltage_data.supply_voltage);
    } else {
        printf("供电电压: 读取失败\n");
    }

    // 电源芯片数据
    if (s_hardware_status.power_chip_data.valid) {
        uint32_t data_age_ms = esp_log_timestamp() - s_hardware_status.power_chip_data.timestamp;
        printf("电源芯片数据 (数据年龄: %lu 毫秒):\n", data_age_ms);
        printf("  电压: %.2fV\n", s_hardware_status.power_chip_data.voltage);
        printf("  电流: %.3fA\n", s_hardware_status.power_chip_data.current);
        printf("  功率: %.2fW\n", s_hardware_status.power_chip_data.power);
    } else {
        printf("电源芯片数据: 正在读取...\n");
        // 自动尝试读取电源芯片数据
        esp_err_t ret = power_chip_read_data(2000); // 2秒超时
        if (ret == ESP_OK && s_hardware_status.power_chip_data.valid) {
            printf("电源芯片数据 (刚刚读取):\n");
            printf("  电压: %.2fV\n", s_hardware_status.power_chip_data.voltage);
            printf("  电流: %.3fA\n", s_hardware_status.power_chip_data.current);
            printf("  功率: %.2fW\n", s_hardware_status.power_chip_data.power);
        } else {
            printf("电源芯片数据: 读取失败 (%s)\n", esp_err_to_name(ret));
        }
    }

    printf("================================================\n");
    return ESP_OK;
}

bool power_get_uart_status(void)
{
    return s_power_uart_initialized;
}

// ==================== 电源监控静态函数实现 ====================

static esp_err_t init_power_monitor_adc(void)
{
    esp_err_t ret;

    // 初始化ADC2 (用于GPIO18 - 供电电压监测)
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_2,
    };
    ret = adc_oneshot_new_unit(&init_config2, &s_adc2_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC2 initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置ADC2通道 (GPIO18 -> ADC2_CHANNEL_7) 
    adc_oneshot_chan_cfg_t chan_config2 = {
        .atten = ADC_ATTEN_DB_12,  // 0~3.3V
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc2_handle, ADC_CHANNEL_7, &chan_config2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC2 channel config failed: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc2_handle);
        s_adc2_handle = NULL;
        return ret;
    }

    // ADC2校准
    adc_cali_curve_fitting_config_t cali_config2 = {
        .unit_id = ADC_UNIT_2,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config2, &s_adc2_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADC2 calibration failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "ADC2 calibration successful");
    }

    ESP_LOGI(TAG, "Power monitor ADC initialized successfully");
    return ESP_OK;
}

static esp_err_t init_power_chip_uart(void)
{
    if (s_power_uart_initialized) {
        ESP_LOGW(TAG, "Power chip UART already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing power chip UART - RX Pin: GPIO%d", POWER_CHIP_UART_RX_PIN);

    uart_config_t uart_config = {
        .baud_rate = POWER_CHIP_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 配置UART参数
    esp_err_t ret = uart_param_config(POWER_CHIP_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置UART引脚 (只需要RX引脚，TX设置为UART_PIN_NO_CHANGE)
    ret = uart_set_pin(POWER_CHIP_UART_NUM, UART_PIN_NO_CHANGE, POWER_CHIP_UART_RX_PIN, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 安装UART驱动
    ret = uart_driver_install(POWER_CHIP_UART_NUM, POWER_CHIP_UART_BUF_SIZE, 
                             POWER_CHIP_UART_BUF_SIZE, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_power_uart_initialized = true;
    ESP_LOGI(TAG, "Power chip UART initialized successfully");

    return ESP_OK;
}

static esp_err_t deinit_power_monitor_adc(void)
{
    if (s_adc2_cali_handle) {
        adc_cali_delete_scheme_curve_fitting(s_adc2_cali_handle);
        s_adc2_cali_handle = NULL;
    }

    if (s_adc2_handle) {
        adc_oneshot_del_unit(s_adc2_handle);
        s_adc2_handle = NULL;
    }

    ESP_LOGI(TAG, "Power monitor ADC deinitialized");
    return ESP_OK;
}

static esp_err_t deinit_power_chip_uart(void)
{
    if (s_power_uart_initialized) {
        uart_driver_delete(POWER_CHIP_UART_NUM);
        s_power_uart_initialized = false;
        ESP_LOGI(TAG, "Power chip UART deinitialized");
    }
    return ESP_OK;
}

static void power_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Power monitor task started");

    // 延迟启动，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1) {
        // 检查电压变化
        if (power_check_voltage_change()) {
            ESP_LOGI(TAG, "🔋 电压变化触发电源芯片数据读取");
            printf("📊 后台监控: 检测到电压变化，正在读取电源芯片数据...\n");
            
            // 尝试读取电源芯片数据
            esp_err_t ret = power_chip_read_data(1000); // 1秒超时
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "✅ 电源芯片数据更新成功");
                printf("✅ 电源数据已更新: %.2fV, %.3fA, %.2fW\n", 
                       s_hardware_status.power_chip_data.voltage,
                       s_hardware_status.power_chip_data.current,
                       s_hardware_status.power_chip_data.power);
            } else if (ret == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "⏱️  电源芯片数据读取超时");
                printf("⏱️  电源芯片数据读取超时\n");
            } else {
                ESP_LOGE(TAG, "❌ 电源芯片数据读取失败: %s", esp_err_to_name(ret));
                printf("❌ 电源芯片数据读取失败: %s\n", esp_err_to_name(ret));
            }
        }

        // 更新电压监控数据
        voltage_monitor_data_t voltage_data;
        power_get_voltage_data(&voltage_data);

        // 等待下一次检查
        vTaskDelay(pdMS_TO_TICKS(VOLTAGE_MONITOR_INTERVAL_MS));
    }
}

static esp_err_t parse_power_chip_data(const uint8_t *raw_data, size_t data_len, power_chip_data_t *data)
{
    if (raw_data == NULL || data == NULL || data_len < 4) {
        return ESP_ERR_INVALID_ARG;
    }

    // 诊断：检查接收到的数据类型
    bool all_zero = true;
    bool all_same = true;
    uint8_t first_byte = raw_data[0];
    
    for (size_t i = 0; i < data_len; i++) {
        if (raw_data[i] != 0x00) {
            all_zero = false;
        }
        if (raw_data[i] != first_byte) {
            all_same = false;
        }
    }
    
    if (all_zero) {
        ESP_LOGW(TAG, "接收到全零数据(%zu bytes) - 可能硬件未连接或芯片未工作", data_len);
        return ESP_ERR_INVALID_RESPONSE;
    } else if (all_same) {
        ESP_LOGW(TAG, "接收到重复数据: 0x%02X (%zu bytes) - 可能硬件故障", first_byte, data_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 实际数据格式: [0xFF帧头][电压][电流][CRC]
    // 例如: 0xFF 0x1C 0x32 0x??
    
    // 查找帧头0xFF
    int packet_start = -1;
    for (int i = 0; i <= (int)data_len - 4; i++) {
        if (raw_data[i] == 0xFF && i + 3 < (int)data_len) {
            packet_start = i;
            break;
        }
    }

    if (packet_start == -1 || (packet_start + 4) > (int)data_len) {
        ESP_LOGW(TAG, "未找到有效数据包(0xFF帧头)");
        ESP_LOGW(TAG, "数据样本: 0x%02X 0x%02X 0x%02X 0x%02X...", 
                 data_len > 0 ? raw_data[0] : 0,
                 data_len > 1 ? raw_data[1] : 0,
                 data_len > 2 ? raw_data[2] : 0,
                 data_len > 3 ? raw_data[3] : 0);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 提取数据包 [0xFF][电压][电流][CRC]
    // 例如: 0xFF 0x1C 0x32 0x??
    uint8_t frame_header = raw_data[packet_start];      // 0xFF (帧头)
    uint8_t voltage_raw = raw_data[packet_start + 1];   // 0x1C (电压数据)
    uint8_t current_raw = raw_data[packet_start + 2];   // 0x32 (电流数据)
    uint8_t crc_received = raw_data[packet_start + 3];  // CRC校验

    ESP_LOGI(TAG, "解析数据包: 帧头=0x%02X, 电压=0x%02X, 电流=0x%02X, CRC=0x%02X", 
             frame_header, voltage_raw, current_raw, crc_received);

    // 验证CRC (可选，如果需要严格校验)
    uint8_t crc_calculated = calculate_crc8(&raw_data[packet_start], 3);
    if (crc_calculated != crc_received) {
        ESP_LOGD(TAG, "CRC不匹配: 计算=0x%02X, 接收=0x%02X (忽略，继续解析)", crc_calculated, crc_received);
        // 注意：CRC不匹配但数据可能仍然有效，继续解析
    }

    // 转换数据 (根据技术文档调整)
    // 0x1C(28) → 28V, 0x32(50) → 5A
    // 电压: 直接转换 (1:1)
    // 电流: 除以10
    float voltage = (float)voltage_raw;           // 直接使用原始值作为电压
    float current = (float)current_raw / 10.0;    // 原始值除以10得到电流
    float power = voltage * current;

    // 填充数据结构
    data->valid = true;
    data->voltage = voltage;
    data->current = current; 
    data->power = power;
    data->timestamp = esp_log_timestamp();

    ESP_LOGI(TAG, "解析电源数据: %.2fV, %.3fA, %.2fW (原始: 电压=0x%02X, 电流=0x%02X, CRC=0x%02X)",
             voltage, current, power, voltage_raw, current_raw, crc_received);

    return ESP_OK;
}

// CRC8校验函数 - 使用Maxim/Dallas算法（多项式0x31）
static uint8_t calculate_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0x00;  // Maxim算法初始值为0x00
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;  // Maxim/Dallas多项式
            } else {
                crc <<= 1;
            }
        }
    }
    
    return crc;
}

// 新增：UART原始数据分析函数
esp_err_t power_analyze_uart_data(uint32_t timeout_ms)
{
    if (!s_power_uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "==================== UART数据分析 ====================");
    ESP_LOGI(TAG, "监控UART数据%lu毫秒，分析协议类型...", timeout_ms);
    
    uint8_t *buffer = malloc(1024);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    size_t total_bytes = 0;
    uint32_t start_time = esp_log_timestamp();
    uint8_t byte_frequency[256] = {0}; // 统计每个字节值的出现频率
    
    while ((esp_log_timestamp() - start_time) < timeout_ms) {
        int length = uart_read_bytes(UART_NUM_1, buffer, 128, pdMS_TO_TICKS(100));
        if (length > 0) {
            total_bytes += length;
            
            // 统计字节频率
            for (int i = 0; i < length; i++) {
                byte_frequency[buffer[i]]++;
            }
            
            // 显示最新数据
            ESP_LOGI(TAG, "接收 %d bytes:", length);
            for (int i = 0; i < length && i < 32; i++) {
                printf("0x%02X ", buffer[i]);
                if ((i + 1) % 16 == 0) printf("\n");
            }
            if (length > 32) printf("... (%d bytes total)\n", length);
            else printf("\n");
        }
    }
    
    ESP_LOGI(TAG, "==================== 分析结果 ====================");
    ESP_LOGI(TAG, "总接收字节数: %zu", total_bytes);
    
    if (total_bytes == 0) {
        ESP_LOGW(TAG, "未接收到任何数据 - 可能硬件未连接");
        free(buffer);
        return ESP_ERR_TIMEOUT;
    }
    
    // 分析数据模式
    int non_zero_bytes = 0;
    int different_values = 0;
    
    for (int i = 0; i < 256; i++) {
        if (byte_frequency[i] > 0) {
            if (i != 0) non_zero_bytes += byte_frequency[i];
            different_values++;
        }
    }
    
    ESP_LOGI(TAG, "数据多样性: %d种不同字节值", different_values);
    ESP_LOGI(TAG, "非零字节: %d/%zu (%.1f%%)", non_zero_bytes, total_bytes, 
             total_bytes > 0 ? (non_zero_bytes * 100.0 / total_bytes) : 0);
    
    // 检查常见协议特征
    if (byte_frequency[0xFF] > 0) {
        ESP_LOGI(TAG, "发现0xFF包头 %d次 - 可能是XSP16或类似协议", byte_frequency[0xFF]);
    }
    if (byte_frequency[0xAA] > 0) {
        ESP_LOGI(TAG, "发现0xAA %d次 - 可能是其他协议同步字节", byte_frequency[0xAA]);
    }
    if (byte_frequency[0x55] > 0) {
        ESP_LOGI(TAG, "发现0x55 %d次 - 可能是其他协议同步字节", byte_frequency[0x55]);
    }
    
    // 推断协议类型
    if (different_values == 1 && byte_frequency[0x00] == total_bytes) {
        ESP_LOGW(TAG, "结论: 全零数据 - 硬件可能未连接或芯片未工作");
    } else if (different_values < 5) {
        ESP_LOGW(TAG, "结论: 数据变化很少 - 可能硬件故障或信号问题");
    } else if (byte_frequency[0xFF] > 0) {
        ESP_LOGI(TAG, "结论: 可能是XSP16或类似的有包头协议");
    } else {
        ESP_LOGI(TAG, "结论: 未知协议格式，需要进一步分析");
    }
    
    ESP_LOGI(TAG, "================================================");
    
    free(buffer);
    return ESP_OK;
}
