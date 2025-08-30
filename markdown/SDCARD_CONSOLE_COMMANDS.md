# TF卡控制台命令完整参考

## 命令列表

ESP32S3 BSP项目提供了 **13个TF卡操作命令**，分为以下几类：

### 🔧 基本管理命令 (5个)

| 命令 | 参数 | 功能 | 示例 |
|------|------|------|------|
| `sdcard_mount` | `[mount_point]` | 挂载TF卡到文件系统 | `sdcard_mount /sdcard` |
| `sdcard_unmount` | 无 | 安全卸载TF卡 | `sdcard_unmount` |
| `sdcard_info` | 无 | 显示TF卡详细信息 | `sdcard_info` |
| `sdcard_ls` | `[path]` | 列出目录内容（增强版） | `sdcard_ls /sdcard/logs` |
| `sdcard_format` | 无 | 格式化TF卡 ⚠️危险 | `sdcard_format` |

### 📄 文件操作命令 (5个)

| 命令 | 参数 | 功能 | 示例 |
|------|------|------|------|
| `sdcard_cat` | `<file_path>` | 查看文件内容 | `sdcard_cat /sdcard/config.txt` |
| `sdcard_write` | `<file_path> <content>` | 写入内容到文件 | `sdcard_write /sdcard/test.txt "Hello"` |
| `sdcard_append` | `<file_path> <content>` | 追加内容（带时间戳） | `sdcard_append /sdcard/log.txt "启动"` |
| `sdcard_rm` | `<file_path>` | 删除文件 | `sdcard_rm /sdcard/temp.txt` |
| `sdcard_cp` | `<src> <dst>` | 复制文件 | `sdcard_cp /sdcard/a.txt /sdcard/b.txt` |

### 📁 目录操作命令 (2个)

| 命令 | 参数 | 功能 | 示例 |
|------|------|------|------|
| `sdcard_mkdir` | `<dir_path>` | 创建目录 | `sdcard_mkdir /sdcard/logs` |
| `sdcard_rmdir` | `<dir_path>` | 删除空目录 | `sdcard_rmdir /sdcard/temp` |

### 📊 信息查询命令 (1个)

| 命令 | 参数 | 功能 | 示例 |
|------|------|------|------|
| `sdcard_stat` | `<path>` | 显示文件/目录详细信息 | `sdcard_stat /sdcard/config.txt` |

---

## 详细命令说明

### 1. sdcard_mount - 挂载TF卡
```bash
用法: sdcard_mount [mount_point]
```
**功能**: 初始化SDMMC接口并挂载TF卡到指定挂载点

**参数**:
- `mount_point` (可选): 挂载点路径，默认为 `/sdcard`

**输出示例**:
```
esp32> sdcard_mount
SD卡挂载成功到: /sdcard
SD卡信息:
  名称: SA16G
  类型: SDHC
  容量: 14.83 MB
  扇区大小: 512 字节
  总空间: 14.83 MB
  可用空间: 14.20 MB
  使用率: 4.3%
```

### 2. sdcard_unmount - 卸载TF卡
```bash
用法: sdcard_unmount
```
**功能**: 安全卸载TF卡文件系统，确保数据完整性

**输出示例**:
```
esp32> sdcard_unmount
SD卡卸载成功
```

### 3. sdcard_info - 显示TF卡信息
```bash
用法: sdcard_info
```
**功能**: 显示TF卡的完整状态和信息

**输出示例**:
```
esp32> sdcard_info
SD卡状态: 已挂载
SD卡详细信息:
  名称: SA16G
  类型: SDHC
  容量: 14.83 MB (14.83 GB)
  扇区大小: 512 字节
  总扇区数: 30218752
  挂载点: /sdcard
空间信息:
  总空间: 14.83 MB
  已用空间: 0.63 MB
  可用空间: 14.20 MB
  使用率: 4.3%
```

### 4. sdcard_ls - 列出目录内容（增强版）
```bash
用法: sdcard_ls [path]
```
**功能**: 以表格格式列出目录内容，显示文件大小和类型

**参数**:
- `path` (可选): 目录路径，默认为 `/sdcard`

**输出示例**:
```
esp32> sdcard_ls /sdcard
列出目录: /sdcard
名称                 大小 类型
----------------------------------------
config.txt            256 文件
logs                <DIR> 目录
temp.bin             1024 文件
backup              <DIR> 目录
----------------------------------------
总计: 2 个文件, 2 个目录
```

### 5. sdcard_format - 格式化TF卡
```bash
用法: sdcard_format
```
**功能**: 格式化TF卡为FAT32格式 ⚠️ **危险操作**

**交互确认**:
```
esp32> sdcard_format
警告: 格式化将删除SD卡上的所有数据！
如果确定要格式化，请输入 'YES': YES
正在格式化SD卡...
SD卡格式化成功
```

### 6. sdcard_cat - 查看文件内容
```bash
用法: sdcard_cat <file_path>
```
**功能**: 显示文本文件的完整内容

**输出示例**:
```
esp32> sdcard_cat /sdcard/config.txt
文件内容: /sdcard/config.txt
----------------------------------------
wifi_ssid=MyWiFi
wifi_password=12345678
device_name=ESP32S3-Device
log_level=INFO
----------------------------------------
```

### 7. sdcard_write - 写入文件
```bash
用法: sdcard_write <file_path> <content>
```
**功能**: 创建新文件并写入内容（覆盖模式）

