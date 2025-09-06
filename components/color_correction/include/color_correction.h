#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RGB颜色结构体
 */
typedef struct {
    uint8_t r;  ///< 红色分量 (0-255)
    uint8_t g;  ///< 绿色分量 (0-255)
    uint8_t b;  ///< 蓝色分量 (0-255)
} rgb_color_t;

/**
 * @brief HSL颜色结构体
 */
typedef struct {
    float h;    ///< 色相 (0-360)
    float s;    ///< 饱和度 (0-1)
    float l;    ///< 亮度 (0-1)
} hsl_color_t;

/**
 * @brief 白点校准参数结构体
 */
typedef struct {
    uint8_t r;  ///< 红色白点参考值
    uint8_t g;  ///< 绿色白点参考值  
    uint8_t b;  ///< 蓝色白点参考值
} white_point_t;

/**
 * @brief 色彩校正配置结构体
 */
typedef struct {
    white_point_t white_point;      ///< 白点校准参数
    rgb_color_t min_white;          ///< 最小白色值
    rgb_color_t max_white;          ///< 最大白色值
    float input_min;                ///< 输入最小值
    float input_max;                ///< 输入最大值
    float gamma_correction;         ///< 伽马校正值
    float brightness_boost;         ///< 亮度增强系数
    float saturation_boost;         ///< 饱和度增强系数
    bool enable_correction;         ///< 是否启用色彩校正
} color_correction_config_t;

// 默认校正参数
#define COLOR_CORRECTION_DEFAULT_WHITE_POINT_R  42
#define COLOR_CORRECTION_DEFAULT_WHITE_POINT_G  28  
#define COLOR_CORRECTION_DEFAULT_WHITE_POINT_B  19
#define COLOR_CORRECTION_DEFAULT_MIN_WHITE_R    5
#define COLOR_CORRECTION_DEFAULT_MIN_WHITE_G    4
#define COLOR_CORRECTION_DEFAULT_MIN_WHITE_B    3
#define COLOR_CORRECTION_DEFAULT_MAX_WHITE_R    168
#define COLOR_CORRECTION_DEFAULT_MAX_WHITE_G    112
#define COLOR_CORRECTION_DEFAULT_MAX_WHITE_B    76
#define COLOR_CORRECTION_DEFAULT_INPUT_MIN      5.0f
#define COLOR_CORRECTION_DEFAULT_INPUT_MAX      255.0f
#define COLOR_CORRECTION_DEFAULT_GAMMA          2.2f
#define COLOR_CORRECTION_DEFAULT_BRIGHTNESS     1.15f
#define COLOR_CORRECTION_DEFAULT_SATURATION     1.520875f

/**
 * @brief 初始化色彩校正系统
 * 
 * @return 
 *     - ESP_OK: 初始化成功
 *     - ESP_FAIL: 初始化失败
 */
esp_err_t color_correction_init(void);

/**
 * @brief 设置色彩校正配置
 * 
 * @param config 色彩校正配置参数
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 */
esp_err_t color_correction_set_config(const color_correction_config_t *config);

/**
 * @brief 获取色彩校正配置
 * 
 * @param config 输出的色彩校正配置参数
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_config(color_correction_config_t *config);

/**
 * @brief 应用色彩校正到RGB颜色
 * 
 * @param input_r 输入红色分量 (0-255)
 * @param input_g 输入绿色分量 (0-255)
 * @param input_b 输入蓝色分量 (0-255)
 * @param output 输出校正后的RGB颜色
 * @return 
 *     - ESP_OK: 校正成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_apply(uint8_t input_r, uint8_t input_g, uint8_t input_b, rgb_color_t *output);

/**
 * @brief 设置白点校准参数
 * 
 * @param white_point 白点校准参数
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_set_white_point(const white_point_t *white_point);

/**
 * @brief 获取白点校准参数
 * 
 * @param white_point 输出白点校准参数
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_white_point(white_point_t *white_point);

/**
 * @brief 设置伽马校正值
 * 
 * @param gamma 伽马校正值 (建议范围: 1.0-3.0)
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_set_gamma(float gamma);

/**
 * @brief 获取伽马校正值
 * 
 * @param gamma 输出伽马校正值
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_gamma(float *gamma);

/**
 * @brief 设置亮度增强系数
 * 
 * @param brightness 亮度增强系数 (建议范围: 0.5-2.0)
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_set_brightness_boost(float brightness);

/**
 * @brief 获取亮度增强系数
 * 
 * @param brightness 输出亮度增强系数
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_brightness_boost(float *brightness);

/**
 * @brief 设置饱和度增强系数
 * 
 * @param saturation 饱和度增强系数 (建议范围: 0.5-2.0)
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_set_saturation_boost(float saturation);

/**
 * @brief 获取饱和度增强系数
 * 
 * @param saturation 输出饱和度增强系数
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_saturation_boost(float *saturation);

/**
 * @brief 启用或禁用色彩校正
 * 
 * @param enable true-启用，false-禁用
 * @return 
 *     - ESP_OK: 设置成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_set_enable(bool enable);

/**
 * @brief 获取色彩校正启用状态
 * 
 * @param enable 输出启用状态
 * @return 
 *     - ESP_OK: 获取成功
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_get_enable(bool *enable);

/**
 * @brief 保存色彩校正配置到NVS
 * 
 * @return 
 *     - ESP_OK: 保存成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 *     - ESP_FAIL: 保存失败
 */
esp_err_t color_correction_save_config(void);

/**
 * @brief 从NVS加载色彩校正配置
 * 
 * @return 
 *     - ESP_OK: 加载成功
 *     - ESP_ERR_NOT_FOUND: 配置不存在，使用默认值
 *     - ESP_FAIL: 加载失败
 */
esp_err_t color_correction_load_config(void);

/**
 * @brief 重置色彩校正配置为默认值
 * 
 * @return 
 *     - ESP_OK: 重置成功
 *     - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t color_correction_reset_config(void);

#ifdef __cplusplus
}
#endif
