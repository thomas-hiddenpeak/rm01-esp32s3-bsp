# USB MUX控制功能使用指南

## 概述

我们为ESP32S3-BSP项目添加了USB MUX控制功能，可以通过控制台命令来切换设备顶部控制扩展USB-C接口的连接目标。

## 硬件配置

### GPIO引脚定义
- **ESP32_MUX1_SEL**: GPIO8 - USB MUX1选择引脚
- **ESP32_MUX2_SEL**: GPIO48 - USB MUX2选择引脚

### MUX状态映射
| 目标设备 | MUX1_SEL | MUX2_SEL | 描述 |
|---------|----------|----------|------|
| ESP32S3 | 0 (低)   | 0 (低)   | 连接到ESP32S3的USB接口 |
| AGX     | 1 (高)   | 0 (低)   | 连接到AGX的USB接口 |
| N305    | 1 (高)   | 1 (高)   | 连接到N305的USB接口 |

## 控制台命令

### usbmux 命令

**语法**: `usbmux <target>|status`

**参数**:
- `esp32s3` - 切换USB-C接口到ESP32S3
- `agx` - 切换USB-C接口到AGX
- `n305` - 切换USB-C接口到N305
- `status` - 显示当前USB接口连接状态

### debug 命令（调试功能）

**语法**: `debug <option>`

**参数**:
- `status` - 显示系统初始化状态
- `hardware` - 显示硬件状态
- `device` - 显示设备状态

**使用示例**:

```bash
# 检查系统初始化状态
ESP32S3> debug status
=== 系统初始化状态 ===
控制台接口: 已初始化
硬件控制: 已初始化
设备接口: 已初始化
硬件控制可用: 是
系统监控可用: 是
====================

# 如果遇到USB MUX操作失败，请先检查状态
ESP32S3> debug status
# 确认硬件控制已正确初始化后再使用usbmux命令
```

### USB MUX命令使用示例

```bash
# 切换到ESP32S3
ESP32S3> usbmux esp32s3
USB-C接口已切换到ESP32S3

# 切换到AGX
ESP32S3> usbmux agx
USB-C接口已切换到AGX

# 切换到N305
ESP32S3> usbmux n305
USB-C接口已切换到N305

# 查看当前状态
ESP32S3> usbmux status
当前USB-C接口连接到: ESP32S3
```

## API接口

### 头文件
```c
#include "hardware_control.h"
```

### 枚举类型
```c
typedef enum {
    USB_MUX_ESP32S3 = 0,     /*!< ESP32S3的USB接口 (mux1=0, mux2=0) */
    USB_MUX_AGX = 1,         /*!< AGX的USB接口 (mux1=1, mux2=0) */
    USB_MUX_N305 = 2         /*!< N305的USB接口 (mux1=1, mux2=1) */
} usb_mux_target_t;
```

### 函数接口

#### usb_mux_set_target()
设置USB MUX目标设备

```c
esp_err_t usb_mux_set_target(usb_mux_target_t target);
```

**参数**:
- `target`: USB MUX目标设备

**返回值**:
- `ESP_OK`: 设置成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_FAIL`: 设置失败

#### usb_mux_get_target()
获取当前USB MUX目标设备

```c
esp_err_t usb_mux_get_target(usb_mux_target_t *target);
```

**参数**:
- `target`: 存储当前目标的指针

**返回值**:
- `ESP_OK`: 获取成功
- `ESP_ERR_INVALID_ARG`: 参数无效

#### usb_mux_get_target_name()
获取USB MUX目标名称字符串

```c
const char *usb_mux_get_target_name(usb_mux_target_t target);
```

**参数**:
- `target`: USB MUX目标

**返回值**: 目标名称字符串，如果参数无效则返回"Unknown"

## 使用示例

### C代码示例

```c
#include "hardware_control.h"

void switch_to_agx_example(void)
{
    // 切换到AGX
    esp_err_t ret = usb_mux_set_target(USB_MUX_AGX);
    if (ret == ESP_OK) {
        printf("USB-C接口已成功切换到AGX\n");
    } else {
        printf("切换失败: %s\n", esp_err_to_name(ret));
    }
}

void check_current_target_example(void)
{
    usb_mux_target_t current_target;
    esp_err_t ret = usb_mux_get_target(&current_target);
    if (ret == ESP_OK) {
        printf("当前USB-C接口连接到: %s\n", 
               usb_mux_get_target_name(current_target));
    }
}
```

## 系统集成

### 初始化
USB MUX控制功能在硬件控制组件初始化时自动初始化，默认连接到ESP32S3。

### 状态管理
USB MUX的当前状态会保存在硬件状态结构体中，可以通过API获取。

### 错误处理
所有USB MUX相关函数都会返回适当的错误代码，方便调试和错误处理。

## 注意事项

1. **GPIO配置**: 确保GPIO8和GPIO48没有被其他功能占用
2. **初始化顺序**: USB MUX在硬件控制组件初始化时自动配置
3. **默认状态**: 系统启动后默认连接到ESP32S3
4. **线程安全**: 当前实现不是线程安全的，在多线程环境中使用时需要额外的同步机制

## 故障排除

### USB MUX操作失败

**错误信息**: `E (xxxxx) HARDWARE_CONTROL: Hardware control not initialized`

**原因**: 硬件控制组件未正确初始化

**解决步骤**:
1. 使用`debug status`命令检查系统初始化状态
2. 确认硬件控制组件已初始化
3. 如果硬件控制未初始化，检查设备接口配置
4. 重启系统重新初始化

**示例**:
```bash
ESP32S3> debug status
=== 系统初始化状态 ===
控制台接口: 已初始化
硬件控制: 未初始化  ← 问题所在
设备接口: 已初始化
硬件控制可用: 否
====================

# 如果硬件控制未初始化，请重启系统
ESP32S3> restart
```

### GPIO控制失败

如果GPIO设置失败，可能的原因：
1. GPIO引脚被其他功能占用
2. GPIO配置权限问题
3. 硬件连接问题

### 验证GPIO状态

使用gpio命令手动测试：
```bash
# 手动测试MUX1引脚 (GPIO8)
ESP32S3> gpio 8 high
ESP32S3> gpio 8 low

# 手动测试MUX2引脚 (GPIO48)  
ESP32S3> gpio 48 high
ESP32S3> gpio 48 low
```

## 更新日志

### 2025年8月1日
- ✅ 添加USB MUX控制硬件支持
- ✅ 实现usbmux控制台命令
- ✅ 添加完整的API接口
- ✅ 更新硬件控制组件
- ✅ 添加控制台命令注册
- ✅ 完成构建和测试

## 相关文件

### 硬件控制组件
- `components/hardware_control/include/hardware_control.h` - 头文件定义
- `components/hardware_control/hardware_control.c` - 实现文件

### 控制台组件
- `components/console_interface/console_interface.c` - 控制台命令实现

### 配置文件
- `main/hardware_config.h` - 硬件配置定义

该功能已经完全集成到现有的硬件控制和控制台系统中，可以立即使用。
