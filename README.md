| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# ESP32S3 组件化板级支持包(BSP)

这是一个基于ESP32S3的组件化板级支持包项目，采用模块化架构设计，提供硬件控制、系统监控和统一控制台接口。

## 🚀 主要特性

- **🎛️ 硬件控制**: PWM风扇调速、WS2812 LED控制、GPIO通用操作、USB MUX切换
- **⚡ 设备电源管理**: Orin和N305设备的电源控制和状态监控
- **📊 系统监控**: 内存监控、CPU监控、温度监控、任务状态监控  
- **💻 控制台接口**: 统一的UART控制台，支持丰富的交互命令和配置管理
- **🧩 组件化架构**: 模块化设计，便于扩展和维护
- **⚡ 事件驱动**: 异步事件处理机制
- **💾 配置持久化**: 支持NVS配置保存和加载
- **🔧 测试功能**: 完整的硬件测试套件

## 🛠️ 硬件功能

### PWM风扇控制
- GPIO 41 控制风扇PWM信号
- 支持0-100%速度调节
- 25kHz PWM频率，8位分辨率

### WS2812 LED控制
- **板载LED**: GPIO 42，28颗LED阵列
- **触摸LED**: GPIO 45，1颗状态指示LED
- 支持RGB颜色控制 (0-255)
- 支持亮度调节 (0-100%)
- 内置彩虹渐变效果

### GPIO通用控制
- 支持任意GPIO引脚操作
- 安全的高/低电平设置
- 专用的输入模式读取
- 避免输出状态干扰的设计

⚠️ **GPIO安全使用原则**:
- 输出控制：使用 `gpio <pin> high|low` 设置输出状态
- 输入读取：使用 `gpio <pin> input` 切换到输入模式并读取
- 避免在输出模式下进行状态读取，以防止GPIO状态干扰
- 关键操作（如恢复模式）完全避免状态验证

### USB MUX控制
- **MUX1引脚**: GPIO 8 - USB MUX1选择控制
- **MUX2引脚**: GPIO 48 - USB MUX2选择控制
- 支持切换USB-C接口连接目标：
  - **ESP32S3**: mux1=0, mux2=0 (默认)
  - **AGX**: mux1=1, mux2=0
  - **N305**: mux1=1, mux2=1
- 控制台命令: `usbmux esp32s3|agx|n305|status`

## 📋 如何使用

### 环境要求

- ESP-IDF v5.5 或更高版本
- ESP32S3 开发板
- Windows/Linux/macOS 开发环境

### 编译和烧录

1. 设置ESP-IDF环境：
```bash
. $IDF_PATH/export.sh  # Linux/macOS
# 或在Windows下：C:\esp\v5.5\esp-idf\export.bat
```

2. 编译项目：
```bash
idf.py build
```

3. 烧录到设备：
```bash
idf.py -p [PORT] flash monitor
```

### 控制台使用

系统启动后，可通过UART控制台（115200波特率）使用以下命令：

#### 系统命令
- `help` - 显示所有可用命令的详细帮助信息
- `info` - 显示系统详细信息
- `status` - 显示当前硬件和系统状态
- `reboot` - 重启系统

#### 配置管理命令
- `save` - 保存当前配置到NVS闪存
- `load` - 从NVS闪存加载配置
- `clear` - 清除NVS中保存的配置

#### 硬件控制命令
- `fan <speed>` - 设置风扇速度 (0-100%)
  - `fan on` - 打开风扇（默认50%速度）
  - `fan off` - 关闭风扇
- `bled <r> <g> <b>` - 设置板载LED颜色 (RGB值: 0-255)
  - `bled bright <level>` - 设置板载LED亮度 (0-100%)
  - `bled rainbow` - 启动彩虹渐变效果
  - `bled off` - 关闭板载LED
- `tled <r> <g> <b>` - 设置触摸LED颜色 (RGB值: 0-255)
  - `tled bright <level>` - 设置触摸LED亮度 (0-100%)
  - `tled off` - 关闭触摸LED
- `gpio <pin> high|low|input` - GPIO引脚控制和读取
  - `gpio <pin> high` - 设置GPIO引脚为高电平
  - `gpio <pin> low` - 设置GPIO引脚为低电平
  - `gpio <pin> input` - 切换到输入模式并读取状态
