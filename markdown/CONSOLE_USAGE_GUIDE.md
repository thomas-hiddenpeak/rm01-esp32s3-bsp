# ESP32S3 控制台使用指南

## 重要说明

您的ESP32S3控制台程序已经成功升级为使用ESP-IDF Console组件的版本！这解决了之前的串口超时问题，并提供了更好的用户体验。

## 🔧 修复的问题

### 原问题：
```
--- Warning: Writing to serial is timing out. Please make sure that your application supports an interactive console...
```

### 解决方案：
- 添加了完整的控制台REPL循环
- 使用ESP-IDF标准的`linenoise`库进行行编辑
- 正确初始化终端检测和历史记录功能

## 🚀 新功能特性

### 1. 智能终端检测
程序会自动检测您的终端能力：
- 支持行编辑的终端：启用多行编辑和历史记录
- 简单终端：自动切换到兼容模式

### 2. 完整的命令行体验
- **命令历史**：上下箭头键浏览历史命令
- **行编辑**：左右箭头、Home、End、Backspace等
- **TAB补全**：自动补全命令名称
- **错误处理**：友好的错误提示信息

## 📋 使用方法

### 烧录程序
```bash
idf.py flash monitor
```

### 基本命令
启动后您会看到提示符：`ESP32S3> `

#### 系统命令
```bash
ESP32S3> help          # 查看所有可用命令
ESP32S3> info          # 显示系统信息
ESP32S3> status        # 显示当前状态
ESP32S3> reboot        # 重启系统
```

#### 风扇控制
```bash
ESP32S3> fan 50         # 设置风扇速度为50%
ESP32S3> fan on         # 打开风扇（默认50%）
ESP32S3> fan off        # 关闭风扇
```

#### 板载LED控制（28颗WS2812）
```bash
ESP32S3> bled 255 0 0   # 设置为红色
ESP32S3> bled 0 255 0   # 设置为绿色
ESP32S3> bled 0 0 255   # 设置为蓝色
ESP32S3> bled rainbow   # 彩虹效果
ESP32S3> bled bright 50 # 设置亮度为50%
ESP32S3> bled off       # 关闭LED
```

#### 触摸LED控制（1颗WS2812）
```bash
ESP32S3> tled 255 255 255  # 设置为白色
ESP32S3> tled bright 30    # 设置亮度为30%
ESP32S3> tled off          # 关闭LED
```

#### GPIO控制
```bash
ESP32S3> gpio 2 high    # 设置GPIO2为高电平
ESP32S3> gpio 2 low     # 设置GPIO2为低电平
ESP32S3> gpio 2 read    # 读取GPIO2状态
```

#### 硬件测试
```bash
ESP32S3> test fan       # 测试风扇功能（自动变速测试）
ESP32S3> test bled      # 测试板载LED（红绿蓝循环）
ESP32S3> test tled      # 测试触摸LED（红绿蓝循环）
ESP32S3> test gpio 5    # 测试GPIO5引脚
```

## 🎯 高级功能

### 1. 命令历史
- 使用**上箭头**键：查看上一条命令
- 使用**下箭头**键：查看下一条命令
- 自动保存最近100条命令

### 2. 行编辑
- **左右箭头**：移动光标
- **Home键**：跳到行首
- **End键**：跳到行尾
- **Backspace**：删除字符
- **Delete**：删除光标后字符

### 3. TAB补全
- 输入命令的前几个字符
- 按**TAB键**自动补全
- 如有多个匹配会显示候选列表

## 🔍 故障排除

### 如果控制台没有响应：
1. 确保使用正确的串口波特率（115200）
2. 检查终端软件是否支持行编辑
3. 尝试按Enter键激活控制台

### 如果命令无法识别：
1. 输入`help`查看所有可用命令
2. 检查命令拼写是否正确
3. 确保参数格式正确

### 如果硬件不响应：
1. 使用`test`命令进行硬件测试
2. 检查引脚连接是否正确
3. 确认电源供应是否充足

## 📊 系统监控

程序会自动监控：
- 堆内存使用情况
- 系统运行时间
- 硬件状态

如果内存不足会自动发出警告。

## 🛠 开发提示

### 添加新命令
在`init_console()`函数中注册新命令：
```c
const esp_console_cmd_t new_cmd = {
    .command = "newcmd",
    .help = "新命令描述",
    .func = &cmd_new,
};
ESP_ERROR_CHECK(esp_console_cmd_register(&new_cmd));
```

### 修改硬件配置
在`hardware_config.h`中修改引脚定义和参数。

现在您可以享受全新的ESP32S3控制台体验了！🎉
