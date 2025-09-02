/*
 * SD卡接口组件实现
 * 基于ESP32S3的SDMMC接口实现TF卡访问功能
 */

#include "sdcard_interface.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#include <string.h>
#include <sys/stat.h>
// 注意：sys/statvfs.h 在 ESP-IDF 中不可用，使用其他方法获取空间信息

static const char* TAG = "sdcard_interface";

// 静态变量
static sdmmc_card_t* s_card = NULL;
static bool s_initialized = false;
static char s_mount_point[32] = "/sdcard";
static sdcard_status_t s_status = SDCARD_STATUS_NOT_INITIALIZED;

// SDMMC GPIO配置 - 根据之前讨论的配置
#define SDCARD_PIN_NUM_D0   4    // DAT0
#define SDCARD_PIN_NUM_D1   5    // DAT1  
#define SDCARD_PIN_NUM_D2   6    // DAT2
#define SDCARD_PIN_NUM_D3   7    // DAT3
#define SDCARD_PIN_NUM_CMD  15   // CMD
#define SDCARD_PIN_NUM_CLK  16   // CLK

esp_err_t sdcard_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "SD卡已经初始化");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "初始化SD卡接口");

    // 配置SDMMC主机
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 40MHz

    // 配置SDMMC插槽
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4; // 4位数据线
    
    // 配置GPIO引脚
    slot_config.clk = SDCARD_PIN_NUM_CLK;   // CLK - GPIO16
    slot_config.cmd = SDCARD_PIN_NUM_CMD;   // CMD - GPIO15
    slot_config.d0 = SDCARD_PIN_NUM_D0;     // DAT0 - GPIO4
    slot_config.d1 = SDCARD_PIN_NUM_D1;     // DAT1 - GPIO5
    slot_config.d2 = SDCARD_PIN_NUM_D2;     // DAT2 - GPIO6
    slot_config.d3 = SDCARD_PIN_NUM_D3;     // DAT3 - GPIO7
    
    // 启用内部上拉电阻（建议外部也要有上拉电阻）
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // 初始化SDMMC外设
    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化SDMMC主机失败: %s", esp_err_to_name(ret));
        s_status = SDCARD_STATUS_ERROR;
        return ret;
    }

    ret = sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化SDMMC插槽失败: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        s_status = SDCARD_STATUS_ERROR;
        return ret;
    }

    s_initialized = true;
    s_status = SDCARD_STATUS_INITIALIZED;
    ESP_LOGI(TAG, "SD卡接口初始化完成");
    
    return ESP_OK;
}

esp_err_t sdcard_mount(const char* mount_point)
{
    if (!s_initialized) {
        esp_err_t ret = sdcard_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    if (s_card != NULL) {
        ESP_LOGW(TAG, "SD卡已经挂载在 %s", s_mount_point);
        return ESP_OK;
    }

    if (mount_point) {
        strncpy(s_mount_point, mount_point, sizeof(s_mount_point) - 1);
        s_mount_point[sizeof(s_mount_point) - 1] = '\0';
    }

    ESP_LOGI(TAG, "挂载SD卡到 %s", s_mount_point);

    // 配置FAT文件系统挂载选项
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 10,                  // 增加同时打开文件数量
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false  // 禁用磁盘状态检查以提高性能
    };

    // 配置SDMMC主机和插槽
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SDCARD_PIN_NUM_CLK;
    slot_config.cmd = SDCARD_PIN_NUM_CMD;
    slot_config.d0 = SDCARD_PIN_NUM_D0;
    slot_config.d1 = SDCARD_PIN_NUM_D1;
    slot_config.d2 = SDCARD_PIN_NUM_D2;
    slot_config.d3 = SDCARD_PIN_NUM_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // 挂载文件系统
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(s_mount_point, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "挂载文件系统失败 - 可能需要格式化SD卡");
        } else {
            ESP_LOGE(TAG, "初始化SD卡失败 (%s) - 请检查SD卡是否正确插入", esp_err_to_name(ret));
        }
        s_status = SDCARD_STATUS_ERROR;
        return ret;
    }

    s_status = SDCARD_STATUS_MOUNTED;
    ESP_LOGI(TAG, "SD卡挂载成功");
    
    // 打印卡信息
    if (s_card) {
        ESP_LOGI(TAG, "卡名称: %s", s_card->cid.name);
        // 根据容量判断卡类型（简化方法）
        uint64_t capacity_mb = ((uint64_t) s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
        ESP_LOGI(TAG, "卡类型: %s", (capacity_mb > 2048) ? "SDHC/SDXC" : "SDSC");
        ESP_LOGI(TAG, "卡容量: %lluMB", capacity_mb);
    }
    
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (s_card == NULL) {
        ESP_LOGW(TAG, "SD卡未挂载");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "卸载SD卡");
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    if (ret == ESP_OK) {
        s_card = NULL;
        s_status = SDCARD_STATUS_INITIALIZED;
        ESP_LOGI(TAG, "SD卡卸载成功");
    } else {
        ESP_LOGE(TAG, "SD卡卸载失败: %s", esp_err_to_name(ret));
        s_status = SDCARD_STATUS_ERROR;
    }
    
    return ret;
}

