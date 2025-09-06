#include "color_correction.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "COLOR_CORRECTION";

// NVS配置键名
#define NVS_NAMESPACE "color_correct"
#define NVS_KEY_CONFIG "config"

// 全局配置变量
static color_correction_config_t s_config;
static bool s_initialized = false;

// 内部函数声明
static rgb_color_t hsl_to_rgb(float h, float s, float l);
static hsl_color_t rgb_to_hsl(uint8_t r, uint8_t g, uint8_t b);
static uint8_t gamma_correction(uint8_t value, float gamma);
static void init_default_config(void);

esp_err_t color_correction_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Color correction already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing color correction system");
    
    // 初始化默认配置
    init_default_config();
    
    // 尝试从NVS加载配置
    esp_err_t ret = color_correction_load_config();
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
        ret = ESP_OK;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s", esp_err_to_name(ret));
        init_default_config(); // 回退到默认配置
        ret = ESP_OK;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Color correction initialized successfully");
    
    return ret;
}

esp_err_t color_correction_set_config(const color_correction_config_t *config)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(color_correction_config_t));
    ESP_LOGI(TAG, "Color correction config updated");
    
    return ESP_OK;
}

esp_err_t color_correction_get_config(color_correction_config_t *config)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(config, &s_config, sizeof(color_correction_config_t));
    return ESP_OK;
}

esp_err_t color_correction_apply(uint8_t input_r, uint8_t input_g, uint8_t input_b, rgb_color_t *output)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (output == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 如果色彩校正被禁用，直接返回原始值
    if (!s_config.enable_correction) {
        output->r = input_r;
        output->g = input_g;
        output->b = input_b;
        return ESP_OK;
    }

    // 步骤1: 基础色彩校正（基于您提供的算法）
    const rgb_color_t black = {0, 0, 0};
    
    // 计算斜率和截距
    const float r_slope = (float)(s_config.max_white.r - s_config.min_white.r) / 
                         (s_config.input_max - s_config.input_min);
    const float g_slope = (float)(s_config.max_white.g - s_config.min_white.g) / 
                         (s_config.input_max - s_config.input_min);
    const float b_slope = (float)(s_config.max_white.b - s_config.min_white.b) / 
                         (s_config.input_max - s_config.input_min);
    
    const float r_intercept = s_config.min_white.r - r_slope * s_config.input_min;
    const float g_intercept = s_config.min_white.g - g_slope * s_config.input_min;
    const float b_intercept = s_config.min_white.b - b_slope * s_config.input_min;

    float temp_r, temp_g, temp_b;

    // 低亮度处理
    if (input_r <= 5 && input_g <= 5 && input_b <= 5) {
        temp_r = (float)input_r * (s_config.min_white.r / s_config.input_min);
        temp_g = (float)input_g * (s_config.min_white.g / s_config.input_min);
        temp_b = (float)input_b * (s_config.min_white.b / s_config.input_min);
    } else {
        temp_r = (float)input_r * r_slope + r_intercept;
        temp_g = (float)input_g * g_slope + g_intercept;
        temp_b = (float)input_b * b_slope + b_intercept;
    }

    // 限制范围
    temp_r = (temp_r < black.r) ? black.r : (temp_r > s_config.max_white.r) ? s_config.max_white.r : temp_r;
    temp_g = (temp_g < black.g) ? black.g : (temp_g > s_config.max_white.g) ? s_config.max_white.g : temp_g;
    temp_b = (temp_b < black.b) ? black.b : (temp_b > s_config.max_white.b) ? s_config.max_white.b : temp_b;

    // 步骤2: HSL增强处理
    uint8_t adjusted_r = (uint8_t)(temp_r + 0.5f);
    uint8_t adjusted_g = (uint8_t)(temp_g + 0.5f);
    uint8_t adjusted_b = (uint8_t)(temp_b + 0.5f);

    // RGB转HSL
    hsl_color_t hsl = rgb_to_hsl(adjusted_r, adjusted_g, adjusted_b);

    // 亮度增强
    hsl.l *= s_config.brightness_boost;
    hsl.l = (hsl.l > 1.0f) ? 1.0f : hsl.l;

    // 饱和度增强
    hsl.s *= s_config.saturation_boost;
    hsl.s = (hsl.s > 1.0f) ? 1.0f : hsl.s;

    // HSL转回RGB
    rgb_color_t enhanced = hsl_to_rgb(hsl.h, hsl.s, hsl.l);

    // 步骤3: 伽马校正
    output->r = gamma_correction(enhanced.r, s_config.gamma_correction);
    output->g = gamma_correction(enhanced.g, s_config.gamma_correction);
    output->b = gamma_correction(enhanced.b, s_config.gamma_correction);

    return ESP_OK;
}

esp_err_t color_correction_set_white_point(const white_point_t *white_point)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (white_point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config.white_point = *white_point;
    ESP_LOGI(TAG, "White point set to R:%d G:%d B:%d", 
             white_point->r, white_point->g, white_point->b);
    
    return ESP_OK;
}

esp_err_t color_correction_get_white_point(white_point_t *white_point)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (white_point == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *white_point = s_config.white_point;
    return ESP_OK;
}

esp_err_t color_correction_set_gamma(float gamma)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gamma <= 0.0f || gamma > 5.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config.gamma_correction = gamma;
    ESP_LOGI(TAG, "Gamma correction set to %.2f", gamma);
    
    return ESP_OK;
}

esp_err_t color_correction_get_gamma(float *gamma)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gamma == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *gamma = s_config.gamma_correction;
    return ESP_OK;
}

esp_err_t color_correction_set_brightness_boost(float brightness)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (brightness <= 0.0f || brightness > 3.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config.brightness_boost = brightness;
    ESP_LOGI(TAG, "Brightness boost set to %.2f", brightness);
    
    return ESP_OK;
}

