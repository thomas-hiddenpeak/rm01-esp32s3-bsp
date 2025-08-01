/**
 * @file hardware_control.c
 * @brief ESP32S3 硬件控制组件实现
 */

#include "hardware_control.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "HARDWARE_CONTROL";

// ==================== 静态变量 ====================

static bool s_initialized = false;
static hardware_status_t s_hardware_status = {0};
static led_strip_handle_t s_board_led_strip = NULL;
static led_strip_handle_t s_touch_led_strip = NULL;

// ==================== 静态函数声明 ====================

static esp_err_t init_fan_pwm(void);
static esp_err_t init_ws2812(void);
static esp_err_t init_usb_mux_gpio(void);
static esp_err_t init_power_control_gpio(void);
static esp_err_t apply_led_color(led_strip_handle_t strip, led_color_t color, uint8_t brightness, uint8_t num_leds);
static void hsv_to_rgb(int hue, int saturation, int value, uint8_t *r, uint8_t *g, uint8_t *b);

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
    s_hardware_status.orin_power_state = POWER_STATE_UNKNOWN;
    s_hardware_status.n305_power_state = POWER_STATE_UNKNOWN;

esp_err_t orin_power_off(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = gpio_set_output(ORIN_POWER_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to power off Orin: %s", esp_err_to_name(ret));
        return ret;
    }

    s_hardware_status.orin_power_state = POWER_STATE_OFF;
    ESP_LOGI(TAG, "Orin powered off (GPIO%d set to HIGH)", ORIN_POWER_PIN);
    return ESP_OK;
}

