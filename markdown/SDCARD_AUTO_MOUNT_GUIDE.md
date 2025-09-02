# SD卡自动挂载功能使用指南

## 功能概述

ESP32S3 BSP项目现在支持SD卡自动挂载功能。系统启动时会自动检测是否插入了TF卡，如果检测到则立即进行挂载操作。

## 功能特性

### ✅ 自动检测
- 系统启动时自动检测TF卡是否存在
- 支持热插拔检测（在系统运行时插入卡片）
- 无需手动执行挂载命令

### ✅ 智能挂载
- 默认挂载点：`/sdcard`
- 支持自定义挂载点
- 挂载失败时不会影响系统正常启动
- 挂载成功后显示SD卡信息

### ✅ 兼容性保持
- 保持所有原有的控制台命令功能
- 支持手动挂载/卸载操作
- 与现有代码完全兼容

## 启动流程中的自动挂载

系统启动时的处理顺序：

1. **NVS初始化**
2. **配置管理器初始化**
3. **设备接口初始化**
4. **控制台接口初始化**
5. **以太网接口初始化**
6. **🆕 SD卡自动挂载** ← 新增步骤
7. **控制台命令注册**
8. **以太网启动**
9. **系统信息显示**
10. **控制台任务启动**

## 实现详情

### 新增API函数

```c
/**
 * @brief 自动检测并挂载SD卡
 * 如果检测到SD卡存在，会自动挂载到默认挂载点
 * 
 * @param mount_point 挂载点路径，如果为NULL则使用默认的"/sdcard"
 * @return esp_err_t ESP_OK成功挂载，ESP_ERR_NOT_FOUND未检测到SD卡，其他为错误码
 */
esp_err_t sdcard_auto_mount(const char* mount_point);
```

### 使用示例

#### 在代码中调用自动挂载

```c
#include "sdcard_interface.h"

// 使用默认挂载点 (/sdcard)
esp_err_t ret = sdcard_auto_mount(NULL);
if (ret == ESP_OK) {
    printf("SD卡自动挂载成功\n");
} else if (ret == ESP_ERR_NOT_FOUND) {
    printf("未检测到SD卡\n");
} else {
    printf("SD卡挂载失败: %s\n", esp_err_to_name(ret));
}

// 使用自定义挂载点
ret = sdcard_auto_mount("/storage");
```

## 日志输出示例

### 成功挂载时的日志

```
I (1234) sdcard_interface: 开始自动检测SD卡...
I (1245) sdcard_interface: SD卡接口初始化完成
I (1267) sdcard_interface: 挂载SD卡到 /sdcard
I (1289) sdcard_interface: SD卡挂载成功
I (1290) sdcard_interface: 卡名称: SD32G
I (1291) sdcard_interface: 卡类型: SDHC/SDXC
I (1292) sdcard_interface: 卡容量: 31914MB
I (1293) sdcard_interface: ✅ SD卡自动挂载成功到: /sdcard
I (1294) sdcard_interface: SD卡: SD32G, SDHC/SDXC, 31914.91 MB
I (1295) ESP32S3_MAIN: SD卡自动挂载成功
```

### 未检测到SD卡时的日志

```
I (1234) sdcard_interface: 开始自动检测SD卡...
D (1245) sdcard_interface: SD卡接口初始化失败: ESP_ERR_TIMEOUT
D (1246) sdcard_interface: 未检测到SD卡或挂载失败: ESP_ERR_NOT_FOUND
D (1247) ESP32S3_MAIN: 未检测到SD卡，跳过挂载
```

## 兼容性说明

### 与现有功能的兼容性

- **控制台命令**：所有现有的SD卡控制台命令仍然可用
  - `sdcard_mount` - 手动挂载
  - `sdcard_unmount` - 卸载
  - `sdcard_info` - 查看信息
  - 其他所有文件操作命令

- **编程接口**：所有现有的API函数保持不变
  - `sdcard_init()`
  - `sdcard_mount()`
  - `sdcard_unmount()`
  - 其他信息查询函数

### 状态管理

自动挂载功能会正确管理SD卡状态：

- `SDCARD_STATUS_NOT_INITIALIZED` - 未初始化
- `SDCARD_STATUS_INITIALIZED` - 已初始化但未挂载
- `SDCARD_STATUS_MOUNTED` - 已挂载（自动或手动）
- `SDCARD_STATUS_ERROR` - 错误状态

## 错误处理

### 返回值说明

- `ESP_OK` - 挂载成功
- `ESP_ERR_NOT_FOUND` - 未检测到SD卡（正常情况）
- `ESP_ERR_INVALID_STATE` - 系统状态错误
- `ESP_ERR_NO_MEM` - 内存不足
- `ESP_FAIL` - 文件系统错误（可能需要格式化）

### 错误恢复

如果自动挂载失败：
1. 系统会继续正常启动
2. 可以通过控制台手动挂载
3. 检查SD卡是否正确插入
4. 确认SD卡格式为FAT32

## 最佳实践

### 推荐使用方式

1. **依赖自动挂载**：大多数情况下不需要手动挂载
2. **检查状态**：在文件操作前检查SD卡状态
3. **错误处理**：妥善处理SD卡不存在的情况

### 示例代码

```c
void my_function(void) {
    // 检查SD卡是否已挂载
    if (sdcard_get_status() == SDCARD_STATUS_MOUNTED) {
        // 执行文件操作
        FILE* f = fopen("/sdcard/data.txt", "w");
        if (f) {
            fprintf(f, "Hello, SD Card!\n");
            fclose(f);
        }
    } else {
        printf("SD卡未挂载，跳过文件操作\n");
    }
}
```

## 注意事项

### ⚠️ 重要提醒

1. **SD卡要求**：
   - 必须使用FAT32格式
   - 建议使用Class 10或更高速度等级
   - 容量建议在32GB以下

2. **硬件连接**：确保SD卡正确连接到指定GPIO引脚

3. **系统影响**：
   - 自动挂载过程不会阻塞系统启动
   - 挂载失败不影响其他功能
   - 消耗少量启动时间（约50-100ms）

4. **调试建议**：
   - 启用详细日志查看挂载过程
   - 使用控制台命令验证功能
   - 监控系统资源使用情况

## 相关文档

- [SD卡接口组件README](../components/sdcard_interface/README.md)
- [SD卡控制台命令参考](SDCARD_CONSOLE_COMMANDS.md)
- [SD卡使用指南](SDCARD_USAGE_GUIDE.md)
- [项目整体架构说明](PROJECT_SUMMARY.md)
