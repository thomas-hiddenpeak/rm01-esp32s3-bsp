# AGX系统监控功能使用指南

本文档介绍ESP32S3-BSP中新增的AGX系统监控功能，包括网络连接检查和系统性能监控。

## 功能概述

AGX系统监控功能提供两个层面的监控：

1. **网络连接监控**：通过ping检查AGX设备网络接口是否正常工作
2. **系统性能监控**：通过HTTP API获取AGX设备的详细运行状态信息

## 配置信息

- AGX设备IP地址：`10.10.99.98`
- Metrics API端点：`http://10.10.99.98:59100/metrics`
- Ping超时时间：3000ms
- HTTP请求超时：10000ms

## 控制台命令

### AGX命令扩展

原有的`agx`命令现在支持更多的监控功能：

```bash
# 基本电源控制
agx on          # 开机AGX设备
agx off         # 关机AGX设备
agx reset       # 重启AGX设备
agx recovery    # 进入恢复模式
agx status      # 显示AGX电源状态

# 新增监控功能
agx monitor     # 完整的AGX系统监控检查(网络+metrics)
agx ping        # 仅检查AGX网络连接状态
agx metrics     # 仅获取AGX系统运行状态信息
```

### 系统状态命令

`status`命令现在会包含AGX监控信息：

```bash
status
```

输出示例：
```
=== 当前状态 ===
风扇速度: 50%
板载LED亮度: 50%
触摸LED亮度: 50%
AGX电源状态: 开启
LPMU电源状态: 关闭

--- AGX监控状态 ---
网络状态: 连通
Ping响应: 2 ms
Metrics可用: 是
检查次数: 5
CPU使用率: 12.5%
内存使用率: 15.6%
CPU温度: 45.7°C

--- 系统监控 ---
可用堆内存: 234567 bytes
运行时间: 123456 ms
=================
```

## API接口

### 函数接口

```c
// 检查AGX网络连接状态
esp_err_t agx_check_network_status(void);

// 获取AGX系统metrics信息
esp_err_t agx_get_metrics(void);

// 执行完整的AGX系统监控检查
esp_err_t agx_monitor_check(void);

// 获取AGX监控状态
esp_err_t agx_get_monitor_status(agx_monitor_status_t *monitor_status);

// 打印AGX监控状态
esp_err_t agx_print_monitor_status(void);

// 获取网络状态字符串
const char* agx_get_network_status_name(agx_net_status_t status);
```

### 数据结构

```c
typedef enum {
    AGX_NET_STATUS_UNKNOWN = 0,     ///< 未知状态
    AGX_NET_STATUS_DOWN,            ///< 网络断开
    AGX_NET_STATUS_UP,              ///< 网络连通
    AGX_NET_STATUS_ERROR            ///< 网络错误
} agx_net_status_t;

typedef struct {
    agx_net_status_t network_status;    ///< 网络连接状态
    uint32_t last_ping_time_ms;         ///< 最后ping响应时间(ms)
    bool metrics_available;             ///< metrics API是否可用
    uint64_t last_check_time;           ///< 最后检查时间戳
    uint32_t check_count;               ///< 检查次数
    uint32_t network_error_count;       ///< 网络错误计数
    uint32_t metrics_error_count;       ///< metrics错误计数
    
    // 从metrics API获取的系统信息
    float cpu_usage_percent;            ///< CPU使用率 (%)
    float gpu_usage_percent;            ///< GPU使用率 (%)
    uint64_t memory_total_kb;           ///< 总内存 (KB)
    uint64_t memory_used_kb;            ///< 已用内存 (KB)
    float memory_usage_percent;         ///< 内存使用率 (%)
    float disk_total_gb;                ///< 磁盘总大小 (GB)
    float disk_used_gb;                 ///< 磁盘已用大小 (GB)
    float disk_usage_percent;           ///< 磁盘使用率 (%)
    float temperature_cpu;              ///< CPU温度 (℃)
    float temperature_gpu;              ///< GPU温度 (℃)
    float total_power_mw;               ///< 总功耗 (mW)
    float uptime_seconds;               ///< 运行时间 (秒)
} agx_monitor_status_t;
```

## 监控数据解析

AGX监控功能从Prometheus metrics格式的数据中解析以下信息：

### CPU信息
- **CPU使用率**：计算所有CPU核心的平均使用率
- **CPU温度**：从`temperature_C{statistic="cpu"}`获取

### GPU信息
- **GPU使用率**：从`gpu_utilization_percentage_Hz`获取
- **GPU温度**：从`temperature_C{statistic="gpu"}`获取

### 内存信息
- **总内存**：从`ram_kB{statistic="total"}`获取
- **已用内存**：从`ram_kB{statistic="used"}`获取
- **内存使用率**：计算已用内存/总内存的百分比

### 磁盘信息
- **磁盘总大小**：从`disk_GB{mountpoint="total"}`获取
- **磁盘已用大小**：从`disk_GB{mountpoint="used"}`获取
- **磁盘使用率**：计算已用空间/总空间的百分比

### 功耗信息
- **总功耗**：从`integrated_power_mW{statistic="power"}`获取

### 运行时间
- **系统运行时间**：从`uptime_s{statistic="alive"}`获取

## 使用示例

### 1. 基本网络检查

```bash
agx ping
```

这将检查AGX设备的网络连接状态，如果连接正常，会显示ping响应时间。

### 2. 获取系统性能信息

```bash
agx metrics
```

这将从AGX设备获取详细的系统性能信息，包括CPU、GPU、内存、磁盘使用情况等。

### 3. 完整监控检查

```bash
agx monitor
```

这将执行完整的监控检查，包括网络连接和系统性能信息获取。

### 4. 查看详细状态

```bash
status
```

这将显示包含AGX监控信息在内的系统完整状态。

## 错误处理

### 网络连接失败
- 检查AGX设备是否已开机
- 检查网络连接是否正常
- 确认AGX设备IP地址配置

### Metrics获取失败
- 确认AGX设备上的metrics服务是否运行在59100端口
- 检查防火墙设置
- 确认HTTP服务是否正常响应

### 数据解析错误
- 检查metrics数据格式是否符合Prometheus标准
- 确认所需的metric名称是否存在于响应数据中

## 集成到其他系统

AGX监控功能已集成到`hardware_status_t`结构中，可以通过设备接口的`device_get_full_status()`函数获取完整的系统状态，包括AGX监控信息。

这使得其他组件可以轻松访问AGX监控数据，用于系统状态报告、告警、或自动化控制。

## 性能考虑

- 网络ping操作通常在几毫秒内完成
- HTTP metrics获取可能需要几秒钟时间，取决于网络延迟和AGX设备响应时间
- 建议不要过于频繁地调用监控函数，以避免对网络和AGX设备造成压力
- 监控数据会保存在内存中，直到下次更新

## 故障排除

### 常见问题

1. **网络连接超时**
   - 检查AGX设备是否开机并完成启动
   - 确认网络配置正确
   - 检查网络电缆连接

2. **Metrics API无响应**
   - 确认AGX设备上运行了相应的监控服务
   - 检查59100端口是否被防火墙阻止
   - 尝试手动访问metrics URL确认服务状态

3. **数据解析失败**
   - 检查AGX设备metrics服务版本是否兼容
   - 确认返回的数据格式正确

通过这些监控功能，您可以实时了解AGX设备的运行状态，及时发现和解决问题。
