# DHCP服务器命令完整指南

## 概述
ESP32S3 DHCP服务器提供完整的网络地址分配和IP-MAC地址绑定功能，支持静态预留、客户端管理和实时监控。

## 基础DHCP控制命令

### 服务器状态管理
```bash
# 显示DHCP服务器状态和客户端列表
eth_dhcp
eth_dhcp status

# 启动DHCP服务器
eth_dhcp start
eth_dhcp enable

# 停止DHCP服务器
eth_dhcp stop
eth_dhcp disable

# 重启DHCP服务器
eth_dhcp restart
```

### 客户端列表查看
```bash
# 显示当前活跃的DHCP客户端
eth_dhcp list

# 显示所有IP-MAC地址绑定预留
eth_dhcp reservations
```

## IP-MAC地址绑定管理

### 添加静态IP预留
```bash
# 基本语法
eth_dhcp reserve <mac_addr> <ip_addr> [description]

# 示例：为主控设备预留IP
eth_dhcp reserve aa:bb:cc:dd:ee:ff 10.10.99.100 "主控设备"

# 示例：为Docker容器预留IP
eth_dhcp reserve 02:42:ac:11:00:02 10.10.99.101 "Docker容器"

# 示例：无描述预留
eth_dhcp reserve 08:00:27:12:34:56 10.10.99.102
```

### 删除IP预留
```bash
# 删除指定MAC地址的IP预留
eth_dhcp unreserve <mac_addr>

# 示例
eth_dhcp unreserve aa:bb:cc:dd:ee:ff
```

### 批量管理
```bash
# 清除所有IP-MAC地址绑定（需要确认）
eth_dhcp clear
```

## 地址池配置

### 配置DHCP地址池
```bash
# 基本语法
eth_dhcp pool <start_ip> <end_ip> [lease_hours]

# 示例：配置地址池（默认24小时租约）
eth_dhcp pool 10.10.99.100 10.10.99.105

# 示例：配置地址池和租约时间（12小时）
eth_dhcp pool 10.10.99.100 10.10.99.105 12
```

## 客户端租约管理

### 释放DHCP租约
```bash
# 释放指定MAC地址的DHCP租约
eth_dhcp release <mac_addr>

# 示例
eth_dhcp release aa:bb:cc:dd:ee:ff
```

## 实际使用场景

### 场景1：为AGX设备分配固定IP
```bash
# 1. 查看当前客户端
eth_dhcp list

# 2. 为AGX设备预留IP
eth_dhcp reserve 48:b0:2d:xx:xx:xx 10.10.99.100 "AGX主控设备"

# 3. 如果设备已连接，释放当前租约让其重新获取
eth_dhcp release 48:b0:2d:xx:xx:xx

# 4. 验证预留配置
eth_dhcp reservations
```

### 场景2：为Docker容器批量分配IP
```bash
# 为多个Docker容器预留连续IP
eth_dhcp reserve 02:42:ac:11:00:02 10.10.99.101 "Web服务容器"
eth_dhcp reserve 02:42:ac:11:00:03 10.10.99.102 "数据库容器"
eth_dhcp reserve 02:42:ac:11:00:04 10.10.99.103 "缓存服务容器"

# 验证配置
eth_dhcp reservations
```

### 场景3：网络故障排除
```bash
# 1. 检查DHCP服务器状态
eth_dhcp status

# 2. 查看当前客户端列表
eth_dhcp list

# 3. 查看IP预留配置
eth_dhcp reservations

# 4. 如果发现IP冲突，释放相关租约
eth_dhcp release <problematic_mac>

# 5. 重启DHCP服务器
eth_dhcp restart
```

### 场景4：系统维护和重置
```bash
# 清除所有IP预留（谨慎操作）
eth_dhcp clear

# 重新配置地址池
eth_dhcp pool 10.10.99.100 10.10.99.110 24

# 重启DHCP服务器
eth_dhcp restart
```

## 重要注意事项

### IP地址范围
- 确保预留的IP地址在配置的DHCP地址池范围内
- 避免与网关地址冲突（通常是10.10.99.97）
- 建议为特殊设备预留10.10.99.100-110范围的IP

### MAC地址格式
- 支持标准格式：`aa:bb:cc:dd:ee:ff`
- 支持Docker格式：`02:42:ac:11:00:02`
- 字母可以是大写或小写

### 配置持久化
- 所有DHCP配置自动保存到NVS闪存
- 重启后配置自动恢复
- 可使用`config save`命令手动保存

### DNS配置
- DHCP客户端将获得网关地址（10.10.99.97）作为DNS服务器
- ESP32自动转发DNS查询到8.8.8.8
- 提供透明的DNS代理服务

## 故障排除

### 常见问题

**1. IP预留不生效**
```bash
# 检查MAC地址格式是否正确
eth_dhcp reservations

# 释放现有租约
eth_dhcp release <mac_addr>

# 重启DHCP服务器
eth_dhcp restart
```

**2. 客户端无法获取IP**
```bash
# 检查DHCP服务器状态
eth_dhcp status

# 检查地址池配置
eth_dhcp list

# 重启服务器
eth_dhcp restart
```

**3. IP冲突**
```bash
# 查看当前分配情况
eth_dhcp list
eth_dhcp reservations

# 释放冲突的租约
eth_dhcp release <mac_addr>

# 调整预留配置
eth_dhcp unreserve <mac_addr>
eth_dhcp reserve <mac_addr> <new_ip> "描述"
```

## 监控和维护

### 定期检查
```bash
# 每日检查活跃客户端
eth_dhcp list

# 每周检查预留配置
eth_dhcp reservations

# 每月检查服务器状态
eth_dhcp status
```

### 性能监控
- DHCP服务器支持最多50个并发客户端
- 建议监控客户端数量，避免超出限制
- 使用`eth_status`命令查看网络接口状态

这个完整的DHCP命令指南涵盖了所有功能和使用场景，可以作为用户操作的参考文档。
