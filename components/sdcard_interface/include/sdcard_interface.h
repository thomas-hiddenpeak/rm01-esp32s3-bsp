/*
 * SD卡接口组件
 * 提供SD卡初始化、挂载、卸载等功能
 */

#ifndef SDCARD_INTERFACE_H
#define SDCARD_INTERFACE_H

#include "esp_err.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#ifdef __cplusplus
extern "C" {
#endif

// SD卡状态
typedef enum {
    SDCARD_STATUS_NOT_INITIALIZED = 0,
    SDCARD_STATUS_INITIALIZED,
    SDCARD_STATUS_MOUNTED,
    SDCARD_STATUS_ERROR
} sdcard_status_t;

// SD卡信息结构体
typedef struct {
    uint64_t capacity;          // 容量 (字节)
    uint32_t sector_size;       // 扇区大小
    uint32_t total_sectors;     // 总扇区数
    char name[64];              // 卡名称
    char type[32];              // 卡类型
    bool is_mounted;            // 是否已挂载
    char mount_point[16];       // 挂载点
} sdcard_info_t;

/**
 * @brief 初始化SD卡接口
 * 
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_init(void);

/**
 * @brief 挂载SD卡到文件系统
 * 
 * @param mount_point 挂载点路径，例如"/sdcard"
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_mount(const char* mount_point);

/**
 * @brief 卸载SD卡
 * 
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_unmount(void);

/**
 * @brief 反初始化SD卡接口
 * 
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_deinit(void);

/**
 * @brief 获取SD卡状态
 * 
 * @return sdcard_status_t 当前SD卡状态
 */
sdcard_status_t sdcard_get_status(void);

/**
 * @brief 获取SD卡信息
 * 
 * @param info 输出SD卡信息的结构体指针
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_get_info(sdcard_info_t* info);

// SD卡空间信息结构体
typedef struct {
    uint64_t total_bytes;       // 总空间(字节)
    uint64_t free_bytes;        // 剩余空间(字节)
    uint64_t used_bytes;        // 已使用空间(字节)
} sdcard_space_t;

/**
 * @brief 获取SD卡剩余空间
 * 
 * @param space 输出SD卡空间信息的结构体指针
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_get_space(sdcard_space_t* space);

/**
 * @brief 检查SD卡是否存在
 * 
 * @return true SD卡存在且可访问
 * @return false SD卡不存在或无法访问
 */
bool sdcard_is_present(void);

/**
 * @brief 自动检测并挂载SD卡
 * 如果检测到SD卡存在，会自动挂载到默认挂载点
 * 
 * @param mount_point 挂载点路径，如果为NULL则使用默认的"/sdcard"
 * @return esp_err_t ESP_OK成功挂载，ESP_ERR_NOT_FOUND未检测到SD卡，其他为错误码
 */
esp_err_t sdcard_auto_mount(const char* mount_point);

/**
 * @brief 格式化SD卡 (小心使用！)
 * 
 * @return esp_err_t ESP_OK成功，其他为错误码
 */
esp_err_t sdcard_format(void);

#ifdef __cplusplus
}
#endif

#endif // SDCARD_INTERFACE_H