esp_err_t orin_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting Orin device");
    
    // 拉高重启引脚
    esp_err_t ret = gpio_set_output(ORIN_RESET_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin reset pin high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持1000ms
    vTaskDelay(pdMS_TO_TICKS(ORIN_RESET_PULSE_MS));

    // 拉低重启引脚
    ret = gpio_set_output(ORIN_RESET_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin reset pin low: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Orin reset completed");
    return ESP_OK;
}

esp_err_t orin_enter_recovery_mode(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Entering Orin recovery mode");
    
    // 步骤1: 将GPIO40拉高
    esp_err_t ret = gpio_set_output(ORIN_RECOVERY_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin recovery pin high: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Orin recovery pin set high");

    // 步骤2: 重启Orin
    ret = orin_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset Orin during recovery mode entry");
        return ret;
    }

    // 步骤3: 将GPIO40拉低
    ret = gpio_set_output(ORIN_RECOVERY_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin recovery pin low: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Orin recovery pin set low");

    // 步骤4: 切换USB MUX到AGX
    ret = usb_mux_set_target(USB_MUX_AGX);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch USB MUX to AGX during recovery mode");
        return ret;
    }

    ESP_LOGI(TAG, "Orin recovery mode entry completed");
    return ESP_OK;
}

esp_err_t n305_power_toggle(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Toggling N305 power");
    
    // 拉高电源按钮引脚
    esp_err_t ret = gpio_set_output(N305_POWER_BTN_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 power button high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持300ms
    vTaskDelay(pdMS_TO_TICKS(N305_POWER_PULSE_MS));

    // 拉低电源按钮引脚
    ret = gpio_set_output(N305_POWER_BTN_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 power button low: %s", esp_err_to_name(ret));
        return ret;
    }

    // 切换电源状态
    if (s_hardware_status.n305_power_state == POWER_STATE_ON) {
        s_hardware_status.n305_power_state = POWER_STATE_OFF;
        ESP_LOGI(TAG, "N305 power toggled to OFF");
    } else {
        s_hardware_status.n305_power_state = POWER_STATE_ON;
        ESP_LOGI(TAG, "N305 power toggled to ON");
    }

    return ESP_OK;
}

esp_err_t n305_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Resetting N305 device");
    
    // 拉高重启引脚
    esp_err_t ret = gpio_set_output(N305_RESET_PIN, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 reset pin high: %s", esp_err_to_name(ret));
        return ret;
    }

    // 保持300ms
    vTaskDelay(pdMS_TO_TICKS(N305_RESET_PULSE_MS));

    // 拉低重启引脚
    ret = gpio_set_output(N305_RESET_PIN, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 reset pin low: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "N305 reset completed");
    return ESP_OK;
}

esp_err_t orin_get_power_state(power_state_t *state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_hardware_status.orin_power_state;
    return ESP_OK;
}

esp_err_t n305_get_power_state(power_state_t *state)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (state == NULL) {
        ESP_LOGE(TAG, "State pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    *state = s_hardware_status.n305_power_state;
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

// ==================== 测试接口实现 ====================h_led_strip = NULL;

// ==================== 静态函数声明 ====================

static esp_err_t init_fan_pwm(void);
static esp_err_t init_ws2812(void);
static esp_err_t init_usb_mux_gpio(void);
static esp_err_t init_power_control_gpio(void);
static esp_err_t apply_led_color(led_strip_handle_t strip, led_color_t color, uint8_t brightness, uint8_t num_leds);
static void hsv_to_rgb(int hue, int saturation, int value, uint8_t *r, uint8_t *g, uint8_t *b);

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
    s_hardware_status.orin_power_state = POWER_STATE_UNKNOWN;
    s_hardware_status.n305_power_state = POWER_STATE_UNKNOWN;

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

    s_initialized = true;
    s_hardware_status.initialized = true;
    
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
    
    esp_err_t ret = apply_led_color(s_board_led_strip, color, 
                                   s_hardware_status.board_led_brightness, 
                                   BOARD_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set board LED color: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Board LED color set to R:%d G:%d B:%d", color.red, color.green, color.blue);
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
    
    esp_err_t ret = apply_led_color(s_touch_led_strip, color, 
                                   s_hardware_status.touch_led_brightness, 
                                   TOUCH_WS2812_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set touch LED color: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Touch LED color set to R:%d G:%d B:%d", color.red, color.green, color.blue);
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

    esp_err_t ret = gpio_set_direction(pin, GPIO_MODE_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set GPIO%d as input: %s", pin, esp_err_to_name(ret));
        return ret;
    }

    int level = gpio_get_level(pin);
    *state = (level == 0) ? GPIO_STATE_LOW : GPIO_STATE_HIGH;

    ESP_LOGI(TAG, "GPIO%d state: %s", pin, *state ? "HIGH" : "LOW");
    return ESP_OK;
}

esp_err_t gpio_toggle_output(uint8_t pin)
{
    gpio_state_t current_state;
    esp_err_t ret = gpio_read_input(pin, &current_state);
    if (ret != ESP_OK) {
        return ret;
    }

    gpio_state_t new_state = (current_state == GPIO_STATE_LOW) ? GPIO_STATE_HIGH : GPIO_STATE_LOW;
    return gpio_set_output(pin, new_state);
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
        case USB_MUX_N305:     // mux1=1, mux2=1
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
        case USB_MUX_N305:
            return "N305";
        default:
            return "Unknown";
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
    ESP_LOGI(TAG, "Starting GPIO%d test", pin);
    
    // Test output mode
    ESP_LOGI(TAG, "Testing GPIO%d output mode", pin);
    esp_err_t ret = gpio_set_output(pin, GPIO_STATE_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d output test failed", pin);
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ret = gpio_set_output(pin, GPIO_STATE_LOW);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d output test failed", pin);
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Test input mode
    ESP_LOGI(TAG, "Testing GPIO%d input mode", pin);
    gpio_state_t state;
    ret = gpio_read_input(pin, &state);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d input test failed", pin);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GPIO%d test completed successfully", pin);
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

esp_err_t hardware_test_orin_power(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting Orin power control test");
    
    // 测试开机
    ESP_LOGI(TAG, "Testing Orin power on");
    esp_err_t ret = orin_power_on();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Orin power on test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 测试关机
    ESP_LOGI(TAG, "Testing Orin power off");
    ret = orin_power_off();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Orin power off test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    ESP_LOGI(TAG, "Orin power control test completed successfully");
    return ESP_OK;
}

esp_err_t hardware_test_n305_power(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Hardware control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting N305 power control test");
    
    // 测试电源切换
    ESP_LOGI(TAG, "Testing N305 power toggle");
    esp_err_t ret = n305_power_toggle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "N305 power toggle test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 再次切换
    ESP_LOGI(TAG, "Testing N305 power toggle again");
    ret = n305_power_toggle();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "N305 power toggle test failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "N305 power control test completed successfully");
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
    printf("Orin电源状态: %s\n", power_state_get_name(s_hardware_status.orin_power_state));
    printf("N305电源状态: %s\n", power_state_get_name(s_hardware_status.n305_power_state));
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

    // Clear both LED strips
    ESP_ERROR_CHECK(led_strip_clear(s_board_led_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_touch_led_strip));
    
    ESP_LOGI(TAG, "WS2812 initialized - Board: GPIO%d (%d LEDs), Touch: GPIO%d (%d LEDs)", 
             BOARD_WS2812_PIN, BOARD_WS2812_NUM, TOUCH_WS2812_PIN, TOUCH_WS2812_NUM);
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

    // 配置Orin电源控制引脚
    ret = gpio_set_direction(ORIN_POWER_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Orin power GPIO%d as output: %s", 
                 ORIN_POWER_PIN, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_direction(ORIN_RESET_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Orin reset GPIO%d as output: %s", 
                 ORIN_RESET_PIN, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_direction(ORIN_RECOVERY_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure Orin recovery GPIO%d as output: %s", 
                 ORIN_RECOVERY_PIN, esp_err_to_name(ret));
        return ret;
    }

    // 配置N305电源控制引脚
    ret = gpio_set_direction(N305_POWER_BTN_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure N305 power button GPIO%d as output: %s", 
                 N305_POWER_BTN_PIN, esp_err_to_name(ret));
        return ret;
    }

    ret = gpio_set_direction(N305_RESET_PIN, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure N305 reset GPIO%d as output: %s", 
                 N305_RESET_PIN, esp_err_to_name(ret));
        return ret;
    }

    // 设置初始状态
    // Orin默认开机状态 (GPIO3 = LOW)
    ret = gpio_set_level(ORIN_POWER_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin power pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // Orin重启引脚默认为低
    ret = gpio_set_level(ORIN_RESET_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin reset pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // Orin恢复模式引脚默认为低
    ret = gpio_set_level(ORIN_RECOVERY_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Orin recovery pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // N305电源按钮默认为低
    ret = gpio_set_level(N305_POWER_BTN_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 power button initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // N305重启引脚默认为低
    ret = gpio_set_level(N305_RESET_PIN, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set N305 reset pin initial level: %s", esp_err_to_name(ret));
        return ret;
    }

    // 更新状态（默认Orin开机状态）
    s_hardware_status.orin_power_state = POWER_STATE_ON;
    s_hardware_status.n305_power_state = POWER_STATE_UNKNOWN;

    ESP_LOGI(TAG, "Power control GPIO initialized");
    ESP_LOGI(TAG, "Orin - Power: GPIO%d, Reset: GPIO%d, Recovery: GPIO%d", 
             ORIN_POWER_PIN, ORIN_RESET_PIN, ORIN_RECOVERY_PIN);
    ESP_LOGI(TAG, "N305 - Power: GPIO%d, Reset: GPIO%d", 
             N305_POWER_BTN_PIN, N305_RESET_PIN);
    
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
