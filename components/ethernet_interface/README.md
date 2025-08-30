# Ethernet Interface Component

这个组件为ESP32S3提供W5500以太网控制功能，包括网络配置、DHCP服务器、网关服务和ping测试。

## 硬件连接

W5500芯片的SPI连接：
- RST_PIN: GPIO39 (复位引脚)
- INT_PIN: GPIO38 (中断引脚) 
- MISO_PIN: GPIO13 (SPI MISO)
- SCLK_PIN: GPIO12 (SPI 时钟)
- MOSI_PIN: GPIO11 (SPI MOSI)
- CS_PIN: GPIO10 (SPI 片选)

## 默认配置

- IP地址: 10.10.99.97
- 网关: 10.10.99.100  
- 子网掩码: 255.255.255.0
- DNS服务器: 8.8.8.8
- DHCP池: 10.10.99.100 - 10.10.99.102
- 租约时间: 24小时

## 控制台命令

### 1. 网络配置

```bash
# 查看当前配置
eth_config

# 设置网络配置
eth_config <ip> <gateway> <netmask> <dns>
# 示例: eth_config 10.10.99.97 10.10.99.100 255.255.255.0 8.8.8.8
```

### 2. DHCP服务器控制

```bash
# 查看DHCP状态
eth_dhcp

# 启用/禁用DHCP服务器
eth_dhcp enable
eth_dhcp disable

# 设置DHCP地址池 (默认24小时租约)
eth_dhcp pool <start_ip> <end_ip>
# 示例: eth_dhcp pool 10.10.99.100 10.10.99.102

# 设置DHCP地址池和租约时间
eth_dhcp pool <start_ip> <end_ip> <lease_hours>
# 示例: eth_dhcp pool 10.10.99.100 10.10.99.105 12
```

### 3. 网关服务控制

```bash
# 查看网关状态
eth_gateway

# 启用/禁用网关服务
eth_gateway enable   # ESP32S3作为网关
eth_gateway disable
```

### 4. 状态查看

```bash
# 查看完整的以太网状态
eth_status
```

### 5. Ping测试

```bash
# 基本ping测试 (默认4次，1000ms超时)
eth_ping <target_ip>
# 示例: eth_ping 8.8.8.8

# 指定ping次数
eth_ping <target_ip> <count>
# 示例: eth_ping 8.8.8.8 10

# 指定ping次数和超时时间
eth_ping <target_ip> <count> <timeout_ms>
# 示例: eth_ping 8.8.8.8 5 2000
```

### 6. 重置接口

```bash
# 重置以太网接口
eth_reset
```

## 功能说明

### 网络配置
- 支持静态IP配置
- 自动保存配置到NVS闪存
- 重启后自动恢复配置

### DHCP服务器
- 可配置IP地址池范围
- 可调整租约时间
- 支持动态启用/禁用

### 网关服务
- 启用后ESP32S3充当网关角色
- 可配合DHCP服务器使用
- 支持数据包转发功能

### 状态监控
- 实时显示连接状态
- 网络统计信息
- MAC地址显示
- DHCP客户端计数

### Ping测试
- 支持指定目标IP
- 可配置测试次数和超时
- 显示详细的延迟统计

## 事件回调

组件支持事件回调机制，可以监听以下事件：
- ETH_STATUS_DISCONNECTED: 网络断开
- ETH_STATUS_CONNECTED: 网络连接
- ETH_STATUS_GOT_IP: 获取IP地址
- ETH_STATUS_DHCP_SERVER_STARTED: DHCP服务器启动
- ETH_STATUS_GATEWAY_ENABLED: 网关服务启用

## 使用示例

```c
// 初始化以太网接口
ethernet_config_t config = ETHERNET_DEFAULT_CONFIG();
ethernet_interface_init(&config);

// 注册事件回调
ethernet_register_event_callback(my_event_handler);

// 启动接口
ethernet_interface_start();

// 启用DHCP服务器
ethernet_set_dhcp_server(true);

// 启用网关服务
ethernet_set_gateway(true);
```

## 注意事项

1. **硬件连接**: 确保W5500芯片正确连接到指定的GPIO引脚
2. **SPI总线**: W5500使用SPI2_HOST，请确保没有其他设备占用
3. **电源**: W5500需要3.3V供电，确保电源稳定
4. **网线**: 需要连接以太网线才能正常工作
5. **IP冲突**: 避免IP地址与网络中其他设备冲突

## 故障排除

### 无法获取IP地址
1. 检查硬件连接
2. 确认网线连接正常
3. 使用 `eth_reset` 重置接口
4. 检查IP配置是否正确

### DHCP服务器无法启动
1. 确认以太网接口已获取IP地址
2. 检查DHCP地址池配置
3. 确认没有IP地址冲突

### Ping测试失败
1. 确认网络连接正常
2. 检查目标IP地址是否可达
3. 尝试ping网关地址进行测试