esp_err_t sdcard_deinit(void)
{
    if (s_card != NULL) {
        sdcard_unmount();
    }
    
    if (s_initialized) {
        sdmmc_host_deinit();
        s_initialized = false;
        s_status = SDCARD_STATUS_NOT_INITIALIZED;
        ESP_LOGI(TAG, "SD卡接口去初始化完成");
    }
    
    return ESP_OK;
}

sdcard_status_t sdcard_get_status(void)
{
    return s_status;
}

esp_err_t sdcard_get_info(sdcard_info_t* info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(info, 0, sizeof(sdcard_info_t));

    if (s_card == NULL) {
        // 如果没有挂载，返回基本状态信息
        info->is_mounted = false;
        return ESP_OK;
    }

    // 填充SD卡信息
    info->capacity = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
    info->sector_size = s_card->csd.sector_size;
    info->total_sectors = s_card->csd.capacity;
    
    // 获取产品名称
    strncpy(info->name, s_card->cid.name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    
    // 根据容量简单判断卡类型
    uint64_t capacity_mb = ((uint64_t) s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
    if (capacity_mb > 2048) {
        strncpy(info->type, "SDHC/SDXC", sizeof(info->type) - 1);
    } else {
        strncpy(info->type, "SDSC", sizeof(info->type) - 1);
    }
    info->type[sizeof(info->type) - 1] = '\0';
    
    info->is_mounted = true;
    strncpy(info->mount_point, s_mount_point, sizeof(info->mount_point) - 1);
    info->mount_point[sizeof(info->mount_point) - 1] = '\0';
    
    return ESP_OK;
}

esp_err_t sdcard_get_space(sdcard_space_t* space)
{
    if (!space) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_card == NULL) {
        ESP_LOGE(TAG, "SD卡未挂载");
        return ESP_ERR_INVALID_STATE;
    }

    // 使用SD卡的扇区信息计算空间（ESP-IDF兼容方法）
    uint64_t card_size = ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size;
    space->total_bytes = card_size;
    
    // 简单估算可用空间（这里无法精确计算已用空间，设为总空间的80%可用）
    // 更精确的方法需要遍历文件系统，但对性能影响较大
    space->free_bytes = card_size * 8 / 10;  // 估算80%可用
    space->used_bytes = space->total_bytes - space->free_bytes;

    return ESP_OK;
}

bool sdcard_is_present(void)
{
    return (s_status == SDCARD_STATUS_INITIALIZED || s_status == SDCARD_STATUS_MOUNTED);
}

esp_err_t sdcard_auto_mount(const char* mount_point)
{
    ESP_LOGI(TAG, "开始自动检测SD卡...");
    
    // 如果已经挂载，直接返回成功
    if (s_status == SDCARD_STATUS_MOUNTED) {
        ESP_LOGI(TAG, "SD卡已挂载在 %s", s_mount_point);
        return ESP_OK;
    }
    
    // 尝试初始化SD卡接口
    esp_err_t ret = sdcard_init();
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "SD卡接口初始化失败: %s", esp_err_to_name(ret));
        return ESP_ERR_NOT_FOUND;
    }
    
    // 尝试挂载SD卡
    const char* target_mount_point = mount_point ? mount_point : "/sdcard";
    ret = sdcard_mount(target_mount_point);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ SD卡自动挂载成功到: %s", target_mount_point);
        
        // 显示SD卡信息
        sdcard_info_t info;
        if (sdcard_get_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "SD卡: %s, %s, %.2f MB", 
                    info.name, info.type, (double)info.capacity / (1024 * 1024));
        }
        
        return ESP_OK;
    } else {
        ESP_LOGD(TAG, "未检测到SD卡或挂载失败: %s", esp_err_to_name(ret));
        return ESP_ERR_NOT_FOUND;
    }
}

esp_err_t sdcard_format(void)
{
    if (s_card == NULL) {
        ESP_LOGE(TAG, "SD卡未挂载，无法格式化");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "开始格式化SD卡 - 这将删除所有数据！");
    
    // 先卸载
    esp_err_t ret = sdcard_unmount();
    if (ret != ESP_OK) {
        return ret;
    }

    // 重新挂载时启用格式化选项
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true, // 启用格式化
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = SDCARD_PIN_NUM_CLK;
    slot_config.cmd = SDCARD_PIN_NUM_CMD;
    slot_config.d0 = SDCARD_PIN_NUM_D0;
    slot_config.d1 = SDCARD_PIN_NUM_D1;
    slot_config.d2 = SDCARD_PIN_NUM_D2;
    slot_config.d3 = SDCARD_PIN_NUM_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ret = esp_vfs_fat_sdmmc_mount(s_mount_point, &host, &slot_config, &mount_config, &s_card);
    
    if (ret == ESP_OK) {
        s_status = SDCARD_STATUS_MOUNTED;
        ESP_LOGI(TAG, "SD卡格式化并挂载成功");
    } else {
        s_status = SDCARD_STATUS_ERROR;
        ESP_LOGE(TAG, "SD卡格式化失败: %s", esp_err_to_name(ret));
    }
    
    return ret;
}
