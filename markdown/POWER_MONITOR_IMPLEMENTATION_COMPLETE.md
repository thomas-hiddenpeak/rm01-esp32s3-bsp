# 电源监控功能实现完成

本文档记录了为ESP32S3项目实现的电源监控功能。

## 功能概述

根据您的需求，实现了以下电源监控功能：

1. **GPIO18** - 供电电压监测（使用ADC2_CHANNEL_7）
2. **GPIO47** - 电源诱骗芯片UART数据接收口（使用UART1）
3. **UART读取** - 从电源诱骗芯片读取协商数据

## 硬件配置

### GPIO配置
- **GPIO18**: 供电电压监测引脚，连接到ADC2_CHANNEL_7
- **GPIO47**: 电源诱骗芯片UART接收引脚，连接到UART1_RX

### UART配置
- **端口**: UART_NUM_1
- **波特率**: 9600 (根据电源芯片规格)
- **数据位**: 8
- **停止位**: 1
- **校验位**: 无

## 实现的功能

### 1. 电压监控
- 实时读取GPIO18上的供电电压
- ADC采样和校准
- 电压变化检测和阈值触发

### 2. 电源芯片数据读取
- 通过UART从GPIO47接收电源诱骗芯片的协商数据
- 支持4字节数据格式解析：`[0xFF帧头][电压][电流][CRC]`
- 实际数据格式示例：`0xFF 0x1C 0x32 0x??`
- CRC8校验确保数据完整性

### 3. 监控任务
- 后台任务持续监控电压变化
- 当检测到电压变化时自动触发数据读取
- 可配置的监控间隔和阈值

## 控制台命令

电源监控系统提供完整的控制台命令接口，使用 `power help` 查看详细帮助。

### 基本命令
```
power status                   - 显示完整电源系统状态
power voltage                  - 读取供电电压 (GPIO18 ADC)
power read [timeout_ms]        - 读取电源芯片数据 (GPIO47 UART)
power chip [timeout_ms]        - 同read命令，读取电源芯片数据
```

### 监控控制
```
power start                    - 启动后台电源监控任务
power stop                     - 停止后台电源监控任务
power threshold <value>        - 设置电压变化阈值 (单位:V)
```

**电压变化阈值说明:**
- **作用**: 当供电电压变化超过设定阈值时，后台监控任务会自动触发电源芯片数据读取
- **默认值**: 1.0V (可在hardware_config.h中配置，调整为较大值减少干扰误触发)
- **推荐范围**: 0.5V - 2.0V
- **触发行为**: 
  - 控制台显示电压变化提示
  - 自动执行`power read`获取最新电源数据
  - 日志记录变化详情和读取结果
- **使用场景**: 
  - 负载变化监控
  - 电源状态自动跟踪
  - 异常电压波动检测
- **干扰抑制**: 较大阈值可有效避免电磁干扰、测量噪声等造成的误触发

### 调试工具
```
power debug                    - 显示UART配置和状态信息
power test                     - 执行10秒UART接收测试
power analyze [timeout_ms]     - 深度分析UART数据协议
power help                     - 显示此帮助信息
```

### 使用示例
```bash
# 基本操作
power voltage                  # 读取GPIO18供电电压
power read                     # 使用默认2秒超时读取芯片数据
power status                   # 显示完整系统状态

# 高级操作
power read 5000                # 使用5秒超时读取芯片数据
power threshold 0.1            # 设置0.1V电压变化阈值
power start                    # 启动后台监控任务

# 调试功能
power debug                    # 检查UART初始化状态
power test                     # 执行UART接收测试
power analyze 10000            # 执行10秒数据协议分析
```

### 命令详细说明

- **power voltage**: 读取GPIO18上的供电电压
- **power read**: 通过GPIO47的UART接收电源诱骗芯片数据，可选超时参数(毫秒)
- **power chip**: `read`命令的别名
- **power status**: 显示完整的电源监控状态，包括电压和芯片数据
- **power start/stop**: 控制后台监控任务的启动和停止
- **power threshold**: 设置电压变化阈值，用于触发自动数据读取
- **power debug**: 显示UART配置信息和1秒数据接收测试
- **power test**: 执行10秒的UART数据接收测试，用于诊断连接问题

