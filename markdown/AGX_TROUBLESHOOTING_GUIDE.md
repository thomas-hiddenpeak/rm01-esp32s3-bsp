# AGX监控系统故障排除指南

## 问题描述
在测试AGX监控功能时，遇到了以下问题：
- `agx ping` 命令成功 - 网络连接正常
- `agx metrics` 命令失败 - HTTP连接被拒绝

## 诊断步骤

### 1. 使用新的诊断命令
现在可以使用新增的诊断功能来详细分析连接问题：

```bash
agx diagnose
```

该命令会检查：
- AGX设备的网络连接状态 (ping)
- HTTP端口59100的连接状态
- 提供详细的错误信息和建议

### 2. 分步测试
按以下顺序测试各个功能：

```bash
# 1. 基本网络连接测试
agx ping

# 2. 完整系统监控（包含网络+HTTP）
agx monitor

# 3. 单独测试HTTP metrics
agx metrics

# 4. 详细诊断
agx diagnose
```

### 3. 可能的问题原因

#### 3.1 AGX设备上的metrics服务未运行
- **症状**: ping成功，但HTTP连接失败
- **原因**: AGX设备的Prometheus metrics服务可能没有启动
- **解决方案**: 
  - 检查AGX设备上是否运行了metrics服务
  - 确认服务监听在端口59100
  - 重启AGX设备上的监控服务

#### 3.2 防火墙阻止连接
- **症状**: 连接超时或被拒绝
- **原因**: AGX设备防火墙阻止了59100端口
- **解决方案**:
  - 在AGX设备上开放59100端口
  - 检查防火墙配置

#### 3.3 IP地址配置问题
- **症状**: 间歇性连接失败
- **原因**: AGX设备IP地址可能发生变化
- **解决方案**:
  - 确认AGX设备当前IP是否为10.10.99.98
  - 检查网络配置

#### 3.4 HTTP服务配置问题
- **症状**: 连接建立但返回错误状态码
- **原因**: metrics服务配置错误
- **解决方案**:
  - 检查AGX设备上的Prometheus exporter配置
  - 验证metrics endpoint路径

## 使用新功能进行调试

### 监控状态显示
运行监控命令后，系统会显示详细的状态信息：

```bash
agx monitor
```

输出包括：
- 网络连接状态
- CPU使用率
- GPU使用率
- 内存使用情况
- 磁盘使用情况
- 温度信息
- 功耗信息

### 错误信息分析
新的实现提供了更详细的错误信息：

- **网络连接错误**: 显示ping失败的具体原因
- **HTTP连接错误**: 显示HTTP状态码和错误描述
- **数据解析错误**: 显示metrics数据格式问题

### 连接诊断
`agx diagnose` 命令提供了全面的诊断信息：

1. **网络层测试**: ping连接测试
2. **传输层测试**: TCP端口连接测试
3. **应用层测试**: HTTP服务响应测试
4. **数据层测试**: metrics数据格式验证

## 下一步建议

1. **立即测试**: 使用 `agx diagnose` 命令获取详细的连接状态
2. **检查AGX设备**: 确认metrics服务是否正常运行
3. **网络验证**: 确认网络配置和防火墙设置
4. **服务验证**: 在AGX设备上手动验证metrics endpoint

## 技术细节

### HTTP客户端配置
- 连接超时: 10秒
- 读取超时: 10秒
- 缓冲区大小: 8KB
- 用户代理: ESP32S3-BSP

### 支持的Metrics格式
系统支持Prometheus格式的以下指标：
- `cpu_usage_percent`
- `gpu_usage_percent`
- `memory_usage_percent`
- `disk_usage_percent`
- `temperature_celsius`
- `power_usage_watts`

### 错误代码说明
- `ESP_ERR_TIMEOUT`: 连接或读取超时
- `ESP_ERR_NOT_FOUND`: HTTP 404错误
- `ESP_FAIL`: 一般性连接失败
- `ESP_ERR_INVALID_RESPONSE`: 数据格式错误
