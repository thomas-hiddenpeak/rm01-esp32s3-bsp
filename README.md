| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# RM-01 robOS 板上机架操作系统

这是一个基于ESP32S3的组件化板级支持包项目，采用模块化架构设计，提供硬件控制、系统监控和统一控制台接口。
这也是一个比较谦虚的表述方式，其本质是一个RM-01的便携AI超算系统的设备管理系统，并且包含一个简易的文件操作模组。

## 🚀 主要特性

- **🎛️ 硬件控制**: PWM风扇调速、WS2812 LED控制、32x32 LED矩阵、GPIO通用操作、USB MUX切换
- **🌐 网络功能**: W5500以太网接口、DHCP服务器、网关服务、网络配置管理
- **💾 存储功能**: TF卡（MicroSD）文件系统支持，完整的文件管理操作
- **⚡ 设备电源管理**: AGX和LPMU设备的电源控制和状态监控
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
- **LED矩阵**: GPIO 9，32x32 WS2812矩阵 (1024颗LED)
- 支持RGB颜色控制 (0-255)
- 支持亮度调节 (0-100%)
- 内置彩虹渐变效果
- LED矩阵支持从SD卡加载JSON动画文件
- 配置持久化和开机自动启动

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

### 以太网功能 (W5500)
- **SPI接口**: SPI2_HOST
- **引脚配置**:
  - RST: GPIO 39 - 复位信号
  - INT: GPIO 38 - 中断信号  
  - MISO: GPIO 13 - SPI数据输入
  - SCLK: GPIO 12 - SPI时钟
  - MOSI: GPIO 11 - SPI数据输出
  - CS: GPIO 10 - SPI片选
- **网络配置**:
  - 默认IP: 10.10.99.97
  - 子网掩码: 255.255.255.0
  - 网关: 10.10.99.1
  - DNS: 8.8.8.8
- **DHCP服务器**:
  - IP池范围: 10.10.99.100 - 10.10.99.110
  - 默认租期: 24小时
  - 支持客户端跟踪和管理
- **功能特性**:
  - 静态IP配置
  - DHCP服务器功能
  - **网关服务**
  - 网络连通性测试
  - NVS配置持久化

### TF卡存储功能 (SDMMC 4-bit)
- **接口类型**: SDMMC 4-bit模式
- **时钟频率**: 40MHz (高速模式)
- **引脚配置**:
  - CMD: GPIO 4 - 命令线
  - CLK: GPIO 5 - 时钟线
  - D0: GPIO 6 - 数据线0
  - D1: GPIO 7 - 数据线1
  - D2: GPIO 15 - 数据线2
  - D3: GPIO 16 - 数据线3
- **文件系统**: FAT32格式
- **功能特性**:
  - 自动挂载和卸载
  - 文件和目录完整操作
  - 空间监控和管理
  - 格式化支持
  - 13个专用控制台命令
  - POSIX文件操作兼容

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
- `matrix <command>` - LED矩阵控制 (32x32 WS2812矩阵，GPIO 9)
  - `matrix clear` - 清空LED矩阵
  - `matrix test` - 显示测试图案（边框、对角线、十字）
  - `matrix bright <level>` - 设置矩阵亮度 (0-100%)
  - `matrix pixel <x> <y> <r> <g> <b>` - 设置单个像素颜色 (坐标: 0-31, RGB: 0-255)
  - `matrix load <animation>` - 从SD卡加载动画 (如: `matrix load Logo`)
  - `matrix config save` - 保存当前矩阵配置为启动默认
  - `matrix config show` - 显示当前矩阵配置
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
- `test matrix` - 执行LED矩阵测试（显示测试图案）
- `test gpio <pin>` - 安全测试GPIO输出功能
- `test gpio_input <pin>` - 测试GPIO输入功能
- `test orin` - 测试Orin电源控制功能
- `test n305` - 测试N305电源控制功能
- `test all` - 执行完整的硬件测试序列
- `test quick` - 执行快速测试
- `test stress <ms>` - 执行指定时长的压力测试

#### 以太网控制命令
- `eth_config` - 显示当前以太网配置
  - `eth_config show` - 显示当前以太网配置
  - `eth_config reload` - 从NVS重新载入配置
- `eth_status` - 显示以太网接口详细状态信息
- `eth_reset` - 重置以太网接口
- `eth_ping <IP>` - ping测试网络连通性（例：`eth_ping 8.8.8.8`）
- `eth_test` - 测试W5500芯片SPI通信

#### DHCP服务器控制命令
- `eth_dhcp` - 显示DHCP服务器状态和客户端列表
  - `eth_dhcp status` - 显示DHCP服务器状态
  - `eth_dhcp start` - 启动DHCP服务器
  - `eth_dhcp stop` - 停止DHCP服务器
  - `eth_dhcp restart` - 重启DHCP服务器

#### 网关服务控制命令
- `eth_gateway status` - 显示网关服务状态
- `eth_gateway start` - 启动网关服务
- `eth_gateway stop` - 停止网关服务

#### TF卡存储控制命令（13个命令）

