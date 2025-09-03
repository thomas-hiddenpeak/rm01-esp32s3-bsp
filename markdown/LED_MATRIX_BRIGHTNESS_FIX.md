# LED矩阵亮度功能修复

## 问题分析
用户反馈 `matrix bright` 命令没有实现功能。经检查发现原来的实现只是设置了亮度变量，但没有立即应用到当前显示的LED上。

## 解决方案
1. **添加矩阵缓冲区**: 增加 `s_matrix_buffer` 数组来保存每个像素的原始颜色值
2. **改进像素设置**: `led_matrix_set_pixel` 现在将原始颜色保存到缓冲区
3. **实现亮度应用**: 新增 `led_matrix_apply_buffer` 函数，将缓冲区的颜色按当前亮度应用到LED
4. **修复亮度设置**: `led_matrix_set_brightness` 现在会立即重新应用当前显示内容

## 修改的函数

### 1. 静态变量
```c
static led_color_t s_matrix_buffer[LED_MATRIX_NUM]; // 添加矩阵缓冲区
```

### 2. led_matrix_init()
```c
// 初始化矩阵缓冲区为黑色
memset(s_matrix_buffer, 0, sizeof(s_matrix_buffer));
```

### 3. led_matrix_apply_buffer() (新增)
```c
static esp_err_t led_matrix_apply_buffer(void)
{
    // 将缓冲区的颜色按当前亮度应用到LED strip
    for (int i = 0; i < LED_MATRIX_NUM; i++) {
        uint8_t r = (s_matrix_buffer[i].red * s_matrix_brightness) / 100;
        uint8_t g = (s_matrix_buffer[i].green * s_matrix_brightness) / 100;
        uint8_t b = (s_matrix_buffer[i].blue * s_matrix_brightness) / 100;
        led_strip_set_pixel(s_matrix_led_strip, i, r, g, b);
    }
    return led_strip_refresh(s_matrix_led_strip);
}
```

### 4. led_matrix_set_pixel()
```c
// 保存原始颜色到缓冲区
s_matrix_buffer[index] = color;
// 然后应用亮度设置到LED
```

### 5. led_matrix_set_brightness()
```c
s_matrix_brightness = brightness;
// 立即应用新的亮度到当前显示的内容
return led_matrix_apply_buffer();
```

### 6. led_matrix_clear()
```c
// 清空缓冲区
memset(s_matrix_buffer, 0, sizeof(s_matrix_buffer));
```

## 测试步骤
1. `matrix test` - 显示测试图案
2. `matrix bright 20` - 调整亮度到20%，应该立即看到变暗效果
3. `matrix bright 80` - 调整亮度到80%，应该立即看到变亮效果
4. `matrix pixel 15 15 255 0 0` - 设置一个红色像素
5. `matrix bright 50` - 调整亮度，红色像素应该相应变化

## 预期效果
现在 `matrix bright` 命令会立即影响当前显示的所有像素，而不需要重新设置像素或重新加载动画。