**示例**:
```
esp32> sdcard_write /sdcard/test.txt "Hello ESP32S3"
内容已写入文件: /sdcard/test.txt
```

### 8. sdcard_append - 追加到文件
```bash
用法: sdcard_append <file_path> <content>
```
**功能**: 在文件末尾追加内容，自动添加时间戳

**示例**:
```
esp32> sdcard_append /sdcard/system.log "系统启动完成"
内容已追加到文件: /sdcard/system.log
```

**生成的日志格式**:
```
[2025-08-31 14:30:25] 系统启动完成
```

### 9. sdcard_rm - 删除文件
```bash
用法: sdcard_rm <file_path>
```
**功能**: 删除指定文件，自动检查文件类型

**示例**:
```
esp32> sdcard_rm /sdcard/temp.txt
文件已删除: /sdcard/temp.txt
```

**错误处理**:
```
esp32> sdcard_rm /sdcard/logs
错误: /sdcard/logs 是目录，请使用 sdcard_rmdir 删除目录
```

### 10. sdcard_cp - 复制文件
```bash
用法: sdcard_cp <src_file> <dst_file>
```
**功能**: 复制文件到新位置，显示传输统计

**示例**:
```
esp32> sdcard_cp /sdcard/config.txt /sdcard/backup/config.bak
文件复制成功: /sdcard/config.txt -> /sdcard/backup/config.bak (256 字节)
```

### 11. sdcard_mkdir - 创建目录
```bash
用法: sdcard_mkdir <dir_path>
```
**功能**: 创建新目录

**示例**:
```
esp32> sdcard_mkdir /sdcard/logs
目录已创建: /sdcard/logs
```

### 12. sdcard_rmdir - 删除空目录
```bash
用法: sdcard_rmdir <dir_path>
```
**功能**: 删除空目录，安全检查

**示例**:
```
esp32> sdcard_rmdir /sdcard/temp
目录已删除: /sdcard/temp
```

**错误处理**:
```
esp32> sdcard_rmdir /sdcard/logs
删除目录失败: /sdcard/logs (目录可能不为空)
```

### 13. sdcard_stat - 查看文件/目录详情
```bash
用法: sdcard_stat <path>
```
**功能**: 显示文件或目录的详细信息

**输出示例**:
```
esp32> sdcard_stat /sdcard/config.txt
文件信息: /sdcard/config.txt
----------------------------------------
类型: 文件
大小: 256 字节
修改时间: Sat Aug 31 14:30:25 2025
权限: 644
```

---

## 命令使用流程

### 典型使用流程
```bash
# 1. 挂载TF卡
esp32> sdcard_mount
SD卡挂载成功到: /sdcard

# 2. 查看当前内容
esp32> sdcard_ls
列出目录: /sdcard
...

# 3. 创建目录结构
esp32> sdcard_mkdir /sdcard/config
esp32> sdcard_mkdir /sdcard/logs
esp32> sdcard_mkdir /sdcard/data

# 4. 创建配置文件
esp32> sdcard_write /sdcard/config/wifi.conf "ssid=MyWiFi"
esp32> sdcard_append /sdcard/config/wifi.conf "password=12345678"

# 5. 查看配置
esp32> sdcard_cat /sdcard/config/wifi.conf

# 6. 记录日志
esp32> sdcard_append /sdcard/logs/system.log "设备初始化完成"

# 7. 备份重要文件
esp32> sdcard_cp /sdcard/config/wifi.conf /sdcard/config/wifi.bak

# 8. 查看文件详情
esp32> sdcard_stat /sdcard/config/wifi.conf

# 9. 清理临时文件
esp32> sdcard_rm /sdcard/temp.txt

# 10. 安全卸载
esp32> sdcard_unmount
```

---

## 错误处理

### 常见错误信息

1. **TF卡未挂载**:
   ```
   SD卡未挂载，请先执行 'sdcard_mount'
   ```

2. **文件不存在**:
   ```
   无法打开文件: /sdcard/nonexistent.txt
   ```

3. **权限错误**:
   ```
   创建目录失败: /sdcard/readonly
   ```

4. **参数错误**:
   ```
   用法: sdcard_write <文件路径> <内容>
   ```

5. **空间不足**:
   ```
   写入文件失败
   ```

### 故障排除步骤

1. **检查挂载状态**: `sdcard_info`
2. **查看剩余空间**: `sdcard_info`
3. **检查文件权限**: `sdcard_stat <file>`
4. **重新挂载**: `sdcard_unmount` 然后 `sdcard_mount`

---

## 性能和限制

### 性能特征
- **读取速度**: ~2-10 MB/s (取决于TF卡)
- **写入速度**: ~1-5 MB/s (取决于TF卡)
- **最大文件大小**: 4GB (FAT32限制)
- **最大目录深度**: 无限制

### 使用限制
- 单线程访问（需要外部同步）
- 不支持符号链接
- 文件名限制（FAT32规范）
- 同时打开文件数限制：5个

---

## 最佳实践

1. **总是先挂载**: 使用任何文件操作前先执行 `sdcard_mount`
2. **安全卸载**: 操作完成后执行 `sdcard_unmount`
3. **定期备份**: 使用 `sdcard_cp` 备份重要文件
4. **监控空间**: 定期使用 `sdcard_info` 检查剩余空间
5. **使用追加日志**: 使用 `sdcard_append` 记录带时间戳的日志
6. **目录组织**: 使用 `sdcard_mkdir` 创建清晰的目录结构
7. **错误检查**: 注意命令返回的错误信息
