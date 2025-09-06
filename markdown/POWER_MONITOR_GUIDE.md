# 电源监控功能使用指南

## 功能概述

本项目新增了电源监控功能，用于监测供电电压和读取电源诱骗芯片的协商数据。

## 硬件配置

- **GPIO18**: 供电电压监测引脚 - 通过ADC2_CHANNEL_7监测供电电压
- **GPIO47**: 电源诱骗芯片数据读取引脚 - 通过ADC1_CHANNEL_6监测芯片数据引脚电压
- **GPIO21**: 电源诱骗芯片UART RX引脚 - 通过UART1读取电源协商数据

## 配置参数

在 `hardware_config.h` 中定义了以下配置参数：

```c
// 电源监控配置
#define POWER_VOLTAGE_MONITOR_PIN    18     // 供电电压监测引脚
#define POWER_CHIP_DATA_PIN          47     // 电源诱骗芯片数据读取引脚

// 电源芯片UART配置
#define POWER_CHIP_UART_NUM          UART_NUM_1    // 电源芯片UART端口
#define POWER_CHIP_UART_RX_PIN       21            // 电源芯片UART接收引脚
#define POWER_CHIP_UART_BAUDRATE     9600          // 电源芯片UART波特率

// 电压监控配置
#define VOLTAGE_MONITOR_INTERVAL_MS  5000          // 电压监控间隔(毫秒)
#define VOLTAGE_CHANGE_THRESHOLD     0.2           // 电压变化阈值(V)
```

## 控制台命令

### power 命令

```
power status        - 显示完整的电源监控状态
power voltage       - 显示当前电压值
power chip [timeout] - 读取电源芯片数据（可选超时时间，默认2秒）
power start         - 启动电源监控任务
power stop          - 停止电源监控任务
power threshold <value> - 设置电压变化阈值
```

### 使用示例

```bash
# 查看电源监控状态
power status

# 查看当前电压
power voltage

# 读取电源芯片数据
power chip

# 读取电源芯片数据（3秒超时）
power chip 3000

# 设置电压变化阈值为0.1V
power threshold 0.1
```

## API接口

### 初始化和反初始化

```c
esp_err_t power_monitor_init(void);
esp_err_t power_monitor_deinit(void);
```

### 电压读取

```c
float power_get_supply_voltage(void);      // 读取供电电压
float power_get_chip_data_voltage(void);   // 读取芯片数据引脚电压
```

### 电源芯片数据

```c
esp_err_t power_chip_read_data(uint32_t timeout_ms);  // 读取芯片协商数据
esp_err_t power_get_chip_data(power_chip_data_t *data); // 获取最新数据
```

### 监控任务

```c
esp_err_t power_monitor_start_task(void);  // 启动监控任务
esp_err_t power_monitor_stop_task(void);   // 停止监控任务
bool power_check_voltage_change(void);     // 检查电压变化
```

## 数据结构

### 电源芯片数据

```c
typedef struct {
    bool valid;                 // 数据有效性
    float voltage;              // 电压 (V)
    float current;              // 电流 (A)
    float power;                // 功率 (W)
    uint32_t timestamp;         // 数据时间戳 (毫秒)
} power_chip_data_t;
```

### 电压监控数据

```c
typedef struct {
    float supply_voltage;       // 供电电压 (V)
    float chip_data_voltage;    // 芯片数据引脚电压 (V)
    uint32_t timestamp;         // 数据时间戳 (毫秒)
} voltage_monitor_data_t;
```

## 工作原理

1. **电压监控**: 通过ADC实时监测GPIO18和GPIO47的电压值
2. **变化检测**: 当电压变化超过设定阈值时，触发电源芯片数据读取
3. **UART通信**: 通过UART1从电源诱骗芯片读取协商数据
4. **数据解析**: 按照XSP16协议解析电源芯片返回的数据
5. **CRC校验**: 使用Maxim/Dallas CRC8算法验证数据完整性

## 协议格式

电源芯片数据采用XSP16协议格式：
- 包头: 0xFF
- 电压: 1字节 (0-255 映射到 0-20V)
- 电流: 1字节 (0-255 映射到 0-5A)
- CRC校验: 1字节 (Maxim/Dallas CRC8)

## 注意事项

1. 电源监控功能会在系统初始化时自动启动
2. 监控任务默认每5秒检查一次电压变化
3. 电压变化阈值默认为0.2V，可以通过控制台命令调整
4. UART只配置了RX引脚，用于接收电源芯片的数据
5. ADC使用12位精度，支持0-3.3V输入范围

## 故障排除

### 电压读取失败
- 检查GPIO18和GPIO47的硬件连接
- 确认ADC初始化是否成功

### UART数据读取超时
- 检查GPIO21的UART连接
- 确认电源芯片是否正常工作
- 检查波特率设置是否正确(9600)

### CRC校验失败
- 检查UART数据传输是否稳定
- 确认电源芯片协议是否为XSP16格式
