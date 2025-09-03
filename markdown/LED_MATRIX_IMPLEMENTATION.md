# LED矩阵控制功能实现

## 概述

基于用户提供的信息，我们为ESP32S3开发板实现了32x32 WS2812 LED矩阵的控制功能。LED矩阵连接到GPIO9引脚，总共包含1024个LED。

## 配置定义

在 `components/hardware_control/include/hardware_control.h` 中添加了以下配置：

```c
// LED矩阵配置
#define LED_MATRIX_PIN      9       // LED矩阵WS2812控制引脚
#define LED_MATRIX_WIDTH    32      // LED矩阵宽度
#define LED_MATRIX_HEIGHT   32      // LED矩阵高度
#define LED_MATRIX_NUM      (LED_MATRIX_WIDTH * LED_MATRIX_HEIGHT)  // LED总数量
```

## 控制台命令

实现了 `matrix` 控制台命令，支持以下功能：

### 命令语法

```bash
matrix clear                          # 清空LED矩阵
matrix test                           # 显示测试图案
matrix bright <0-100>                 # 设置亮度(0-100%)
matrix pixel <x> <y> <r> <g> <b>      # 设置单个像素颜色
matrix load <animation_name>          # 从JSON文件加载动画
```

### 使用示例

```bash
# 清空LED矩阵
matrix clear

# 显示测试图案（边框、对角线、中心十字）
matrix test

# 设置亮度为50%
matrix bright 50

# 在坐标(10,15)显示红色像素
matrix pixel 10 15 255 0 0

# 加载名为"Logo"的动画
matrix load Logo
```

## API接口

在 `components/hardware_control/include/hardware_control.h` 中添加了以下API函数：

```c
// LED矩阵初始化
esp_err_t led_matrix_init(void);

// 设置单个像素颜色
esp_err_t led_matrix_set_pixel(uint8_t x, uint8_t y, led_color_t color);

// 清空LED矩阵
esp_err_t led_matrix_clear(void);

// 刷新显示
esp_err_t led_matrix_refresh(void);

// 设置亮度
esp_err_t led_matrix_set_brightness(uint8_t brightness);

// 从JSON文件加载动画
esp_err_t led_matrix_load_animation(const char *animation_name);

// 显示测试图案
esp_err_t led_matrix_test_pattern(void);
```

## JSON动画格式

动画数据存储在 `/sdcard/matrix.json` 文件中，格式如下：

```json
{
  "animations": [
    {
      "name": "Logo",
      "points": [
        {
          "type": "point",
          "x": 11,
          "y": 8,
          "r": 151,
          "g": 189,
          "b": 246
        },
        ...
      ]
    }
  ]
}
```

- `x`, `y`: 像素坐标 (0-31)
- `r`, `g`, `b`: RGB颜色值 (0-255)

## 硬件接口

- **GPIO引脚**: GPIO9
- **LED类型**: WS2812
- **矩阵尺寸**: 32x32 (1024个LED)
- **数据协议**: RMT驱动
- **时钟频率**: 10MHz

## 测试命令

在现有的 `test` 命令中添加了LED矩阵测试：

```bash
test matrix  # 显示LED矩阵测试图案
```

## 实现特点

1. **内存效率**: 使用ESP-IDF的led_strip组件，支持硬件RMT驱动
2. **亮度控制**: 独立的亮度设置，不影响颜色值
3. **JSON解析**: 使用cJSON库解析动画文件
4. **错误处理**: 完整的参数验证和错误报告
5. **坐标系统**: 标准的笛卡尔坐标系，原点在左上角

## 依赖组件

- `led_strip`: ESP-IDF的WS2812驱动组件
- `json`: cJSON库用于解析动画文件
- `driver`: GPIO和RMT驱动
- `freertos`: 任务管理

## 注意事项

1. 确保SD卡已正确挂载到 `/sdcard`
2. 动画文件必须是有效的JSON格式
3. 坐标范围: x,y ∈ [0,31]
4. 颜色范围: r,g,b ∈ [0,255]
5. 亮度范围: brightness ∈ [0,100]

## 编译要求

在 `components/hardware_control/CMakeLists.txt` 中添加了json依赖：

```cmake
idf_component_register(SRCS "hardware_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES driver led_strip esp_timer json
                       PRIV_REQUIRES freertos)
```

## 使用流程

1. 系统启动时自动初始化LED矩阵
2. 通过控制台命令控制显示
3. 可以实时调整亮度和显示内容
4. 支持从SD卡加载预设动画

这个实现提供了完整的LED矩阵控制功能，支持实时控制和动画播放。