**基本管理命令**:
- `sdcard_mount [path]` - 挂载TF卡到文件系统（默认/sdcard）
- `sdcard_unmount` - 安全卸载TF卡
- `sdcard_info` - 显示TF卡详细信息（容量、使用率等）
- `sdcard_ls [path]` - 列出目录内容（增强版，显示文件大小和类型）
- `sdcard_format` - 格式化TF卡为FAT32 ⚠️危险操作

**文件操作命令**:
- `sdcard_cat <file>` - 查看文件内容
- `sdcard_write <file> <content>` - 写入内容到文件
- `sdcard_append <file> <content>` - 追加内容到文件（带时间戳）
- `sdcard_rm <file>` - 删除文件
- `sdcard_cp <src> <dst>` - 复制文件

**目录操作命令**:
- `sdcard_mkdir <dir>` - 创建目录
- `sdcard_rmdir <dir>` - 删除空目录

**信息查询命令**:
- `sdcard_stat <path>` - 显示文件/目录详细信息

> 📖 **详细使用手册**: 请参考 `markdown/SDCARD_CONSOLE_COMMANDS.md` 获取完整的TF卡命令使用指南和示例

#### LED矩阵控制快速入门

LED矩阵是一个32x32的WS2812像素阵列，提供丰富的显示功能：

```bash
# 基本操作
matrix test                     # 显示测试图案
matrix bright 30                # 设置亮度30%
matrix pixel 15 15 255 0 0      # 中心设置红色像素

# 动画加载 (需要SD卡)
matrix load Logo                # 加载Logo动画

# 配置管理
matrix config save              # 保存当前设置为启动默认
matrix config show              # 显示当前配置
```

> 📖 **LED矩阵详细指南**: 请参考 `markdown/LED_MATRIX_USAGE_GUIDE.md` 获取完整的LED矩阵使用手册

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
│   ├── ethernet_interface/     以太网接口组件
│   ├── sdcard_interface/       TF卡存储接口组件
│   └── console_interface/      控制台接口组件
├── managed_components/         托管组件
│   └── espressif__led_strip/   LED条带驱动
└── markdown/                   项目文档
    ├── PROJECT_SUMMARY.md      项目总结
    ├── CONSOLE_GUIDE.md        控制台使用指南
    ├── USB_MUX_CONTROL_GUIDE.md USB MUX控制功能指南
    ├── SDCARD_CONSOLE_COMMANDS.md TF卡控制台命令完整参考
    ├── LED_MATRIX_USAGE_GUIDE.md LED矩阵控制使用指南
    ├── LED_MATRIX_IMPLEMENTATION.md LED矩阵功能实现说明
    ├── LED_MATRIX_AUTO_STARTUP.md LED矩阵自动启动功能
    └── README_COMPONENTS.md    组件说明文档
```

## 🔧 组件架构

### 核心组件

1. **hardware_control**: 硬件抽象层，提供PWM、GPIO、LED等硬件接口
2. **system_monitor**: 系统监控，包括内存、CPU、温度等状态监控
3. **device_interface**: 统一设备接口，整合硬件控制和系统监控
4. **ethernet_interface**: 以太网接口，提供W5500网络功能、DHCP服务器和网关服务
5. **sdcard_interface**: TF卡存储接口，提供文件系统管理和存储操作
6. **console_interface**: 控制台接口，提供UART命令行交互

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

### 以太网连接问题

* 检查W5500硬件连接：确认SPI引脚连接正确
* 网络配置：使用 `eth_config` 查看当前配置
* 连接状态：使用 `eth_status` 检查以太网接口状态
* 硬件测试：使用 `eth_test` 验证W5500 SPI通信
* DHCP问题：使用 `eth_dhcp status` 检查DHCP服务器状态

### 网络连通性问题

* 使用 `eth_ping <IP>` 测试网络连通性
* 检查网线连接和网络设备状态
* 确认IP地址配置是否与网络环境匹配
* 检查防火墙设置是否阻止连接

### TF卡存储问题

* 检查TF卡硬件连接：确认SDMMC引脚连接正确（GPIO 4,5,6,7,15,16）
* 卡片兼容性：使用Class 10或更高级别的TF卡，容量建议32GB以下
* 格式检查：确保TF卡已格式化为FAT32格式
* 挂载状态：使用 `sdcard_info` 检查挂载状态和容量信息
* 文件操作：使用 `sdcard_ls` 验证文件系统是否正常
* 重新挂载：如遇问题，先 `sdcard_unmount` 再 `sdcard_mount`

## 🔗 技术支持

如有技术问题或功能需求，请通过以下渠道反馈：

* 功能请求或错误报告：创建 [GitHub issue](https://github.com/thomas-hiddenpeak/rm01-esp32s3-bsp/issues)

## 📝 许可证

本项目遵循开源许可证，详情请参考项目根目录下的LICENSE文件。

## 🤝 贡献

欢迎提交Pull Request或Issue来改进项目。在贡献代码前，请确保：

1. 代码符合项目编码规范
2. 新功能有对应的文档说明
3. 通过了基本的功能测试

---

**注意**: 本项目专为ESP32S3版本的RM-01设计，不保证在其他系统的兼容性。