- `usbmux <target>` - 切换USB-C接口连接目标
  - `usbmux esp32s3` - 连接到ESP32S3
  - `usbmux agx` - 连接到AGX
  - `usbmux n305` - 连接到N305
  - `usbmux status` - 查看当前连接状态

#### 设备电源控制命令
- `orin <action>` - Orin设备电源控制
  - `orin on` - 开机Orin设备
  - `orin off` - 关机Orin设备
  - `orin reset` - 重启Orin设备
  - `orin recovery` - 进入恢复模式并切换USB到AGX
  - `orin status` - 显示Orin电源状态
- `n305 <action>` - N305设备电源控制
  - `n305 toggle` - 切换N305开机/关机状态
  - `n305 reset` - 重启N305设备
  - `n305 status` - 显示N305电源状态

#### 测试命令
- `test fan` - 执行风扇功能测试
- `test bled` - 执行板载LED测试
- `test tled` - 执行触摸LED测试
- `test gpio <pin>` - 安全测试GPIO输出功能
- `test gpio_input <pin>` - 测试GPIO输入功能
- `test orin` - 测试Orin电源控制功能
- `test n305` - 测试N305电源控制功能
- `test all` - 执行完整的硬件测试序列
- `test quick` - 执行快速测试
- `test stress <ms>` - 执行指定时长的压力测试

## 📁 项目结构

```
├── CMakeLists.txt              项目构建配置
├── sdkconfig                   ESP-IDF配置文件
├── main/                       主程序目录
│   ├── main.c                  主程序入口
│   ├── hardware_config.h       硬件配置定义
│   └── CMakeLists.txt          主程序构建配置
├── components/                 自定义组件目录
│   ├── hardware_control/       硬件控制组件
│   ├── system_monitor/         系统监控组件
│   ├── device_interface/       设备接口组件
│   └── console_interface/      控制台接口组件
├── managed_components/         托管组件
│   └── espressif__led_strip/   LED条带驱动
└── markdown/                   项目文档
    ├── PROJECT_SUMMARY.md      项目总结
    ├── CONSOLE_GUIDE.md        控制台使用指南
    ├── USB_MUX_CONTROL_GUIDE.md USB MUX控制功能指南
    └── README_COMPONENTS.md    组件说明文档
```

## 🔧 组件架构

### 核心组件

1. **hardware_control**: 硬件抽象层，提供PWM、GPIO、LED等硬件接口
2. **system_monitor**: 系统监控，包括内存、CPU、温度等状态监控
3. **device_interface**: 统一设备接口，整合硬件控制和系统监控
4. **console_interface**: 控制台接口，提供UART命令行交互

### 设计特点

- **模块化**: 各组件独立开发和测试
- **事件驱动**: 异步事件处理机制
- **可扩展**: 便于添加新的硬件模块
- **统一接口**: 标准化的API设计

## 🐛 故障排除

### 程序上传失败

* 检查硬件连接：运行 `idf.py -p PORT monitor`，重启开发板查看输出日志
* 波特率过高：在 `menuconfig` 中降低下载波特率后重试
* 端口权限：确保有权限访问串口设备

### 控制台无响应

* 检查串口设置：115200波特率，8N1
* 检查USB驱动是否正确安装
* 尝试其他串口工具（如PuTTY、minicom等）

### LED不亮

* 检查GPIO引脚配置
* 确认LED供电正常
* 检查WS2812时序是否正确

## 🔗 技术支持

如有技术问题或功能需求，请通过以下渠道反馈：

* 技术问题：访问 [esp32.com](https://esp32.com/) 论坛
* 功能请求或错误报告：创建 [GitHub issue](https://github.com/thomas-hiddenpeak/rm01-esp32s3-bsp/issues)

## 📝 许可证

本项目遵循开源许可证，详情请参考项目根目录下的LICENSE文件。

## 🤝 贡献

欢迎提交Pull Request或Issue来改进项目。在贡献代码前，请确保：

1. 代码符合项目编码规范
2. 新功能有对应的文档说明
3. 通过了基本的功能测试

---

**注意**: 本项目专为ESP32S3设计，不保证在其他ESP32系列芯片上的兼容性。
