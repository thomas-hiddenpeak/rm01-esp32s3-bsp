# Console Interface Component

## 组件概述

`console_interface` 是一个独立的控制台接口组件，提供完整的控制台功能，包括命令注册、输入处理、事件回调等。该组件将原来在 `main.c` 中的控制台代码完全模块化，使其可以在其他项目中复用。

## 主要特性

### 🚀 核心功能
- **完整的控制台系统**: 支持命令注册、执行、历史记录
- **模块化设计**: 独立的组件，可在其他项目中复用
- **事件驱动**: 支持控制台事件回调机制
- **统计功能**: 提供命令执行统计和运行时间统计
- **配置灵活**: 支持自定义配置参数

### 🎛️ 命令分类
- **系统命令**: help, info, status, reboot
- **设备控制**: fan, bled, tled, gpio, test
- **配置管理**: save, load, clear, config, defaults
- **网络配置**: eth_config, eth_dhcp, eth_gateway (自动保存)

### 📊 控制台特性
- **输入处理**: 支持退格、多行输入、字符过滤
- **历史记录**: 支持命令历史浏览
- **自动补全**: 支持 TAB 键自动补全
- **错误处理**: 完善的命令错误处理机制
- **智能保存**: 网络配置自动保存，硬件配置需手动保存

## API 接口

### 初始化和控制

```c
// 初始化控制台接口
esp_err_t console_interface_init(const console_interface_config_t *config);

// 启动控制台任务
esp_err_t console_interface_start(uint32_t stack_size, uint8_t priority);

// 停止控制台任务
esp_err_t console_interface_stop(void);
```

### 命令注册

```c
// 注册系统命令
esp_err_t console_interface_register_system_commands(void);

// 注册设备控制命令
esp_err_t console_interface_register_device_commands(void);

// 注册配置管理命令
esp_err_t console_interface_register_config_commands(void);
```

### 事件处理

```c
// 注册事件回调
esp_err_t console_interface_register_event_callback(console_event_callback_t callback);
```

### 辅助功能

```c
// 程序化执行命令
esp_err_t console_interface_execute_command(const char *command);

// 控制台打印
void console_interface_print(const char *format, ...);

// 显示启动横幅
void console_interface_show_banner(void);

// 获取统计信息
esp_err_t console_interface_get_stats(uint32_t *commands_executed, uint64_t *uptime_ms);
```

## 配置参数

```c
typedef struct {
    uint16_t max_cmdline_length;    // 最大命令行长度
    uint8_t max_cmdline_args;       // 最大命令行参数数量
    uint16_t history_length;        // 命令历史长度
    bool enable_color_hints;        // 启用彩色提示
    bool enable_multiline;          // 启用多行输入
    const char *prompt;             // 控制台提示符
} console_interface_config_t;
```

## 事件类型

```c
typedef enum {
    CONSOLE_EVENT_READY,           // 控制台准备就绪
    CONSOLE_EVENT_COMMAND_SUCCESS, // 命令执行成功
    CONSOLE_EVENT_COMMAND_ERROR,   // 命令执行错误
    CONSOLE_EVENT_SHUTDOWN         // 控制台关闭
} console_event_t;
```

## 使用示例

### 基本使用

```c
#include "console_interface.h"

// 控制台事件处理器
static void console_event_handler(console_event_t event, const char *data)
{
    switch (event) {
        case CONSOLE_EVENT_READY:
            ESP_LOGI("APP", "控制台准备就绪");
            break;
        case CONSOLE_EVENT_COMMAND_SUCCESS:
            ESP_LOGD("APP", "命令执行成功: %s", data);
            break;
        default:
            break;
    }
}

void app_main(void)
{
    // 初始化控制台
    console_interface_config_t console_config = CONSOLE_INTERFACE_DEFAULT_CONFIG();
    console_interface_init(&console_config);
    
    // 注册事件回调
    console_interface_register_event_callback(console_event_handler);
    
    // 注册所有命令
    console_interface_register_system_commands();
    console_interface_register_device_commands();
    console_interface_register_config_commands();
    
    // 启动控制台任务
    console_interface_start(4096, 5);
}
```

### 高级使用

```c
// 程序化执行命令
console_interface_execute_command("info");
console_interface_execute_command("fan 50");

// 自定义打印
console_interface_print("系统状态: %s\n", "正常");

// 获取统计信息
uint32_t commands_executed;
uint64_t uptime_ms;
console_interface_get_stats(&commands_executed, &uptime_ms);
printf("已执行 %lu 条命令，运行 %llu ms\n", commands_executed, uptime_ms);
```

## 🔧 配置管理特性

### 智能自动保存
控制台接口实现了智能的配置保存机制：

#### 自动保存的配置类型
- **网络配置**: 以太网IP设置、DHCP服务器配置、网关设置
- **命令**: `config set eth/dhcp/gateway` 和 `defaults eth/dhcp/gateway`
- **特点**: 修改后立即保存到NVS，重启后保持

#### 手动保存的配置类型  
- **硬件配置**: 风扇速度、LED颜色/亮度设置
- **命令**: `config set fan/led`
- **特点**: 需要手动执行 `save` 命令保存

### 使用示例

```bash
# 网络配置 - 自动保存
config set dhcp true 10.10.99.100 10.10.99.110 24
# 输出: ✅ DHCP配置已自动保存到NVS

# 硬件配置 - 需手动保存
config set fan 50 0 true
save  # 手动保存
# 输出: 配置已保存到NVS
```

## 依赖关系

### 必需组件
- `freertos` - FreeRTOS 任务管理
- `esp_common` - ESP-IDF 公共库
- `esp_timer` - 时间管理
- `console` - ESP 控制台支持
- `device_interface` - 设备接口组件
- `hardware_control` - 硬件控制组件
- `system_monitor` - 系统监控组件

### 可选组件
- `driver` - 硬件驱动(私有依赖)

## 文件结构

```
components/console_interface/
├── include/
│   └── console_interface.h    # 公共接口定义
├── console_interface.c        # 组件实现
└── CMakeLists.txt            # 构建配置
```

## 主要改进

### 相比原 main.c 中的控制台代码

1. **模块化**: 完全独立的组件，可复用
2. **事件驱动**: 支持事件回调机制
3. **统计功能**: 提供运行统计信息
4. **更好的错误处理**: 完善的错误处理和日志记录
5. **配置灵活**: 支持自定义配置参数
6. **API 丰富**: 提供程序化接口

### 内存优化
- 组件化后减少了 main.c 的代码量
- 更好的内存管理和任务控制
- 支持动态启动和停止

## 与其他组件的集成

该组件与以下组件紧密集成：

- **device_interface**: 提供统一的设备控制接口
- **hardware_control**: 直接控制硬件设备
- **system_monitor**: 系统状态监控

## 注意事项

1. **初始化顺序**: 必须先初始化依赖的设备组件
2. **任务优先级**: 建议设置适当的任务优先级
3. **内存配置**: 确保分配足够的栈空间
4. **事件处理**: 事件回调函数应保持简洁，避免阻塞

## 未来扩展

可能的扩展方向：

1. **远程控制**: 支持网络远程控制台
2. **脚本支持**: 支持脚本文件执行
3. **权限管理**: 支持用户权限控制
4. **插件系统**: 支持动态加载命令插件
5. **多语言**: 支持多语言界面

---

*该组件是 ESP32S3 BSP 项目组件化架构的重要组成部分，提供了完整、灵活、可复用的控制台解决方案。*