esp_err_t color_correction_get_brightness_boost(float *brightness)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (brightness == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *brightness = s_config.brightness_boost;
    return ESP_OK;
}

esp_err_t color_correction_set_saturation_boost(float saturation)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (saturation <= 0.0f || saturation > 3.0f) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config.saturation_boost = saturation;
    ESP_LOGI(TAG, "Saturation boost set to %.2f", saturation);
    
    return ESP_OK;
}

esp_err_t color_correction_get_saturation_boost(float *saturation)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (saturation == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *saturation = s_config.saturation_boost;
    return ESP_OK;
}

esp_err_t color_correction_set_enable(bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_config.enable_correction = enable;
    ESP_LOGI(TAG, "Color correction %s", enable ? "enabled" : "disabled");
    
    return ESP_OK;
}

esp_err_t color_correction_get_enable(bool *enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (enable == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *enable = s_config.enable_correction;
    return ESP_OK;
}

esp_err_t color_correction_save_config(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &s_config, sizeof(color_correction_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(ret));
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Color correction config saved to NVS");
        } else {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t color_correction_load_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t required_size = sizeof(color_correction_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, &s_config, &required_size);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Color correction config loaded from NVS");
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "Config not found in NVS");
    } else {
        ESP_LOGE(TAG, "Failed to load config from NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);
    return ret;
}

esp_err_t color_correction_reset_config(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    init_default_config();
    ESP_LOGI(TAG, "Color correction config reset to defaults");
    
    return ESP_OK;
}

// 内部辅助函数实现

static void init_default_config(void)
{
    s_config.white_point.r = COLOR_CORRECTION_DEFAULT_WHITE_POINT_R;
    s_config.white_point.g = COLOR_CORRECTION_DEFAULT_WHITE_POINT_G;
    s_config.white_point.b = COLOR_CORRECTION_DEFAULT_WHITE_POINT_B;
    
    s_config.min_white.r = COLOR_CORRECTION_DEFAULT_MIN_WHITE_R;
    s_config.min_white.g = COLOR_CORRECTION_DEFAULT_MIN_WHITE_G;
    s_config.min_white.b = COLOR_CORRECTION_DEFAULT_MIN_WHITE_B;
    
    s_config.max_white.r = COLOR_CORRECTION_DEFAULT_MAX_WHITE_R;
    s_config.max_white.g = COLOR_CORRECTION_DEFAULT_MAX_WHITE_G;
    s_config.max_white.b = COLOR_CORRECTION_DEFAULT_MAX_WHITE_B;
    
    s_config.input_min = COLOR_CORRECTION_DEFAULT_INPUT_MIN;
    s_config.input_max = COLOR_CORRECTION_DEFAULT_INPUT_MAX;
    s_config.gamma_correction = COLOR_CORRECTION_DEFAULT_GAMMA;
    s_config.brightness_boost = COLOR_CORRECTION_DEFAULT_BRIGHTNESS;
    s_config.saturation_boost = COLOR_CORRECTION_DEFAULT_SATURATION;
    s_config.enable_correction = true;
    
    ESP_LOGD(TAG, "Default config initialized");
}

static hsl_color_t rgb_to_hsl(uint8_t r, uint8_t g, uint8_t b)
{
    hsl_color_t hsl;
    float r_norm = r / 255.0f;
    float g_norm = g / 255.0f;
    float b_norm = b / 255.0f;
    
    float max = fmaxf(fmaxf(r_norm, g_norm), b_norm);
    float min = fminf(fminf(r_norm, g_norm), b_norm);
    float delta = max - min;
    
    // 亮度
    hsl.l = (max + min) / 2.0f;
    
    if (delta == 0.0f) {
        // 灰度
        hsl.h = 0.0f;
        hsl.s = 0.0f;
    } else {
        // 饱和度
        hsl.s = (hsl.l > 0.5f) ? delta / (2.0f - max - min) : delta / (max + min);
        
        // 色相
        if (max == r_norm) {
            hsl.h = (g_norm - b_norm) / delta + (g_norm < b_norm ? 6.0f : 0.0f);
        } else if (max == g_norm) {
            hsl.h = (b_norm - r_norm) / delta + 2.0f;
        } else {
            hsl.h = (r_norm - g_norm) / delta + 4.0f;
        }
        hsl.h *= 60.0f;
    }
    
    return hsl;
}

static rgb_color_t hsl_to_rgb(float h, float s, float l)
{
    rgb_color_t rgb;
    
    if (s == 0.0f) {
        // 灰度
        rgb.r = rgb.g = rgb.b = (uint8_t)(l * 255);
    } else {
        float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c / 2.0f;
        
        float r_prime, g_prime, b_prime;
        
        if (h < 60.0f) {
            r_prime = c; g_prime = x; b_prime = 0;
        } else if (h < 120.0f) {
            r_prime = x; g_prime = c; b_prime = 0;
        } else if (h < 180.0f) {
            r_prime = 0; g_prime = c; b_prime = x;
        } else if (h < 240.0f) {
            r_prime = 0; g_prime = x; b_prime = c;
        } else if (h < 300.0f) {
            r_prime = x; g_prime = 0; b_prime = c;
        } else {
            r_prime = c; g_prime = 0; b_prime = x;
        }
        
        rgb.r = (uint8_t)((r_prime + m) * 255);
        rgb.g = (uint8_t)((g_prime + m) * 255);
        rgb.b = (uint8_t)((b_prime + m) * 255);
    }
    
    return rgb;
}

static uint8_t gamma_correction(uint8_t value, float gamma)
{
    float normalized = powf(value / 255.0f, 1.0f / gamma);
    return (uint8_t)(normalized * 255.0f);
}