## API接口

### 初始化和清理
```c
esp_err_t power_monitor_init(void);
esp_err_t power_monitor_deinit(void);
```

### 电压读取
```c
float power_get_supply_voltage(void);
esp_err_t power_get_voltage_data(voltage_monitor_data_t *data);
```

### 电源芯片数据
```c
esp_err_t power_chip_read_data(uint32_t timeout_ms);
esp_err_t power_get_chip_data(power_chip_data_t *data);
```

### 配置和状态
```c
esp_err_t power_set_voltage_threshold(float threshold);
esp_err_t power_print_status(void);
```

## 数据格式

### 电压监控数据
```c
typedef struct {
    float supply_voltage;       // 供电电压 (V)
    uint32_t timestamp;         // 数据时间戳 (毫秒)
} voltage_monitor_data_t;
```

### 电源芯片数据
```c
typedef struct {
    float voltage;              // 电压 (V)
    float current;              // 电流 (A)  
    float power;                // 功率 (W)
    uint32_t timestamp;         // 数据时间戳
    bool valid;                 // 数据有效标志
} power_chip_data_t;
```

## 配置参数

在 `hardware_config.h` 中可以调整以下参数：

```c
#define POWER_SUPPLY_VOLTAGE_PIN    18      // 供电电压监测引脚
#define POWER_CHIP_UART_RX_PIN      47      // UART接收引脚
#define POWER_CHIP_UART_BAUDRATE    9600    // UART波特率 (电源芯片规格)
#define VOLTAGE_MONITOR_INTERVAL_MS 5000    // 监控间隔
#define VOLTAGE_CHANGE_THRESHOLD    1.0     // 电压变化阈值 (调整为1V减少干扰)
```

## 使用示例

### 基本电压读取
```c
// 读取当前供电电压
float voltage = power_get_supply_voltage();
printf("供电电压: %.2fV\n", voltage);
```

### 读取电源芯片数据
```c
// 触发数据读取
esp_err_t ret = power_chip_read_data(1000); // 1秒超时
if (ret == ESP_OK) {
    power_chip_data_t data;
    if (power_get_chip_data(&data) == ESP_OK && data.valid) {
        printf("电压: %.2fV, 电流: %.3fA, 功率: %.2fW\n", 
               data.voltage, data.current, data.power);
    }
}
```

## 注意事项

1. **只读操作**: GPIO47只用于UART接收，不发送数据
2. **电压范围**: ADC输入电压范围为0-3.3V
3. **数据有效性**: 电源芯片数据使用自定义4字节格式，确保数据完整性
4. **任务优先级**: 监控任务运行在较低优先级，不会影响主要功能

## 故障排除

### 电压读取为0
- 检查GPIO18连接
- 确认ADC初始化成功
- 检查输入电压范围

### UART数据读取失败
- 检查GPIO47连接
- 确认波特率设置正确
- 检查UART初始化状态

### CRC校验失败
- 检查数据传输完整性
- 确认数据格式符合XSP16协议
- 检查干扰和噪声

### 调试步骤

当遇到数据解析错误时，按以下步骤调试：

1. **检查UART状态**
   ```
   power debug
   ```
   查看UART是否正确初始化，配置是否正确

2. **测试数据接收**
   ```
   power test
   ```
   运行10秒测试，查看是否能接收到任何数据

3. **检查数据格式**
   如果收到数据但格式不正确，检查：
   - 电源诱骗芯片的实际数据格式 (当前期望: 0xFF帧头格式)
   - 波特率是否匹配（当前设置：9600）
   - 数据是否包含0xFF包头

4. **硬件连接检查**
   - 确认GPIO47正确连接到电源芯片的输出
   - 检查电平匹配（3.3V逻辑电平）
   - 验证接地连接

## 实现完成

电源监控功能已完全实现并集成到项目中，支持：
- ✅ GPIO18供电电压监测
- ✅ GPIO47 UART数据接收
- ✅ 电源芯片协商数据读取
- ✅ 控制台命令接口
- ✅ 后台监控任务
- ✅ 完整的错误处理

所有功能已经过编译验证，可以正常使用。
