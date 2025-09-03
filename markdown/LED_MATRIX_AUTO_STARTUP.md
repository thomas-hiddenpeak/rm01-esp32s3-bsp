# LED矩阵自动启动功能实现

## 概述

实现了LED矩阵的自动启动功能，包括配置保存到NVS和开机自动显示。系统启动时会在SD卡挂载完成后自动检查配置并显示预设的LED矩阵内容。

## 新增配置结构

### 1. LED配置扩展
在 `config_manager.h` 中扩展了 `led_config_t` 结构：

```c
typedef struct {
    uint8_t default_brightness;         // 原有字段
    led_color_t board_led_color;        // 原有字段  
    led_color_t touch_led_color;        // 原有字段
    bool effect_enable;                 // 原有字段
    uint16_t rainbow_speed_ms;          // 原有字段
    
    // LED矩阵配置 (新增)
    bool matrix_auto_start;             // 自动启动矩阵显示
    uint8_t matrix_brightness;          // 矩阵默认亮度 (0-100)
    char matrix_startup_animation[64];  // 启动动画名称
    bool matrix_enable;                 // 启用矩阵显示
} led_config_t;
```

### 2. 默认配置
```c
#define DEFAULT_LED_CONFIG() { \
    /* 原有配置 */ \
    .matrix_auto_start = true, \
    .matrix_brightness = 30, \
    .matrix_startup_animation = "Logo", \
    .matrix_enable = true \
}
```

## 新增API函数

### 1. 配置管理API
```c
// 设置LED矩阵配置
esp_err_t config_manager_set_matrix_config(uint8_t brightness, 
                                           const char *animation_name,
                                           bool auto_start,
                                           bool enable);

// 获取LED矩阵配置
esp_err_t config_manager_get_matrix_config(uint8_t *brightness,
                                           char *animation_name,
                                           bool *auto_start,
                                           bool *enable);
```

### 2. 硬件控制API
```c
// 获取LED矩阵当前亮度
uint8_t led_matrix_get_brightness(void);
```

## 控制台命令扩展

### 新增的matrix命令
```bash
matrix config save              # 保存当前矩阵配置
matrix config show              # 显示当前矩阵配置
```

### 使用示例
```bash
# 设置矩阵并保存配置
matrix bright 40
matrix load Logo
matrix config save

# 查看当前配置
matrix config show
```

## 自动启动流程

### 1. 系统启动时序
```
1. NVS初始化
2. 配置管理器初始化
3. 加载保存的配置
4. 设备接口初始化
5. SD卡自动挂载 ← LED矩阵自动启动触发点
6. 控制台命令注册
7. 以太网启动
8. Web服务器启动
9. 控制台任务启动
```

### 2. LED矩阵自动启动逻辑
在 `main.c` 中，SD卡挂载成功后：

```c
// 获取矩阵配置
config_manager_get_matrix_config(&brightness, animation_name, &auto_start, &enable);

if (enable && auto_start) {
    // 初始化LED矩阵
    led_matrix_init();
    
    // 设置亮度
    led_matrix_set_brightness(brightness);
    
    // 加载启动动画
    ret = led_matrix_load_animation(animation_name);
    if (ret == ESP_ERR_NOT_FOUND) {
        // 动画未找到，显示测试图案
        led_matrix_test_pattern();
    }
}
```

### 3. 错误处理
- SD卡挂载失败：跳过LED矩阵自动启动
- 动画文件未找到：显示测试图案
- 亮度设置失败：记录警告但继续
- 矩阵初始化失败：记录错误

## 配置持久化

### 1. NVS存储
- 所有LED矩阵配置随其他系统配置一起保存到NVS
- 配置包含校验和，确保数据完整性
- 支持配置版本管理

### 2. 配置更新
```c
// 更新配置并立即保存
config_manager_set_matrix_config(brightness, animation_name, auto_start, enable);
config_manager_save();
```

## 使用场景

### 1. 首次使用
```bash
# 1. 设置理想的显示效果
matrix bright 25
matrix load Logo

# 2. 保存配置为默认启动
matrix config save

# 3. 重启后自动恢复此设置
```

### 2. 调试模式
```bash
# 临时禁用自动启动
matrix config save  # 保存当前亮度但保持自动启动开启

# 或者通过配置管理器完全禁用
# (需要通过配置文件或专门的命令)
```

### 3. 演示模式
```bash
# 保存演示专用配置
matrix bright 80
matrix load Logo
matrix config save
```

## 配置参数说明

| 参数 | 类型 | 范围 | 默认值 | 说明 |
|------|------|------|--------|------|
| matrix_brightness | uint8_t | 0-100 | 30 | 矩阵亮度百分比 |
| matrix_startup_animation | char[64] | - | "Logo" | 启动时加载的动画名称 |
| matrix_auto_start | bool | true/false | true | 是否在启动时自动显示 |
| matrix_enable | bool | true/false | true | 是否启用矩阵功能 |

## 日志输出示例

```
I (2345) ESP32S3_MAIN: SD卡自动挂载成功
I (2346) ESP32S3_MAIN: 检查LED矩阵自动启动配置...
I (2347) ESP32S3_MAIN: 配置为自动启动LED矩阵: 亮度=30%, 动画='Logo'
I (2348) HARDWARE_CONTROL: LED matrix initialized successfully
I (2349) HARDWARE_CONTROL: LED matrix brightness set to 30%
I (2350) HARDWARE_CONTROL: Loading animation: Logo
I (2351) HARDWARE_CONTROL: Animation 'Logo' loaded successfully (234 points)
I (2352) ESP32S3_MAIN: LED矩阵自动启动成功
```

## 注意事项

1. **SD卡依赖**: LED矩阵自动启动需要SD卡成功挂载
2. **动画文件**: 确保 `/sdcard/matrix.json` 文件存在且格式正确
3. **亮度限制**: 建议启动亮度不超过50%以节省功耗
4. **配置持久化**: 使用 `matrix config save` 确保配置被保存
5. **错误恢复**: 如果配置的动画不存在，系统会自动显示测试图案

这个实现确保了LED矩阵能够在系统启动时自动恢复到用户期望的状态，提供了良好的用户体验。
