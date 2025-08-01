# ESP32S3 控制台程序使用说明

## 功能概述

这是一个为ESP32S3开发的控制台程序，提供以下功能：

1. **风扇控制** - PWM调速控制
2. **WS2812 LED控制** - 板载和触摸LED
3. **GPIO控制** - 通用IO口操作
4. **系统监控** - 内存、运行时间等信息

## 硬件配置

- 风扇PWM引脚: GPIO 41
- 板载WS2812引脚: GPIO 42 (28颗LED)
- 触摸WS2812引脚: GPIO 45 (1颗LED)

## 构建和烧录

1. 设置ESP-IDF环境：
```bash
C:\Users\sprin\esp\v5.5\esp-idf\export.bat
```

2. 构建项目：
```bash
idf.py build
```

3. 烧录到ESP32S3：
```bash
idf.py -p COMx flash monitor
```

## 控制台命令

### 系统命令

- `help` - 显示帮助信息
- `info` - 显示系统信息
- `status` - 显示当前状态
- `reboot` - 重启系统

### 风扇控制

- `fan <0-100>` - 设置风扇速度百分比
- `fan on` - 打开风扇(50%速度)
- `fan off` - 关闭风扇

示例：
```
ESP32S3> fan 75
风扇速度设置为: 75%

ESP32S3> fan off
风扇速度设置为: 0%
```

### 板载LED控制

- `bled <r> <g> <b>` - 设置RGB颜色值(0-255)
- `bled bright <0-100>` - 设置亮度百分比
- `bled off` - 关闭所有LED
- `bled rainbow` - 彩虹渐变效果

示例：
```
ESP32S3> bled 255 0 0
板载LED设置为: R=255, G=0, B=0, 亮度=50%

ESP32S3> bled bright 80
板载LED亮度设置为: 80%

ESP32S3> bled rainbow
板载LED设置为彩虹效果
```

### 触摸LED控制

- `tled <r> <g> <b>` - 设置RGB颜色值(0-255)
- `tled bright <0-100>` - 设置亮度百分比
- `tled off` - 关闭LED

示例：
```
ESP32S3> tled 0 255 0
触摸LED设置为: R=0, G=255, B=0, 亮度=50%

ESP32S3> tled bright 100
触摸LED亮度设置为: 100%
```

### GPIO控制

- `gpio <pin> high` - 设置GPIO为高电平
- `gpio <pin> low` - 设置GPIO为低电平
- `gpio <pin> read` - 读取GPIO状态

示例：
```
ESP32S3> gpio 2 high
GPIO2 设置为高电平

ESP32S3> gpio 2 read
GPIO2 当前电平: 高

ESP32S3> gpio 2 low
GPIO2 设置为低电平
```

## 系统监控

程序会自动每10秒输出系统监控信息，包括：
- 可用堆内存
- 系统运行时间
- 内存不足警告

## 注意事项

1. 串口波特率设置为115200
2. 风扇PWM频率为25kHz，8位分辨率
3. WS2812使用RMT驱动，10MHz时钟
4. GPIO操作前会自动配置为输入/输出模式
5. 所有颜色值范围为0-255
6. 亮度值范围为0-100%

## 故障排除

1. **连接失败** - 检查串口端口和波特率
2. **LED不亮** - 检查引脚连接和电源
3. **风扇不转** - 检查PWM引脚连接和风扇电源
4. **命令无响应** - 检查命令格式和参数范围

## 扩展功能

可以通过修改源代码添加更多功能：
- 温度传感器读取
- 电机控制
- 无线通信功能
- 更多LED效果
- 定时任务

## 开发信息

- 基于ESP-IDF v5.5
- 使用FreeRTOS任务管理
- 支持中文显示
- 模块化设计，易于扩展
