# TF卡接口使用指南

## 概述

本ESP32S3项目已集成TF卡访问功能，支持通过SDMMC 4位接口访问TF卡。TF卡功能包括：

- 自动初始化SDMMC接口
- 挂载/卸载文件系统
- 获取卡信息和剩余空间
- 格式化功能
- 完整的控制台命令集

## 硬件连接

TF卡通过SDMMC接口连接到ESP32S3：

```
ESP32S3 GPIO  <-->  TF卡引脚
GPIO4         <-->  DAT0
GPIO5         <-->  DAT1
GPIO6         <-->  DAT2
GPIO7         <-->  DAT3
GPIO15        <-->  CMD
GPIO16        <-->  CLK
3.3V          <-->  VDD
GND           <-->  VSS
```

## 控制台命令

### 基本操作

1. **挂载TF卡**：
   ```
   sdcard_mount [挂载点]
   ```
   - 默认挂载到 `/sdcard`
   - 示例：`sdcard_mount /sd`

2. **卸载TF卡**：
   ```
   sdcard_unmount
   ```

3. **查看TF卡信息**：
   ```
   sdcard_info
   ```
   显示卡容量、类型、名称、剩余空间等信息

4. **列出文件**：
   ```
   sdcard_ls [路径]
   ```
   - 默认列出根目录
   - 显示文件名、大小、类型
   - 示例：`sdcard_ls /sdcard/logs`

5. **格式化TF卡**：
   ```
   sdcard_format
   ```
   ⚠️ **警告**：这将删除TF卡上的所有数据！

### 文件操作命令

6. **查看文件内容**：
   ```
   sdcard_cat <文件路径>
   ```
   - 显示文本文件的完整内容
   - 示例：`sdcard_cat /sdcard/config.txt`

7. **写入文件**：
   ```
   sdcard_write <文件路径> <内容>
   ```
   - 创建新文件并写入内容（覆盖模式）
   - 示例：`sdcard_write /sdcard/test.txt "Hello World"`

8. **追加到文件**：
   ```
   sdcard_append <文件路径> <内容>
   ```
   - 在文件末尾追加内容，自动添加时间戳
   - 示例：`sdcard_append /sdcard/log.txt "系统启动"`

9. **删除文件**：
   ```
   sdcard_rm <文件路径>
   ```
   - 删除指定文件
   - 示例：`sdcard_rm /sdcard/temp.txt`

10. **复制文件**：
    ```
    sdcard_cp <源文件> <目标文件>
    ```
    - 复制文件到新位置
    - 显示复制的字节数
    - 示例：`sdcard_cp /sdcard/config.txt /sdcard/backup/config.bak`

### 目录操作命令

11. **创建目录**：
    ```
    sdcard_mkdir <目录路径>
    ```
    - 创建新目录
    - 示例：`sdcard_mkdir /sdcard/logs`

12. **删除空目录**：
    ```
    sdcard_rmdir <目录路径>
    ```
    - 删除空目录（目录必须为空）
    - 示例：`sdcard_rmdir /sdcard/temp`

13. **查看文件/目录详情**：
    ```
    sdcard_stat <路径>
    ```
    - 显示详细信息：类型、大小、修改时间、权限
    - 示例：`sdcard_stat /sdcard/config.txt`

### 标准文件系统操作

标准POSIX文件操作可在挂载后使用：

- `cat` - 查看文件内容
- `ls` - 列出目录（需要具体实现）
- `mkdir` - 创建目录（通过代码）
- `rm` - 删除文件（通过代码）

## 编程接口

### 基本流程

```c
#include "sdcard_interface.h"

// 1. 初始化（可选，mount会自动调用）
esp_err_t ret = sdcard_init();

// 2. 挂载
ret = sdcard_mount("/sdcard");
if (ret == ESP_OK) {
    // 3. 使用标准文件操作
    FILE* f = fopen("/sdcard/test.txt", "w");
    fprintf(f, "Hello TF Card!\n");
    fclose(f);
    
    // 4. 卸载（可选）
    sdcard_unmount();
}
```

### API函数

#### 初始化和挂载
```c
esp_err_t sdcard_init(void);                    // 初始化SDMMC接口
esp_err_t sdcard_mount(const char* mount_point); // 挂载文件系统
esp_err_t sdcard_unmount(void);                 // 卸载文件系统
esp_err_t sdcard_deinit(void);                  // 反初始化
```

#### 状态查询
```c
sdcard_status_t sdcard_get_status(void);        // 获取当前状态
bool sdcard_is_present(void);                   // 检查卡是否存在
```

#### 信息获取
```c
esp_err_t sdcard_get_info(sdcard_info_t* info);   // 获取卡信息
esp_err_t sdcard_get_space(sdcard_space_t* space); // 获取空间信息
```

#### 维护操作
```c
esp_err_t sdcard_format(void);                  // 格式化（危险操作）
```

### 数据结构

