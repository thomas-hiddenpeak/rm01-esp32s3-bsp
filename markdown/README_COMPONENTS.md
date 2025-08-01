# ESP32S3 组件化BSP项目

## 项目概述

这是一个基于ESP32S3的板级支持包(BSP)项目，采用组件化架构设计，提供了硬件控制、系统监控和统一设备接口。

## 架构说明

### 组件结构

```
components/
├── hardware_control/       # 硬件控制组件
│   ├── include/
│   │   └── hardware_control.h
│   ├── hardware_control.c
│   └── CMakeLists.txt
├── system_monitor/         # 系统监控组件
│   ├── include/
│   │   └── system_monitor.h
│   ├── system_monitor.c
│   └── CMakeLists.txt
└── device_interface/       # 设备接口组件
    ├── include/
    │   └── device_interface.h
    ├── device_interface.c
    └── CMakeLists.txt
```

### 组件说明

#### 1. hardware_control 组件
- **功能**: 提供硬件设备的底层控制接口
- **支持设备**:
  - 风扇PWM控制 (GPIO引脚可配置)
  - 板载WS2812 LED控制
  - 触摸WS2812 LED控制
  - GPIO输入输出控制
- **主要接口**:
  - `hardware_control_init()` - 初始化硬件控制
  - `fan_set_speed()` - 设置风扇速度
  - `board_led_set_color()` - 设置板载LED颜色
  - `touch_led_set_color()` - 设置触摸LED颜色
  - `gpio_set_output()` / `gpio_read_input()` - GPIO控制

#### 2. system_monitor 组件
- **功能**: 提供系统状态监控和性能监控
- **监控项目**:
  - 内存使用情况
  - CPU频率和核心数
  - 系统运行时间
  - 芯片信息
- **主要接口**:
  - `system_monitor_init()` - 初始化系统监控
  - `system_get_info()` - 获取系统信息
  - `system_monitor_start()` - 启动自动监控
  - `system_get_free_heap()` - 获取可用内存

#### 3. device_interface 组件
- **功能**: 提供统一的设备控制接口，整合硬件控制和系统监控
- **特性**:
  - 统一的设备控制API
  - 配置保存/加载到NVS
  - 设备事件回调机制
  - 测试接口
- **主要接口**:
  - `device_interface_init()` - 初始化设备接口
  - `device_quick_setup()` - 一键设置设备
  - `device_get_full_status()` - 获取完整设备状态
  - `device_save_config()` / `device_load_config()` - 配置管理

## 控制台命令

### 系统命令
- `help` - 显示帮助信息
- `info` - 显示系统信息
- `status` - 显示当前状态
- `reboot` - 重启系统

### 配置管理
- `save` - 保存当前配置到NVS
- `load` - 从NVS加载配置
- `clear` - 清除NVS中的配置

### 硬件控制
- `fan <0-100>|on|off` - 风扇控制
- `bled <r> <g> <b>|bright <0-100>|off|rainbow` - 板载LED控制
- `tled <r> <g> <b>|bright <0-100>|off` - 触摸LED控制
- `gpio <pin> high|low|read` - GPIO控制

### 测试命令
- `test fan|bled|tled|gpio <pin>` - 单项测试
- `test all` - 全面测试
- `test quick` - 快速测试
- `test stress <ms>` - 压力测试

## 使用示例

### 基本初始化

```c
#include "device_interface.h"

void app_main(void)
{
    // 初始化设备接口
    device_interface_config_t config = DEVICE_INTERFACE_DEFAULT_CONFIG();
    device_interface_init(&config);
    
    // 快速设置设备
    device_quick_setup(50, LED_COLOR_RED, LED_COLOR_BLUE);
    
    // 获取设备状态
    device_status_t status;
    device_get_full_status(&status);
}
```

### 使用硬件控制组件

```c
#include "hardware_control.h"

void hardware_example(void)
{
    // 初始化硬件控制
    hardware_control_init();
    
    // 控制风扇
    fan_set_speed(75);  // 75%速度
    
    // 控制LED
    led_color_t red = {255, 0, 0};
    board_led_set_color(red);
    board_led_set_brightness(80);
    
    // 控制GPIO
    gpio_set_output(2, GPIO_STATE_HIGH);
}
```

### 使用系统监控组件

```c
#include "system_monitor.h"

void monitor_example(void)
{
    // 初始化系统监控
    system_monitor_config_t config = {
        .monitor_interval_ms = 30000,
        .memory_warning_threshold = 10240,
        .enable_auto_monitoring = true,
        .warning_cb = my_warning_callback
    };
    system_monitor_init(&config);
    
    // 获取系统信息
    system_info_t info;
    system_get_info(&info);
    
    // 检查内存状态
    uint32_t free_heap = system_get_free_heap();
    bool low_memory = system_is_memory_low(8192);
}
```

## 配置说明

### 硬件配置
硬件引脚配置在 `main/hardware_config.h` 中定义：

```c
// 风扇PWM配置
#define FAN_PWM_PIN          4
#define FAN_PWM_FREQUENCY    1000

// LED配置
#define BOARD_WS2812_PIN     18
#define BOARD_WS2812_NUM     8
#define TOUCH_WS2812_PIN     17
#define TOUCH_WS2812_NUM     1
```

### 编译配置
在项目根目录的 `CMakeLists.txt` 中会自动包含components目录中的组件。

## 构建和烧录

```bash
# 构建项目
idf.py build

# 烧录和监控
idf.py flash monitor
```

## 特性优势

1. **模块化设计**: 各功能模块独立，便于维护和扩展
2. **统一接口**: 通过device_interface组件提供一致的API
3. **配置持久化**: 支持将设备配置保存到NVS
4. **事件机制**: 支持设备事件回调
5. **完整测试**: 提供多种测试模式
6. **错误处理**: 完善的错误处理和日志记录
7. **内存监控**: 自动监控系统内存使用情况

## 扩展指南

### 添加新的硬件设备
1. 在 `hardware_control` 组件中添加设备控制函数
2. 在 `hardware_control.h` 中声明新的接口
3. 在 `device_interface` 组件中添加高级封装
4. 在控制台中添加相应的命令

### 添加新的监控项目
1. 在 `system_monitor` 组件中添加监控函数
2. 更新 `system_info_t` 结构体
3. 在监控任务中添加新的监控逻辑

## 故障排除

### 常见问题
1. **编译错误**: 检查组件依赖关系和头文件包含
2. **初始化失败**: 检查硬件连接和引脚配置
3. **内存不足**: 调整任务堆栈大小或检查内存泄漏

### 调试方法
1. 使用 `device_print_full_status()` 查看设备状态
2. 运行 `test all` 进行全面硬件测试
3. 检查ESP_LOG输出获取详细错误信息

## 许可证

此项目采用 [许可证名称] 许可证，详见 LICENSE 文件。
