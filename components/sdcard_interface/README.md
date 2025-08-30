# SD卡接口组件

SD卡接口组件为 ESP32S3 提供了完整的 SD 卡访问功能，支持 SDMMC 4位数据线模式。

## 硬件连接

SD 卡通过 SDMMC 接口连接到 ESP32S3，使用以下引脚：

| 信号 | ESP32S3 引脚 | SD卡引脚 | 描述 |
|------|-------------|----------|------|
| D0   | GPIO4       | DAT0     | 数据线0 |
| D1   | GPIO5       | DAT1     | 数据线1 |
| D2   | GPIO6       | DAT2     | 数据线2 |
| D3   | GPIO7       | DAT3     | 数据线3 |
| CMD  | GPIO15      | CMD      | 命令线 |
| CLK  | GPIO16      | CLK      | 时钟线 |

## 功能特性

- **初始化和挂载**: 自动检测和初始化 SD 卡
- **文件系统支持**: 使用 FAT32 文件系统
- **空间管理**: 查看总容量、已用空间和可用空间
- **卡信息查询**: 获取 SD 卡详细信息（容量、类型、速度等）
- **安全操作**: 支持安全挂载和卸载
- **格式化功能**: 支持 SD 卡格式化（谨慎使用）

## API 接口

### 基本操作

```c
// 初始化SD卡接口
esp_err_t sdcard_init(void);

// 挂载SD卡到指定路径
esp_err_t sdcard_mount(const char* mount_point);

// 卸载SD卡
esp_err_t sdcard_unmount(void);

// 反初始化
esp_err_t sdcard_deinit(void);
```

### 信息查询

```c
// 获取SD卡状态
sdcard_status_t sdcard_get_status(void);

// 获取SD卡详细信息
esp_err_t sdcard_get_info(sdcard_info_t* info);

// 获取空间信息
esp_err_t sdcard_get_space(uint64_t* free_bytes, uint64_t* total_bytes);

// 检查SD卡是否存在
bool sdcard_is_present(void);
```

### 危险操作

```c
// 格式化SD卡（会删除所有数据）
esp_err_t sdcard_format(void);
```

## 控制台命令

通过控制台可以方便地管理 SD 卡：

### sdcard_mount [挂载点]
挂载 SD 卡到文件系统。默认挂载点为 `/sdcard`。

```
ESP32S3> sdcard_mount
SD卡挂载成功到: /sdcard
SD卡信息:
  名称: SD32G
  类型: SDHC/SDXC
  容量: 31914.91 MB
  扇区大小: 512 字节
  总空间: 31914.91 MB
  可用空间: 31900.45 MB
  使用率: 0.0%
```

### sdcard_unmount
卸载 SD 卡。

```
ESP32S3> sdcard_unmount
SD卡卸载成功
```

### sdcard_info
显示 SD 卡详细信息。

```
ESP32S3> sdcard_info
SD卡状态: 已挂载
SD卡详细信息:
  名称: SD32G
  类型: SDHC/SDXC
  容量: 31914.91 MB (31.15 GB)
  扇区大小: 512 字节
  总扇区数: 65363968
  挂载点: /sdcard
空间信息:
  总空间: 31914.91 MB
  已用空间: 14.46 MB
  可用空间: 31900.45 MB
  使用率: 0.0%
```

### sdcard_ls [路径]
列出 SD 卡目录内容。默认列出根目录。

```
ESP32S3> sdcard_ls /sdcard
列出目录: /sdcard
目录存在，但详细列表功能需要进一步实现
提示: 可以使用文件系统 API 来实现完整的目录列表功能
```

### sdcard_format
格式化 SD 卡（危险操作，会删除所有数据）。

```
ESP32S3> sdcard_format
警告: 格式化将删除SD卡上的所有数据！
如果确定要格式化，请输入 'YES': YES
正在格式化SD卡...
SD卡格式化成功
```

## 使用示例

### 基本使用流程

1. **初始化和挂载**:
   ```
   sdcard_mount
   ```

2. **查看信息**:
   ```
   sdcard_info
   ```

3. **使用完毕后卸载**:
   ```
   sdcard_unmount
   ```

### 编程接口示例

```c
#include "sdcard_interface.h"

void example_sdcard_usage(void)
{
    // 初始化并挂载SD卡
    if (sdcard_init() == ESP_OK) {
        if (sdcard_mount("/sdcard") == ESP_OK) {
            printf("SD卡挂载成功\n");
            
            // 获取空间信息
            uint64_t free_bytes, total_bytes;
            if (sdcard_get_space(&free_bytes, &total_bytes) == ESP_OK) {
                printf("可用空间: %.2f MB\n", (double)free_bytes / (1024 * 1024));
            }
            
            // 使用标准文件操作
            FILE* f = fopen("/sdcard/test.txt", "w");
            if (f) {
                fprintf(f, "Hello SD Card!\n");
                fclose(f);
                printf("文件写入成功\n");
            }
            
            // 卸载SD卡
            sdcard_unmount();
        }
    }
}
```

## 状态说明

SD 卡接口具有以下状态：

- **SDCARD_STATUS_NOT_INITIALIZED**: 未初始化
- **SDCARD_STATUS_INITIALIZED**: 已初始化，但未挂载
- **SDCARD_STATUS_MOUNTED**: 已挂载到文件系统
- **SDCARD_STATUS_ERROR**: 错误状态

## 注意事项

1. **引脚冲突**: 确保 SD 卡引脚没有与其他功能冲突
2. **电源要求**: SD 卡需要稳定的 3.3V 电源
3. **文件系统**: 当前仅支持 FAT32 文件系统
4. **线程安全**: SD 卡操作应在单个任务中进行，或使用适当的同步机制
5. **格式化警告**: 格式化操作会永久删除所有数据，请谨慎使用

## 故障排除

### 常见问题

1. **挂载失败**
   - 检查 SD 卡是否正确插入
   - 确认引脚连接正确
   - 检查 SD 卡是否支持（推荐使用 Class 10 或更高级别的卡）

2. **读写错误**
   - 检查 SD 卡是否有写保护
   - 确认文件系统是否正常
   - 尝试格式化 SD 卡

3. **性能问题**
   - 使用高速 SD 卡（Class 10 或 UHS-I）
   - 确保时钟频率设置合适
   - 避免频繁的小文件操作

## 扩展功能

可以考虑添加以下功能：

- 完整的目录列表功能
- 文件复制和移动命令
- SD 卡热插拔检测
- 多分区支持
- 数据完整性检查