```c
// TF卡状态
typedef enum {
    SDCARD_STATUS_NOT_INITIALIZED = 0,
    SDCARD_STATUS_INITIALIZED,
    SDCARD_STATUS_MOUNTED,
    SDCARD_STATUS_ERROR
} sdcard_status_t;

// TF卡信息
typedef struct {
    uint64_t capacity;          // 容量（字节）
    uint32_t sector_size;       // 扇区大小
    uint32_t total_sectors;     // 总扇区数
    char name[64];              // 卡名称
    char type[32];              // 卡类型（SDSC/SDHC/SDXC）
    bool is_mounted;            // 是否已挂载
    char mount_point[16];       // 挂载点路径
} sdcard_info_t;

// 空间信息
typedef struct {
    uint64_t total_bytes;       // 总空间（字节）
    uint64_t free_bytes;        // 剩余空间（字节）
    uint64_t used_bytes;        // 已使用空间（字节）
} sdcard_space_t;
```

## 使用示例

### 1. 基本文件操作

```c
#include "sdcard_interface.h"
#include <stdio.h>
#include <sys/stat.h>

void example_basic_file_operations(void)
{
    // 挂载TF卡
    if (sdcard_mount("/sdcard") != ESP_OK) {
        printf("TF卡挂载失败\n");
        return;
    }
    
    // 创建并写入文件
    FILE* f = fopen("/sdcard/test.txt", "w");
    if (f) {
        fprintf(f, "Hello from ESP32S3!\n");
        fprintf(f, "Current time: %ld\n", time(NULL));
        fclose(f);
        printf("文件写入成功\n");
    }
    
    // 读取文件
    f = fopen("/sdcard/test.txt", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            printf("读取: %s", line);
        }
        fclose(f);
    }
    
    // 获取文件信息
    struct stat st;
    if (stat("/sdcard/test.txt", &st) == 0) {
        printf("文件大小: %ld 字节\n", st.st_size);
    }
}
```

### 2. 获取TF卡信息

```c
void example_get_card_info(void)
{
    sdcard_info_t info;
    sdcard_space_t space;
    
    if (sdcard_get_info(&info) == ESP_OK) {
        printf("=== TF卡信息 ===\n");
        printf("名称: %s\n", info.name);
        printf("类型: %s\n", info.type);
        printf("容量: %.2f MB\n", info.capacity / (1024.0 * 1024.0));
        printf("扇区大小: %u 字节\n", info.sector_size);
        printf("挂载点: %s\n", info.mount_point);
    }
    
    if (sdcard_get_space(&space) == ESP_OK) {
        printf("=== 空间信息 ===\n");
        printf("总空间: %.2f MB\n", space.total_bytes / (1024.0 * 1024.0));
        printf("已使用: %.2f MB\n", space.used_bytes / (1024.0 * 1024.0));
        printf("剩余空间: %.2f MB\n", space.free_bytes / (1024.0 * 1024.0));
        printf("使用率: %.1f%%\n", 
               (space.used_bytes * 100.0) / space.total_bytes);
    }
}
```

### 3. 目录操作

```c
#include <dirent.h>

void example_directory_operations(void)
{
    // 创建目录
    if (mkdir("/sdcard/logs", 0755) == 0) {
        printf("目录创建成功\n");
    }
    
    // 列出目录内容
    DIR* dir = opendir("/sdcard");
    if (dir) {
        struct dirent* entry;
        printf("=== 目录列表 ===\n");
        while ((entry = readdir(dir)) != NULL) {
            printf("%s%s\n", entry->d_name, 
                   (entry->d_type == DT_DIR) ? "/" : "");
        }
        closedir(dir);
    }
}
```

## 注意事项

1. **电源要求**：TF卡需要稳定的3.3V供电
2. **GPIO配置**：使用的GPIO必须支持SDMMC功能
3. **上拉电阻**：数据线和命令线建议外接10kΩ上拉电阻
4. **文件系统**：支持FAT32格式的TF卡
5. **线程安全**：多线程访问需要外部同步
6. **错误处理**：始终检查API返回值
7. **资源管理**：及时关闭文件句柄和目录句柄

## 故障排除

### 常见错误

1. **挂载失败**：
   - 检查TF卡是否正确插入
   - 检查TF卡格式（应为FAT32）
   - 检查GPIO连接
   - 尝试格式化TF卡

2. **读写错误**：
   - 检查文件路径是否正确
   - 检查文件权限
   - 检查剩余空间

3. **初始化失败**：
   - 检查GPIO配置
   - 检查电源供应
   - 检查TF卡兼容性

### 调试日志

启用详细日志以便调试：
```c
esp_log_level_set("sdcard_interface", ESP_LOG_DEBUG);
esp_log_level_set("vfs_fat_sdmmc", ESP_LOG_DEBUG);
esp_log_level_set("sdmmc_req", ESP_LOG_DEBUG);
```

## 性能优化

1. **缓冲区大小**：调整 `allocation_unit_size` 以优化大文件操作
2. **时钟频率**：根据TF卡规格调整 `max_freq_khz`
3. **文件缓存**：使用 `setvbuf()` 设置文件缓冲
4. **批量操作**：合并小的读写操作以提高效率
