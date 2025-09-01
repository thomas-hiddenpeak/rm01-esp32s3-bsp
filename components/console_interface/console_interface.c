/**
 * @file console_interface.c
 * @brief ESP32S3 Console Interface Component Implementation
 */

#include "console_interface.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

// 引入设备组件
#include "device_interface.h"
#include "hardware_control.h"
#include "system_monitor.h"
#include "ethernet_interface.h"
#include "config_manager.h"
#include "sdcard_interface.h"

static const char *TAG = "CONSOLE_INTERFACE";

// 内部状态结构
typedef struct {
    bool initialized;
    bool running;
    TaskHandle_t console_task_handle;
    console_interface_config_t config;
    console_event_callback_t event_callback;
    uint32_t commands_executed;
    uint64_t start_time_ms;
} console_state_t;

static console_state_t s_console_state = {0};

// 内部函数声明
static void console_task(void *pvParameters);
static uint64_t get_time_ms(void);

// 命令函数声明
static int cmd_help(int argc, char **argv);
static int cmd_info(int argc, char **argv);
static int cmd_status(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_fan(int argc, char **argv);
static int cmd_bled(int argc, char **argv);
static int cmd_tled(int argc, char **argv);
static int cmd_gpio(int argc, char **argv);
static int cmd_usbmux(int argc, char **argv);
static int cmd_agx(int argc, char **argv);
static int cmd_lpmu(int argc, char **argv);
static int cmd_test(int argc, char **argv);
static int cmd_save(int argc, char **argv);
static int cmd_load(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_config(int argc, char **argv);
static int cmd_defaults(int argc, char **argv);

// 触发控制台事件
static void trigger_console_event(console_event_t event, const char *data)
{
    if (s_console_state.event_callback) {
        s_console_state.event_callback(event, data);
    }
}

// 获取时间戳
static uint64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}

esp_err_t console_interface_init(const console_interface_config_t *config)
{
    if (s_console_state.initialized) {
        ESP_LOGW(TAG, "Console interface already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // 复制配置
    memcpy(&s_console_state.config, config, sizeof(console_interface_config_t));

    // 禁用缓冲，提高响应性
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    // 初始化ESP控制台
    esp_console_config_t console_config = {
        .max_cmdline_args = config->max_cmdline_args,
        .max_cmdline_length = config->max_cmdline_length,
        .hint_color = config->enable_color_hints ? 35 : 0
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    // 配置linenoise
    linenoiseSetMultiLine(config->enable_multiline ? 1 : 0);
    linenoiseSetCompletionCallback(NULL);
    linenoiseSetHintsCallback(NULL);
    linenoiseHistorySetMaxLen(config->history_length);

    // 注册ESP控制台内置help命令
    esp_console_register_help_command();

    s_console_state.initialized = true;
    s_console_state.start_time_ms = get_time_ms();
    
    ESP_LOGI(TAG, "Console interface initialized");
    return ESP_OK;
}

esp_err_t console_interface_start(uint32_t stack_size, uint8_t priority)
{
    if (!s_console_state.initialized) {
        ESP_LOGE(TAG, "Console interface not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_console_state.running) {
        ESP_LOGW(TAG, "Console task already running");
        return ESP_OK;
    }

    BaseType_t ret = xTaskCreate(
        console_task,
        "console_task",
        stack_size,
        NULL,
        priority,
        &s_console_state.console_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
        return ESP_ERR_NO_MEM;
    }

    s_console_state.running = true;
    trigger_console_event(CONSOLE_EVENT_READY, NULL);
    
    ESP_LOGI(TAG, "Console task started");
    return ESP_OK;
}

esp_err_t console_interface_stop(void)
{
    if (!s_console_state.running) {
        return ESP_OK;
    }

    trigger_console_event(CONSOLE_EVENT_SHUTDOWN, NULL);
    
    if (s_console_state.console_task_handle) {
        vTaskDelete(s_console_state.console_task_handle);
        s_console_state.console_task_handle = NULL;
    }

    s_console_state.running = false;
    ESP_LOGI(TAG, "Console task stopped");
    return ESP_OK;
}

esp_err_t console_interface_register_event_callback(console_event_callback_t callback)
{
    s_console_state.event_callback = callback;
    return ESP_OK;
}

esp_err_t console_interface_register_system_commands(void)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_console_cmd_t commands[] = {
        {
            .command = "help",
            .help = "显示帮助信息",
            .func = &cmd_help,
        },
        {
            .command = "info",
            .help = "显示系统信息",
            .func = &cmd_info,
        },
        {
            .command = "status",
            .help = "显示当前状态",
            .func = &cmd_status,
        },
        {
            .command = "reboot",
            .help = "重启系统",
            .func = &cmd_reboot,
        }
    };

    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }

    ESP_LOGI(TAG, "System commands registered");
    return ESP_OK;
}

esp_err_t console_interface_register_device_commands(void)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_console_cmd_t commands[] = {
        {
            .command = "fan",
            .help = "风扇控制: fan <0-100>|on|off",
            .func = &cmd_fan,
        },
        {
            .command = "bled",
            .help = "板载LED控制: bled <r> <g> <b>|bright <0-100>|off|rainbow",
            .func = &cmd_bled,
        },
        {
            .command = "tled",
            .help = "触摸LED控制: tled <r> <g> <b>|bright <0-100>|off",
            .func = &cmd_tled,
        },
        {
            .command = "gpio",
            .help = "GPIO控制: gpio <pin> high|low|input",
            .func = &cmd_gpio,
        },
        {
            .command = "usbmux",
            .help = "USB MUX控制: usbmux esp32s3|agx|lpmu|status",
            .func = &cmd_usbmux,
        },
        {
            .command = "agx",
            .help = "AGX电源控制: agx on|off|reset|recovery|status",
            .func = &cmd_agx,
        },
        {
            .command = "lpmu",
            .help = "LPMU电源控制: lpmu toggle|reset|status",
            .func = &cmd_lpmu,
        },
        {
            .command = "test",
            .help = "硬件测试: test fan|bled|tled|gpio <pin>|gpio_input <pin>|agx|lpmu|all|quick|stress <ms>",
            .func = &cmd_test,
        }
    };

    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }

    ESP_LOGI(TAG, "Device commands registered");
    return ESP_OK;
}

esp_err_t console_interface_register_config_commands(void)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_console_cmd_t commands[] = {
        {
            .command = "save",
            .help = "保存当前配置到NVS",
            .func = &cmd_save,
        },
        {
            .command = "load",
            .help = "从NVS加载配置",
            .func = &cmd_load,
        },
        {
            .command = "clear",
            .help = "清除NVS中的配置",
            .func = &cmd_clear,
        },
        {
            .command = "config",
            .help = "配置管理: config show|reset|set <type> <params>",
            .func = &cmd_config,
        },
        {
            .command = "defaults",
            .help = "默认参数管理: defaults show|apply|fan|led|eth|dhcp|gateway (网络配置自动保存)",
            .func = &cmd_defaults,
        }
    };

    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }

    ESP_LOGI(TAG, "Config commands registered");
    return ESP_OK;
}

esp_err_t console_interface_register_ethernet_commands(void)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // 注册以太网控制台命令
    esp_err_t ret = ethernet_register_console_commands();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Ethernet commands registered");
    } else {
        ESP_LOGE(TAG, "Failed to register ethernet commands: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// ==================== SD卡相关命令 ====================

// SD卡 Shell 状态管理
typedef struct {
    bool in_shell_mode;
    char current_path[256];  // 减小路径缓冲区大小
    char previous_path[256];
} sdcard_shell_state_t;

static sdcard_shell_state_t s_sdcard_shell = {
    .in_shell_mode = false,
    .current_path = "/sdcard",
    .previous_path = "/sdcard"
};

// SD卡 Shell 函数声明
static int cmd_sdcard_shell(int argc, char **argv);
static int cmd_shell_exit(int argc, char **argv);
static int cmd_shell_pwd(int argc, char **argv);
static int cmd_shell_cd(int argc, char **argv);
static int cmd_shell_ls(int argc, char **argv);
static int cmd_shell_cat(int argc, char **argv);
static int cmd_shell_mkdir(int argc, char **argv);
static int cmd_shell_rm(int argc, char **argv);
static int cmd_shell_rmdir(int argc, char **argv);
static int cmd_shell_cp(int argc, char **argv);
static int cmd_shell_write(int argc, char **argv);
static int cmd_shell_stat(int argc, char **argv);
static void sdcard_shell_register_commands(void);
static void sdcard_shell_unregister_commands(void);
static void normalize_path(char* path);
static bool is_valid_path(const char* path);
static void show_shell_prompt(void);

// 显示Shell提示符
static void show_shell_prompt(void)
{
    if (s_sdcard_shell.in_shell_mode) {
        printf("\n[Shell模式] 当前目录: %s\n", s_sdcard_shell.current_path);
        printf("输入命令 (pwd, cd, ls, cat, mkdir, rm, rmdir, cp, write, stat, exit): ");
        fflush(stdout);
    }
}

// SD卡普通命令函数

static int cmd_sdcard_mount(int argc, char **argv)
{
    esp_err_t ret = sdcard_init();
    if (ret != ESP_OK) {
        printf("SD卡接口初始化失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    const char* mount_point = "/sdcard";
    if (argc > 1) {
        mount_point = argv[1];
    }

    ret = sdcard_mount(mount_point);
    if (ret == ESP_OK) {
        printf("SD卡挂载成功到: %s\n", mount_point);
        
        // 显示SD卡信息
        sdcard_info_t info;
        if (sdcard_get_info(&info) == ESP_OK) {
            printf("SD卡信息:\n");
            printf("  名称: %s\n", info.name);
            printf("  类型: %s\n", info.type);
            printf("  容量: %.2f MB\n", (double)info.capacity / (1024 * 1024));
            printf("  扇区大小: %lu 字节\n", info.sector_size);
        }

        // 显示空间信息
        sdcard_space_t space_info;
        if (sdcard_get_space(&space_info) == ESP_OK) {
            printf("  总空间: %.2f MB\n", (double)space_info.total_bytes / (1024 * 1024));
            printf("  可用空间: %.2f MB\n", (double)space_info.free_bytes / (1024 * 1024));
            printf("  使用率: %.1f%%\n", 
                   ((double)space_info.used_bytes / space_info.total_bytes) * 100);
        }
    } else {
        printf("SD卡挂载失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    return 0;
}

static int cmd_sdcard_unmount(int argc, char **argv)
{
    esp_err_t ret = sdcard_unmount();
    if (ret == ESP_OK) {
        printf("SD卡卸载成功\n");
    } else {
        printf("SD卡卸载失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    return 0;
}

static int cmd_sdcard_info(int argc, char **argv)
{
    sdcard_status_t status = sdcard_get_status();
    
    printf("SD卡状态: ");
    switch (status) {
        case SDCARD_STATUS_NOT_INITIALIZED:
            printf("未初始化\n");
            break;
        case SDCARD_STATUS_INITIALIZED:
            printf("已初始化，未挂载\n");
            break;
        case SDCARD_STATUS_MOUNTED:
            printf("已挂载\n");
            break;
        case SDCARD_STATUS_ERROR:
            printf("错误状态\n");
            break;
        default:
            printf("未知状态\n");
            break;
    }

    if (status != SDCARD_STATUS_MOUNTED) {
        printf("请先挂载SD卡以获取详细信息\n");
        return 0;
    }

    sdcard_info_t info;
    esp_err_t ret = sdcard_get_info(&info);
    if (ret != ESP_OK) {
        printf("获取SD卡信息失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("SD卡详细信息:\n");
    printf("  名称: %s\n", info.name);
    printf("  类型: %s\n", info.type);
    printf("  容量: %.2f MB (%.2f GB)\n", 
           (double)info.capacity / (1024 * 1024),
           (double)info.capacity / (1024 * 1024 * 1024));
    printf("  扇区大小: %lu 字节\n", info.sector_size);
    printf("  总扇区数: %lu\n", info.total_sectors);
    printf("  挂载点: %s\n", info.mount_point);

    sdcard_space_t space_info;
    if (sdcard_get_space(&space_info) == ESP_OK) {
        printf("空间信息:\n");
        printf("  总空间: %.2f MB\n", (double)space_info.total_bytes / (1024 * 1024));
        printf("  已用空间: %.2f MB\n", 
               (double)space_info.used_bytes / (1024 * 1024));
        printf("  可用空间: %.2f MB\n", (double)space_info.free_bytes / (1024 * 1024));
        printf("  使用率: %.1f%%\n", 
               ((double)space_info.used_bytes / space_info.total_bytes) * 100);
    }

    return 0;
}

static int cmd_sdcard_ls(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    const char* path = "/sdcard";
    if (argc > 1) {
        path = argv[1];
    }

    printf("列出目录: %s\n", path);

    DIR* dir = opendir(path);
    if (dir == NULL) {
        printf("无法打开目录: %s\n", path);
        return 1;
    }

    struct dirent* entry;
    int file_count = 0, dir_count = 0;
    printf("%-20s %10s %s\n", "名称", "大小", "类型");
    printf("----------------------------------------\n");
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[512];
        int path_len = snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        if (path_len >= sizeof(full_path)) {
            printf("%-20s %10s %s\n", entry->d_name, "ERR", "路径过长");
            continue;
        }
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                printf("%-20s %10s %s\n", entry->d_name, "<DIR>", "目录");
                dir_count++;
            } else {
                printf("%-20s %10ld %s\n", entry->d_name, st.st_size, "文件");
                file_count++;
            }
        } else {
            printf("%-20s %10s %s\n", entry->d_name, "?", "未知");
        }
    }
    
    closedir(dir);
    printf("----------------------------------------\n");
    printf("总计: %d 个文件, %d 个目录\n", file_count, dir_count);

    return 0;
}

static int cmd_sdcard_format(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，无法格式化\n");
        return 1;
    }

    printf("警告: 格式化将删除SD卡上的所有数据！\n");
    printf("如果确定要格式化，请输入 'YES': ");
    
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin) == NULL) {
        printf("输入错误\n");
        return 1;
    }

    // 移除换行符
    char* newline = strchr(confirm, '\n');
    if (newline) *newline = '\0';

    if (strcmp(confirm, "YES") != 0) {
        printf("格式化已取消\n");
        return 0;
    }

    printf("正在格式化SD卡...\n");
    esp_err_t ret = sdcard_format();
    if (ret == ESP_OK) {
        printf("SD卡格式化成功\n");
    } else {
        printf("SD卡格式化失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    return 0;
}

static int cmd_sdcard_cat(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: sdcard_cat <文件路径>\n");
        printf("提示: 如果不是绝对路径，会自动加上 /sdcard/ 前缀\n");
        return 1;
    }

    const char* input_path = argv[1];
    char file_path[512];
    
    // 如果路径不是以 / 开头，自动加上 /sdcard/ 前缀
    if (input_path[0] != '/') {
        snprintf(file_path, sizeof(file_path), "/sdcard/%s", input_path);
    } else {
        strncpy(file_path, input_path, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
    }
    
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        printf("无法打开文件: %s\n", file_path);
        printf("提示: 请确认文件名和路径正确，注意大小写\n");
        return 1;
    }

    printf("文件内容: %s\n", file_path);
    printf("----------------------------------------\n");
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        printf("%s", buffer);
    }
    
    fclose(file);
    printf("\n----------------------------------------\n");
    
    return 0;
}

static int cmd_sdcard_write(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 3) {
        printf("用法: sdcard_write <文件路径> <内容>\n");
        printf("示例: sdcard_write test.txt \"Hello World\"\n");
        printf("提示: 如果不是绝对路径，会自动加上 /sdcard/ 前缀\n");
        return 1;
    }

    const char* input_path = argv[1];
    const char* content = argv[2];
    char file_path[512];
    
    // 如果路径不是以 / 开头，自动加上 /sdcard/ 前缀
    if (input_path[0] != '/') {
        snprintf(file_path, sizeof(file_path), "/sdcard/%s", input_path);
    } else {
        strncpy(file_path, input_path, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
    }
    
    FILE* file = fopen(file_path, "w");
    if (file == NULL) {
        printf("无法创建文件: %s\n", file_path);
        printf("提示: 请检查目录是否存在，文件名是否有效\n");
        return 1;
    }

    fprintf(file, "%s\n", content);
    fclose(file);
    
    printf("内容已写入文件: %s\n", file_path);
    return 0;
}

static int cmd_sdcard_append(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 3) {
        printf("用法: sdcard_append <文件路径> <内容>\n");
        printf("示例: sdcard_append /sdcard/log.txt \"新的日志条目\"\n");
        return 1;
    }

    const char* file_path = argv[1];
    const char* content = argv[2];
    
    FILE* file = fopen(file_path, "a");
    if (file == NULL) {
        printf("无法打开文件: %s\n", file_path);
        return 1;
    }

    time_t now = time(NULL);
    struct tm* timeinfo = localtime(&now);
    fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d] %s\n", 
            timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, content);
    fclose(file);
    
    printf("内容已追加到文件: %s\n", file_path);
    return 0;
}

static int cmd_sdcard_rm(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: sdcard_rm <文件路径>\n");
        printf("提示: 如果不是绝对路径，会自动加上 /sdcard/ 前缀\n");
        return 1;
    }

    const char* input_path = argv[1];
    char file_path[512];
    
    // 如果路径不是以 / 开头，自动加上 /sdcard/ 前缀
    if (input_path[0] != '/') {
        snprintf(file_path, sizeof(file_path), "/sdcard/%s", input_path);
    } else {
        strncpy(file_path, input_path, sizeof(file_path) - 1);
        file_path[sizeof(file_path) - 1] = '\0';
    }
    
    // 检查文件是否存在
    struct stat st;
    if (stat(file_path, &st) != 0) {
        printf("文件不存在: %s\n", file_path);
        printf("提示: 请确认文件名和路径正确，注意大小写\n");
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        printf("错误: %s 是目录，请使用 sdcard_rmdir 删除目录\n", file_path);
        return 1;
    }

    if (unlink(file_path) == 0) {
        printf("文件已删除: %s\n", file_path);
    } else {
        printf("删除文件失败: %s\n", file_path);
        return 1;
    }
    
    return 0;
}

static int cmd_sdcard_mkdir(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: sdcard_mkdir <目录路径>\n");
        printf("提示: 如果不是绝对路径，会自动加上 /sdcard/ 前缀\n");
        return 1;
    }

    const char* input_path = argv[1];
    char dir_path[512];
    
    // 如果路径不是以 / 开头，自动加上 /sdcard/ 前缀
    if (input_path[0] != '/') {
        snprintf(dir_path, sizeof(dir_path), "/sdcard/%s", input_path);
    } else {
        strncpy(dir_path, input_path, sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
    }
    
    if (mkdir(dir_path, 0755) == 0) {
        printf("目录已创建: %s\n", dir_path);
    } else {
        printf("创建目录失败: %s\n", dir_path);
        printf("提示: 请检查父目录是否存在，路径是否有效\n");
        return 1;
    }
    
    return 0;
}

static int cmd_sdcard_rmdir(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: sdcard_rmdir <目录路径>\n");
        printf("提示: 如果不是绝对路径，会自动加上 /sdcard/ 前缀\n");
        return 1;
    }

    const char* input_path = argv[1];
    char dir_path[512];
    
    // 如果路径不是以 / 开头，自动加上 /sdcard/ 前缀
    if (input_path[0] != '/') {
        snprintf(dir_path, sizeof(dir_path), "/sdcard/%s", input_path);
    } else {
        strncpy(dir_path, input_path, sizeof(dir_path) - 1);
        dir_path[sizeof(dir_path) - 1] = '\0';
    }
    
    // 检查是否为目录
    struct stat st;
    if (stat(dir_path, &st) != 0) {
        printf("目录不存在: %s\n", dir_path);
        printf("提示: 请确认目录名和路径正确，注意大小写\n");
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        printf("错误: %s 不是目录\n", dir_path);
        return 1;
    }

    if (rmdir(dir_path) == 0) {
        printf("目录已删除: %s\n", dir_path);
    } else {
        printf("删除目录失败: %s (目录可能不为空)\n", dir_path);
        printf("提示: 只能删除空目录，请先删除目录中的所有文件\n");
        return 1;
    }
    
    return 0;
}

static int cmd_sdcard_cp(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 3) {
        printf("用法: sdcard_cp <源文件> <目标文件>\n");
        return 1;
    }

    const char* src_path = argv[1];
    const char* dst_path = argv[2];
    
    FILE* src = fopen(src_path, "rb");
    if (src == NULL) {
        printf("无法打开源文件: %s\n", src_path);
        return 1;
    }

    FILE* dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        printf("无法创建目标文件: %s\n", dst_path);
        fclose(src);
        return 1;
    }

    char buffer[1024];
    size_t bytes_read;
    size_t total_bytes = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            printf("写入文件失败\n");
            fclose(src);
            fclose(dst);
            return 1;
        }
        total_bytes += bytes_read;
    }
    
    fclose(src);
    fclose(dst);
    
    printf("文件复制成功: %s -> %s (%zu 字节)\n", src_path, dst_path, total_bytes);
    return 0;
}

static int cmd_sdcard_stat(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: sdcard_stat <文件或目录路径>\n");
        return 1;
    }

    const char* path = argv[1];
    struct stat st;
    
    if (stat(path, &st) != 0) {
        printf("无法获取信息: %s\n", path);
        return 1;
    }

    printf("文件信息: %s\n", path);
    printf("----------------------------------------\n");
    printf("类型: %s\n", S_ISDIR(st.st_mode) ? "目录" : "文件");
    printf("大小: %ld 字节\n", st.st_size);
    printf("修改时间: %s", ctime(&st.st_mtime));
    printf("权限: %lo\n", (unsigned long)(st.st_mode & 0777));
    
    return 0;
}

// ==================== SD卡 Shell 模式实现 ====================

static void normalize_path(char* path) {
    // 简单的路径规范化：移除末尾的 '/' (除了根目录)
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

static bool is_valid_path(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static bool check_shell_mode(void) {
    if (!s_sdcard_shell.in_shell_mode) {
        printf("请先执行 'sdcard_shell' 进入Shell模式\n");
        return false;
    }
    return true;
}

static int safe_path_join(char* dest, size_t dest_size, const char* base, const char* name) {
    if (strcmp(base, "/sdcard") == 0) {
        return snprintf(dest, dest_size, "/sdcard/%s", name);
    } else {
        return snprintf(dest, dest_size, "%s/%s", base, name);
    }
}

// 简化的路径构建宏
#define BUILD_PATH(dest, input) do { \
    if ((input)[0] != '/') { \
        if (safe_path_join(dest, sizeof(dest), s_sdcard_shell.current_path, input) >= (int)sizeof(dest)) { \
            printf("路径过长\n"); \
            return 1; \
        } \
    } else { \
        if (strlen(input) >= sizeof(dest)) { \
            printf("路径过长\n"); \
            return 1; \
        } \
        strcpy(dest, input); \
    } \
} while(0)

static int cmd_sdcard_shell(int argc, char **argv)
{
    if (sdcard_get_status() != SDCARD_STATUS_MOUNTED) {
        printf("SD卡未挂载，请先执行 'sdcard_mount'\n");
        return 1;
    }

    if (s_sdcard_shell.in_shell_mode) {
        printf("已经在SD卡Shell模式中\n");
        return 0;
    }

    // 进入Shell模式
    s_sdcard_shell.in_shell_mode = true;
    strcpy(s_sdcard_shell.current_path, "/sdcard");
    strcpy(s_sdcard_shell.previous_path, "/sdcard");
    
    // 注册Shell命令
    sdcard_shell_register_commands();
    
    printf("进入SD卡Shell模式\n");
    printf("========================================\n");
    printf("欢迎使用 SD Card Shell 操作环境！\n");
    printf("类似 Linux 的文件操作体验\n");
    printf("当前目录: %s\n", s_sdcard_shell.current_path);
    printf("========================================\n");
    printf("可用命令:\n");
    printf("  pwd        - 显示当前目录\n");
    printf("  cd <dir>   - 切换目录 (支持 .., -, /)\n");
    printf("  ls [dir]   - 列出目录内容 (显示文件大小)\n");
    printf("  cat <file> - 查看文件内容\n");
    printf("  mkdir <dir>- 创建目录\n");
    printf("  rm <file>  - 删除文件\n");
    printf("  rmdir <dir>- 删除目录\n");
    printf("  cp <src> <dst> - 复制文件\n");
    printf("  write <file> <content> - 写入文件\n");
    printf("  stat <path> - 查看文件详细信息\n");
    printf("  exit       - 退出Shell模式\n");
    printf("========================================\n");
    show_shell_prompt();
    
    return 0;
}

static int cmd_shell_exit(int argc, char **argv)
{
    if (!s_sdcard_shell.in_shell_mode) {
        printf("当前不在SD卡Shell模式中\n");
        return 1;
    }

    // 退出Shell模式
    s_sdcard_shell.in_shell_mode = false;
    
    // 注销Shell命令
    sdcard_shell_unregister_commands();
    
    printf("退出SD卡Shell模式\n");
    
    // 注意：ESP-IDF控制台不支持动态修改提示符
    // 提示符将保持原状
    
    return 0;
}

static int cmd_shell_pwd(int argc, char **argv)
{
    if (!s_sdcard_shell.in_shell_mode) {
        printf("请先执行 'sdcard_shell' 进入Shell模式\n");
        return 1;
    }
    printf("%s\n", s_sdcard_shell.current_path);
    show_shell_prompt();
    return 0;
}

static int cmd_shell_cd(int argc, char **argv)
{
    if (!s_sdcard_shell.in_shell_mode) {
        printf("请先执行 'sdcard_shell' 进入Shell模式\n");
        return 1;
    }

    if (argc < 2) {
        printf("用法: cd <目录>\n");
        printf("特殊用法: cd .. (上级目录), cd - (上次目录), cd / (根目录)\n");
        return 1;
    }

    const char* target = argv[1];
    char new_path[256];
    
    if (strcmp(target, "..") == 0) {
        // 上级目录
        strcpy(new_path, s_sdcard_shell.current_path);
        char* last_slash = strrchr(new_path, '/');
        if (last_slash != NULL && last_slash != new_path) {
            *last_slash = '\0';
        } else {
            strcpy(new_path, "/sdcard");
        }
    } else if (strcmp(target, "-") == 0) {
        // 上次目录
        strcpy(new_path, s_sdcard_shell.previous_path);
    } else if (strcmp(target, "/") == 0) {
        // 根目录
        strcpy(new_path, "/sdcard");
    } else if (target[0] == '/') {
        // 绝对路径 - 限制长度
        if (strlen(target) >= sizeof(new_path)) {
            printf("路径过长\n");
            return 1;
        }
        strcpy(new_path, target);
    } else {
        // 相对路径 - 使用安全函数
        if (safe_path_join(new_path, sizeof(new_path), s_sdcard_shell.current_path, target) >= (int)sizeof(new_path)) {
            printf("路径过长\n");
            return 1;
        }
    }
    
    normalize_path(new_path);
    
    // 检查目录是否存在
    if (!is_valid_path(new_path)) {
        printf("目录不存在: %s\n", new_path);
        return 1;
    }
    
    // 更新路径
    strcpy(s_sdcard_shell.previous_path, s_sdcard_shell.current_path);
    strcpy(s_sdcard_shell.current_path, new_path);
    
    printf("当前目录: %s\n", s_sdcard_shell.current_path);
    show_shell_prompt();
    
    return 0;
}

static int cmd_shell_ls(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;
    
    const char* path = (argc > 1) ? argv[1] : s_sdcard_shell.current_path;
    char full_path[256];
    
    // 简化路径处理
    if (path[0] != '/') {
        if (safe_path_join(full_path, sizeof(full_path), s_sdcard_shell.current_path, path) >= (int)sizeof(full_path)) {
            printf("路径过长\n");
            return 1;
        }
        path = full_path;
    }

    DIR* dir = opendir(path);
    if (dir == NULL) {
        printf("无法打开目录: %s\n", path);
        return 1;
    }

    struct dirent* entry;
    int file_count = 0, dir_count = 0;
    printf("%-20s %10s %s\n", "名称", "大小", "类型");
    printf("----------------------------------------\n");
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 获取文件完整路径以获取文件大小
        char item_path[256];
        if (safe_path_join(item_path, sizeof(item_path), path, entry->d_name) < (int)sizeof(item_path)) {
            struct stat st;
            char size_str[16];
            
            if (stat(item_path, &st) == 0) {
                if (entry->d_type == DT_DIR) {
                    strcpy(size_str, "<DIR>");
                } else {
                    // 格式化文件大小
                    if (st.st_size < 1024) {
                        snprintf(size_str, sizeof(size_str), "%ldB", st.st_size);
                    } else if (st.st_size < 1024 * 1024) {
                        snprintf(size_str, sizeof(size_str), "%.1fK", (double)st.st_size / 1024);
                    } else {
                        snprintf(size_str, sizeof(size_str), "%.1fM", (double)st.st_size / (1024 * 1024));
                    }
                }
            } else {
                strcpy(size_str, "---");
            }
            
            printf("%-20s %10s %s\n", entry->d_name, size_str, 
                   (entry->d_type == DT_DIR) ? "目录" : "文件");
        } else {
            // 路径过长，使用简化显示
            printf("%-20s %10s %s\n", entry->d_name, 
                   (entry->d_type == DT_DIR) ? "<DIR>" : "---", 
                   (entry->d_type == DT_DIR) ? "目录" : "文件");
        }
        
        if (entry->d_type == DT_DIR) {
            dir_count++;
        } else {
            file_count++;
        }
    }
    
    closedir(dir);
    printf("----------------------------------------\n");
    printf("总计: %d 个文件, %d 个目录\n", file_count, dir_count);
    show_shell_prompt();

    return 0;
}

static int cmd_shell_cat(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;

    if (argc < 2) {
        printf("用法: cat <文件名>\n");
        return 1;
    }

    const char* filename = argv[1];
    char file_path[256];
    
    BUILD_PATH(file_path, filename);
    
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        printf("无法打开文件: %s\n", file_path);
        return 1;
    }

    printf("文件内容: %s\n", file_path);
    printf("----------------------------------------\n");
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        printf("%s", buffer);
    }
    
    fclose(file);
    printf("\n----------------------------------------\n");
    
    return 0;
}

static int cmd_shell_mkdir(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;

    if (argc < 2) {
        printf("用法: mkdir <目录名>\n");
        return 1;
    }

    const char* dirname = argv[1];
    char dir_path[256];
    BUILD_PATH(dir_path, dirname);
    
    if (mkdir(dir_path, 0755) == 0) {
        printf("目录已创建: %s\n", dir_path);
    } else {
        printf("创建目录失败: %s\n", dir_path);
        return 1;
    }
    
    show_shell_prompt();
    return 0;
}

static int cmd_shell_rm(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;

    if (argc < 2) {
        printf("用法: rm <文件名>\n");
        return 1;
    }

    const char* filename = argv[1];
    char file_path[256];
    BUILD_PATH(file_path, filename);
    
    if (unlink(file_path) == 0) {
        printf("文件已删除: %s\n", file_path);
        show_shell_prompt();
    } else {
        printf("删除文件失败: %s\n", file_path);
        show_shell_prompt();
        return 1;
    }
    
    return 0;
}

static int cmd_shell_rmdir(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;

    if (argc < 2) {
        printf("用法: rmdir <目录名>\n");
        return 1;
    }

    const char* dirname = argv[1];
    char dir_path[256];
    BUILD_PATH(dir_path, dirname);
    
    if (rmdir(dir_path) == 0) {
        printf("目录已删除: %s\n", dir_path);
        show_shell_prompt();
    } else {
        printf("删除目录失败: %s (目录可能不为空)\n", dir_path);
        show_shell_prompt();
        return 1;
    }
    
    return 0;
}

static int cmd_shell_write(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;

    if (argc < 3) {
        printf("用法: write <文件名> <内容>\n");
        return 1;
    }

    const char* filename = argv[1];
    const char* content = argv[2];
    char file_path[256];
    BUILD_PATH(file_path, filename);
    
    FILE* file = fopen(file_path, "w");
    if (file == NULL) {
        printf("无法创建文件: %s\n", file_path);
        show_shell_prompt();
        return 1;
    }

    fprintf(file, "%s\n", content);
    fclose(file);
    
    printf("内容已写入文件: %s\n", file_path);
    show_shell_prompt();
    return 0;
}

// 简化cp和stat命令
static int cmd_shell_cp(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;
    
    if (argc < 3) {
        printf("用法: cp <源文件> <目标文件>\n");
        return 1;
    }

    const char* src_name = argv[1];
    const char* dst_name = argv[2];
    char src_path[256], dst_path[256];
    
    BUILD_PATH(src_path, src_name);
    BUILD_PATH(dst_path, dst_name);
    
    // 检查源文件是否存在
    struct stat src_stat;
    if (stat(src_path, &src_stat) != 0) {
        printf("源文件不存在: %s\n", src_path);
        return 1;
    }
    
    if (S_ISDIR(src_stat.st_mode)) {
        printf("错误: %s 是目录，暂不支持目录复制\n", src_path);
        return 1;
    }
    
    FILE* src = fopen(src_path, "rb");
    if (src == NULL) {
        printf("无法打开源文件: %s\n", src_path);
        return 1;
    }

    FILE* dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        printf("无法创建目标文件: %s\n", dst_path);
        fclose(src);
        return 1;
    }

    char buffer[1024];
    size_t bytes_read;
    size_t total_bytes = 0;
    
    printf("正在复制 %s -> %s ...\n", src_path, dst_path);
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst) != bytes_read) {
            printf("写入文件失败\n");
            fclose(src);
            fclose(dst);
            return 1;
        }
        total_bytes += bytes_read;
        
        // 显示进度（每64KB显示一次）
        if (total_bytes % (64 * 1024) == 0 || bytes_read < sizeof(buffer)) {
            if (src_stat.st_size > 0) {
                int progress = (int)((total_bytes * 100) / src_stat.st_size);
                printf("\r进度: %d%% (%zu/%ld 字节)", progress, total_bytes, src_stat.st_size);
                fflush(stdout);
            }
        }
    }
    
    fclose(src);
    fclose(dst);
    
    printf("\n文件复制成功: %s -> %s (%zu 字节)\n", src_path, dst_path, total_bytes);
    show_shell_prompt();
    return 0;
}

static int cmd_shell_stat(int argc, char **argv)
{
    if (!check_shell_mode()) return 1;
    
    if (argc < 2) {
        printf("用法: stat <文件或目录名>\n");
        return 1;
    }

    const char* name = argv[1];
    char item_path[256];
    BUILD_PATH(item_path, name);
    
    struct stat st;
    if (stat(item_path, &st) != 0) {
        printf("无法获取信息: %s\n", item_path);
        return 1;
    }

    printf("文件信息: %s\n", item_path);
    printf("========================================\n");
    printf("类型: %s\n", S_ISDIR(st.st_mode) ? "目录" : "普通文件");
    
    if (S_ISDIR(st.st_mode)) {
        printf("大小: <目录>\n");
    } else {
        printf("大小: %ld 字节", st.st_size);
        if (st.st_size >= 1024) {
            if (st.st_size < 1024 * 1024) {
                printf(" (%.2f KB)", (double)st.st_size / 1024);
            } else {
                printf(" (%.2f MB)", (double)st.st_size / (1024 * 1024));
            }
        }
        printf("\n");
    }
    
    printf("修改时间: %s", ctime(&st.st_mtime));
    printf("访问权限: %o\n", (unsigned)(st.st_mode & 0777));
    
    // 显示权限的可读形式
    printf("权限详情: ");
    printf("%c", S_ISDIR(st.st_mode) ? 'd' : '-');
    printf("%c%c%c", 
           (st.st_mode & S_IRUSR) ? 'r' : '-',
           (st.st_mode & S_IWUSR) ? 'w' : '-',
           (st.st_mode & S_IXUSR) ? 'x' : '-');
    printf("%c%c%c", 
           (st.st_mode & S_IRGRP) ? 'r' : '-',
           (st.st_mode & S_IWGRP) ? 'w' : '-',
           (st.st_mode & S_IXGRP) ? 'x' : '-');
    printf("%c%c%c\n", 
           (st.st_mode & S_IROTH) ? 'r' : '-',
           (st.st_mode & S_IWOTH) ? 'w' : '-',
           (st.st_mode & S_IXOTH) ? 'x' : '-');
    
    printf("========================================\n");
    show_shell_prompt();
    
    return 0;
}

static void sdcard_shell_register_commands(void)
{
    // Shell模式命令
    const esp_console_cmd_t shell_commands[] = {
        {.command = "exit", .help = "退出SD卡Shell模式", .func = &cmd_shell_exit},
        {.command = "pwd", .help = "显示当前目录", .func = &cmd_shell_pwd},
        {.command = "cd", .help = "切换目录", .hint = "<目录>", .func = &cmd_shell_cd},
        {.command = "ls", .help = "列出目录内容", .hint = "[目录]", .func = &cmd_shell_ls},
        {.command = "cat", .help = "查看文件内容", .hint = "<文件>", .func = &cmd_shell_cat},
        {.command = "mkdir", .help = "创建目录", .hint = "<目录>", .func = &cmd_shell_mkdir},
        {.command = "rm", .help = "删除文件", .hint = "<文件>", .func = &cmd_shell_rm},
        {.command = "rmdir", .help = "删除目录", .hint = "<目录>", .func = &cmd_shell_rmdir},
        {.command = "cp", .help = "复制文件", .hint = "<源> <目标>", .func = &cmd_shell_cp},
        {.command = "write", .help = "写入文件", .hint = "<文件> <内容>", .func = &cmd_shell_write},
        {.command = "stat", .help = "查看文件详情", .hint = "<文件>", .func = &cmd_shell_stat},
    };

    for (int i = 0; i < sizeof(shell_commands) / sizeof(shell_commands[0]); i++) {
        esp_console_cmd_register(&shell_commands[i]);
    }
}

static void sdcard_shell_unregister_commands(void)
{
    // 注意：ESP-IDF console组件不支持动态命令注销
    // Shell命令将保持注册状态，但通过 in_shell_mode 标志控制行为
    // 这是一个设计权衡，避免了复杂的命令管理
    ESP_LOGI(TAG, "SD card shell commands remain registered (ESP-IDF limitation)");
}

esp_err_t console_interface_register_sdcard_commands(void)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // SD卡相关命令
    const esp_console_cmd_t sdcard_commands[] = {
        {
            .command = "sdcard_mount",
            .help = "挂载SD卡到文件系统",
            .hint = "[mount_point]",
            .func = &cmd_sdcard_mount,
            .argtable = NULL
        },
        {
            .command = "sdcard_unmount", 
            .help = "卸载SD卡",
            .hint = NULL,
            .func = &cmd_sdcard_unmount,
            .argtable = NULL
        },
        {
            .command = "sdcard_info",
            .help = "显示SD卡信息",
            .hint = NULL,
            .func = &cmd_sdcard_info,
            .argtable = NULL
        },
        {
            .command = "sdcard_ls",
            .help = "列出SD卡目录内容",
            .hint = "[path]",
            .func = &cmd_sdcard_ls,
            .argtable = NULL
        },
        {
            .command = "sdcard_format",
            .help = "格式化SD卡 (危险操作!)",
            .hint = NULL,
            .func = &cmd_sdcard_format,
            .argtable = NULL
        },
        {
            .command = "sdcard_cat",
            .help = "查看文件内容",
            .hint = "<file_path>",
            .func = &cmd_sdcard_cat,
            .argtable = NULL
        },
        {
            .command = "sdcard_write",
            .help = "写入内容到文件",
            .hint = "<file_path> <content>",
            .func = &cmd_sdcard_write,
            .argtable = NULL
        },
        {
            .command = "sdcard_append",
            .help = "追加内容到文件(带时间戳)",
            .hint = "<file_path> <content>",
            .func = &cmd_sdcard_append,
            .argtable = NULL
        },
        {
            .command = "sdcard_rm",
            .help = "删除文件",
            .hint = "<file_path>",
            .func = &cmd_sdcard_rm,
            .argtable = NULL
        },
        {
            .command = "sdcard_mkdir",
            .help = "创建目录",
            .hint = "<dir_path>",
            .func = &cmd_sdcard_mkdir,
            .argtable = NULL
        },
        {
            .command = "sdcard_rmdir",
            .help = "删除空目录",
            .hint = "<dir_path>",
            .func = &cmd_sdcard_rmdir,
            .argtable = NULL
        },
        {
            .command = "sdcard_cp",
            .help = "复制文件",
            .hint = "<src_file> <dst_file>",
            .func = &cmd_sdcard_cp,
            .argtable = NULL
        },
        {
            .command = "sdcard_stat",
            .help = "显示文件/目录详细信息",
            .hint = "<path>",
            .func = &cmd_sdcard_stat,
            .argtable = NULL
        },
        {
            .command = "sdcard_shell",
            .help = "进入SD卡Shell操作模式 (类似Linux环境)",
            .hint = NULL,
            .func = &cmd_sdcard_shell,
            .argtable = NULL
        }
    };

    // 注册命令
    for (int i = 0; i < sizeof(sdcard_commands) / sizeof(sdcard_commands[0]); i++) {
        esp_err_t ret = esp_console_cmd_register(&sdcard_commands[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register SD card command '%s': %s", 
                     sdcard_commands[i].command, esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "SD card commands registered successfully");
    return ESP_OK;
}

esp_err_t console_interface_execute_command(const char *command)
{
    if (!command) {
        return ESP_ERR_INVALID_ARG;
    }

    int ret;
    esp_err_t err = esp_console_run(command, &ret);
    
    if (err == ESP_OK) {
        s_console_state.commands_executed++;
        trigger_console_event(CONSOLE_EVENT_COMMAND_SUCCESS, command);
    } else {
        trigger_console_event(CONSOLE_EVENT_COMMAND_ERROR, command);
    }

    return err;
}

void console_interface_print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

void console_interface_print_prompt(void)
{
    printf("%s", s_console_state.config.prompt);
    fflush(stdout);
}

void console_interface_show_banner(void)
{
    printf("\n=== ESP32S3 组件化控制台程序启动 ===\n");
    printf("组件化控制台已启动，等待命令输入...\n");
    printf("新功能：配置保存/加载、统一设备接口、系统监控\n");
    printf("输入 'help' 查看可用命令\n");
    printf("提示：使用 TAB 键自动补全命令，上下箭头键浏览历史命令\n\n");
}

bool console_interface_is_ready(void)
{
    return s_console_state.initialized && s_console_state.running;
}

esp_err_t console_interface_get_stats(uint32_t *commands_executed, uint64_t *uptime_ms)
{
    if (!s_console_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (commands_executed) {
        *commands_executed = s_console_state.commands_executed;
    }

    if (uptime_ms) {
        *uptime_ms = get_time_ms() - s_console_state.start_time_ms;
    }

    return ESP_OK;
}

// ========== 命令实现函数 ==========

static int cmd_help(int argc, char **argv)
{
    printf("\n=== ESP32S3 组件化控制台可用命令 ===\n");
    printf("系统命令:\n");
    printf("  help          - 显示此帮助信息\n");
    printf("  info          - 显示系统信息\n");
    printf("  status        - 显示当前状态\n");
    printf("  reboot        - 重启系统\n");
    printf("\n配置管理:\n");
    printf("  save          - 保存当前配置到NVS\n");
    printf("  load          - 从NVS加载配置\n");
    printf("  clear         - 清除NVS中的配置\n");
    printf("  config        - 高级配置管理 (详细用法: config)\n");
    printf("  defaults      - 默认参数管理 (详细用法: defaults)\n");
    printf("  注意: 网络配置(以太网/DHCP/网关)会自动保存到NVS\n");
    printf("\n风扇控制:\n");
    printf("  fan <0-100>   - 设置风扇速度 (0-100%%)\n");
    printf("  fan off       - 关闭风扇\n");
    printf("  fan on        - 打开风扇(50%%)\n");
    printf("\n板载LED控制 (28颗WS2812):\n");
    printf("  bled <r> <g> <b>     - 设置板载LED颜色 (0-255)\n");
    printf("  bled bright <0-100>  - 设置板载LED亮度\n");
    printf("  bled off             - 关闭板载LED\n");
    printf("  bled rainbow         - 彩虹效果\n");
    printf("\n触摸LED控制 (1颗WS2812):\n");
    printf("  tled <r> <g> <b>     - 设置触摸LED颜色 (0-255)\n");
    printf("  tled bright <0-100>  - 设置触摸LED亮度\n");
    printf("  tled off             - 关闭触摸LED\n");
    printf("\nGPIO控制:\n");
    printf("  gpio <pin> high      - 设置GPIO引脚为高电平\n");
    printf("  gpio <pin> low       - 设置GPIO引脚为低电平\n");
    printf("  gpio <pin> input     - 切换到输入模式并读取状态\n");
    printf("\nUSB MUX控制:\n");
    printf("  usbmux esp32s3       - 切换USB-C接口到ESP32S3\n");
    printf("  usbmux agx           - 切换USB-C接口到AGX\n");
    printf("  usbmux lpmu          - 切换USB-C接口到LPMU\n");
    printf("  usbmux status        - 显示当前USB接口状态\n");
    printf("\nAGX设备控制:\n");
    printf("  agx on              - 开机AGX设备\n");
    printf("  agx off             - 关机AGX设备\n");
    printf("  agx reset           - 重启AGX设备\n");
    printf("  agx recovery        - 进入恢复模式并切换USB到AGX\n");
    printf("  agx status          - 显示AGX电源状态\n");
    printf("\nLPMU设备控制:\n");
    printf("  lpmu toggle          - 切换LPMU开机/关机状态\n");
    printf("  lpmu reset           - 重启LPMU设备\n");
    printf("  lpmu status          - 显示LPMU电源状态\n");
    printf("\n测试命令:\n");
    printf("  test fan             - 测试风扇功能\n");
    printf("  test bled            - 测试板载LED\n");
    printf("  test tled            - 测试触摸LED\n");
    printf("  test gpio <pin>      - 安全测试GPIO输出功能\n");
    printf("  test gpio_input <pin> - 测试GPIO输入功能\n");
    printf("  test agx            - 测试AGX电源控制功能\n");
    printf("  test lpmu            - 测试LPMU电源控制功能\n");
    printf("  test all             - 测试所有硬件\n");
    printf("  test quick           - 快速测试\n");
    printf("  test stress <ms>     - 压力测试\n");
    printf("\n以太网控制:\n");
    printf("  eth_config           - 显示当前以太网配置\n");
    printf("  eth_config show      - 显示当前以太网配置\n");
    printf("  eth_config reload    - 从NVS重新载入配置\n");
    printf("  eth_status           - 显示以太网接口状态\n");
    printf("  eth_reset            - 重置以太网接口\n");
    printf("  eth_ping <IP>        - ping测试网络连通性\n");
    printf("  eth_test             - 测试W5500芯片SPI通信\n");
    printf("\nDHCP服务器控制:\n");
    printf("  eth_dhcp             - 显示DHCP服务器状态和客户端列表\n");
    printf("  eth_dhcp status      - 显示DHCP服务器状态\n");
    printf("  eth_dhcp start       - 启动DHCP服务器\n");
    printf("  eth_dhcp stop        - 停止DHCP服务器\n");
    printf("  eth_dhcp restart     - 重启DHCP服务器\n");
    printf("\n网关服务控制:\n");
    printf("  eth_gateway status   - 显示网关服务状态\n");
    printf("  eth_gateway start    - 启动网关服务\n");
    printf("  eth_gateway stop     - 停止网关服务\n");
    printf("\nTF卡存储控制 (13个命令):\n");
    printf("  📁 基本管理:\n");
    printf("  sdcard_mount [path]   - 挂载TF卡到文件系统 (默认/sdcard)\n");
    printf("  sdcard_unmount       - 安全卸载TF卡\n");
    printf("  sdcard_info          - 显示TF卡详细信息 (容量/使用率)\n");
    printf("  sdcard_ls [path]     - 列出目录内容 (增强版，显示大小)\n");
    printf("  sdcard_format        - 格式化TF卡为FAT32 ⚠️危险操作\n");
    printf("  📄 文件操作:\n");
    printf("  sdcard_cat <file>    - 查看文件内容\n");
    printf("  sdcard_write <file> <content> - 写入内容到文件\n");
    printf("  sdcard_append <file> <content> - 追加内容 (带时间戳)\n");
    printf("  sdcard_rm <file>     - 删除文件\n");
    printf("  sdcard_cp <src> <dst> - 复制文件\n");
    printf("  📂 目录操作:\n");
    printf("  sdcard_mkdir <dir>   - 创建目录\n");
    printf("  sdcard_rmdir <dir>   - 删除空目录\n");
    printf("  📊 信息查询:\n");
    printf("  sdcard_stat <path>   - 显示文件/目录详细信息\n");
    printf("  🖥️  Shell模式:\n");
    printf("  sdcard_shell         - 进入类Linux操作环境 (cd/pwd/ls/等)\n");
    printf("\n🔧 配置命令 (自动保存):\n");
    printf("  config set eth <ip> <gw> <mask> <dns>    - 设置以太网配置\n");
    printf("  config set dhcp <enable> <start> <end> <lease> - 设置DHCP参数\n");
    printf("  config set gateway <enable> <nat> <firewall>   - 设置网关参数\n");
    printf("  defaults eth         - 应用以太网默认配置\n");
    printf("  defaults dhcp        - 应用DHCP默认配置\n");
    printf("  defaults gateway     - 应用网关默认配置\n");
    printf("\n注意：\n");
    printf("  • 使用 TAB 键自动补全，上下箭头浏览历史\n");
    printf("  • GPIO输入操作使用 'input' 参数以避免状态干扰\n");
    printf("  • LED RGB值范围: 0-255, 亮度范围: 0-100%%\n");
    printf("  • 风扇速度范围: 0-100%%, PWM频率: 25kHz\n");
    printf("  • 以太网默认配置: IP 10.10.99.97, DHCP池 10.10.99.101-110\n");
    printf("  • TF卡接口: SDMMC 4-bit, GPIO 4,5,6,7,15,16, 支持FAT32\n");
    printf("  ✅ 网络配置(eth/dhcp/gateway)修改后自动保存，无需手动save\n");
    printf("  ⚠️  硬件配置(风扇/LED)修改后需要手动执行 'save' 命令保存\n");
    printf("  📖 TF卡详细使用指南: markdown/SDCARD_CONSOLE_COMMANDS.md\n");
    printf("========================================\n");
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    device_print_full_status();
    
    // 显示控制台统计
    uint32_t commands_executed;
    uint64_t uptime_ms;
    if (console_interface_get_stats(&commands_executed, &uptime_ms) == ESP_OK) {
        printf("\n=== 控制台统计 ===\n");
        printf("已执行命令数: %" PRIu32 "\n", commands_executed);
        printf("控制台运行时间: %" PRIu64 " ms\n", uptime_ms);
        printf("=================\n");
    }
    
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    device_status_t status;
    esp_err_t ret = device_get_full_status(&status);
    if (ret != ESP_OK) {
        printf("获取设备状态失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("\n=== 当前状态 ===\n");
    if (status.hardware_available) {
        printf("风扇速度: %d%%\n", status.hardware.fan_speed);
        printf("板载LED亮度: %d%%\n", status.hardware.board_led_brightness);
        printf("触摸LED亮度: %d%%\n", status.hardware.touch_led_brightness);
    }
    if (status.monitor_available) {
        printf("可用堆内存: %" PRIu32 " bytes\n", status.system.free_heap);
        printf("运行时间: %" PRIu64 " ms\n", status.system.uptime_ms);
    }
    printf("=================\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    printf("系统重启中...\n");
    system_safe_restart(1000);
    return 0;
}

static int cmd_fan(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: fan <0-100> | on | off\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    if (strcmp(argv[1], "off") == 0) {
        ret = fan_stop();
        if (ret == ESP_OK) {
            // 更新配置管理器中的配置
            config_manager_set_fan_speed(0, 0);
        }
    }
    else if (strcmp(argv[1], "on") == 0) {
        ret = fan_start();
        if (ret == ESP_OK) {
            // 更新配置管理器中的配置为默认ON速度
            config_manager_set_fan_speed(50, 0);
        }
    }
    else {
        int speed = atoi(argv[1]);
        if (speed >= 0 && speed <= 100) {
            ret = fan_set_speed(speed);
            if (ret == ESP_OK) {
                // 更新配置管理器中的配置
                config_manager_set_fan_speed(speed, 0);
                printf("风扇速度已设置为 %d%% (配置已更新)\n", speed);
            }
        } else {
            printf("风扇速度必须在0-100之间\n");
            return 1;
        }
    }
    
    if (ret != ESP_OK) {
        printf("风扇控制失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_bled(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: bled <r> <g> <b> | bright <0-100> | off | rainbow\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    if (strcmp(argv[1], "off") == 0) {
        ret = board_led_turn_off();
    }
    else if (strcmp(argv[1], "bright") == 0) {
        if (argc < 3) {
            printf("用法: bled bright <0-100>\n");
            return 1;
        }
        int brightness = atoi(argv[2]);
        if (brightness >= 0 && brightness <= 100) {
            ret = board_led_set_brightness(brightness);
        } else {
            printf("亮度必须在0-100之间\n");
            return 1;
        }
    }
    else if (strcmp(argv[1], "rainbow") == 0) {
        ret = board_led_set_effect(LED_EFFECT_RAINBOW);
    }
    else if (argc >= 4) {
        int r = atoi(argv[1]);
        int g = atoi(argv[2]);
        int b = atoi(argv[3]);
        
        if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            led_color_t color = {r, g, b};
            ret = board_led_set_color(color);
        } else {
            printf("RGB值必须在0-255之间\n");
            return 1;
        }
    } else {
        printf("用法: bled <r> <g> <b> | bright <0-100> | off | rainbow\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("板载LED控制失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_tled(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: tled <r> <g> <b> | bright <0-100> | off\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    if (strcmp(argv[1], "off") == 0) {
        ret = touch_led_turn_off();
    }
    else if (strcmp(argv[1], "bright") == 0) {
        if (argc < 3) {
            printf("用法: tled bright <0-100>\n");
            return 1;
        }
        int brightness = atoi(argv[2]);
        if (brightness >= 0 && brightness <= 100) {
            ret = touch_led_set_brightness(brightness);
        } else {
            printf("亮度必须在0-100之间\n");
            return 1;
        }
    }
    else if (argc >= 4) {
        int r = atoi(argv[1]);
        int g = atoi(argv[2]);
        int b = atoi(argv[3]);
        
        if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
            led_color_t color = {r, g, b};
            ret = touch_led_set_color(color);
        } else {
            printf("RGB值必须在0-255之间\n");
            return 1;
        }
    } else {
        printf("用法: tled <r> <g> <b> | bright <0-100> | off\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("触摸LED控制失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_gpio(int argc, char **argv)
{
    if (argc < 3) {
        printf("用法: gpio <pin> high|low|input\n");
        return 1;
    }
    
    int pin = atoi(argv[1]);
    esp_err_t ret = ESP_OK;
    
    if (strcmp(argv[2], "high") == 0) {
        ret = gpio_set_output(pin, GPIO_STATE_HIGH);
        if (ret == ESP_OK) {
            printf("GPIO%d 已设置为高电平\n", pin);
        }
    }
    else if (strcmp(argv[2], "low") == 0) {
        ret = gpio_set_output(pin, GPIO_STATE_LOW);
        if (ret == ESP_OK) {
            printf("GPIO%d 已设置为低电平\n", pin);
        }
    }
    else if (strcmp(argv[2], "input") == 0) {
        gpio_state_t state;
        ret = gpio_read_input_mode(pin, &state);
        if (ret == ESP_OK) {
            printf("GPIO%d 输入电平: %s\n", pin, state ? "高" : "低");
        }
    }
    else {
        printf("用法: gpio <pin> high|low|input\n");
        printf("注意: 'input' 将GPIO设置为输入模式并读取状态\n");
        printf("      避免在输出模式下进行状态读取以防止干扰\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("GPIO操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_usbmux(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: usbmux esp32s3|agx|n305|status\n");
        return 1;
    }
    
    // 检查硬件控制是否已初始化
    if (!hardware_control_is_initialized()) {
        printf("错误: 硬件控制组件未初始化\n");
        printf("请检查设备接口初始化状态\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(argv[1], "esp32s3") == 0) {
        ret = usb_mux_set_target(USB_MUX_ESP32S3);
        if (ret == ESP_OK) {
            printf("USB-C接口已切换到ESP32S3\n");
        }
    }
    else if (strcmp(argv[1], "agx") == 0) {
        ret = usb_mux_set_target(USB_MUX_AGX);
        if (ret == ESP_OK) {
            printf("USB-C接口已切换到AGX\n");
        }
    }
    else if (strcmp(argv[1], "lpmu") == 0) {
        ret = usb_mux_set_target(USB_MUX_LPMU);
        if (ret == ESP_OK) {
            printf("USB-C接口已切换到LPMU\n");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        usb_mux_target_t current_target;
        ret = usb_mux_get_target(&current_target);
        if (ret == ESP_OK) {
            printf("当前USB-C接口连接到: %s\n", usb_mux_get_target_name(current_target));
        }
    }
    else {
        printf("用法: usbmux esp32s3|agx|lpmu|status\n");
        printf("  esp32s3 - 切换到ESP32S3 USB接口\n");
        printf("  agx     - 切换到AGX USB接口\n");
        printf("  lpmu    - 切换到LPMU USB接口\n");
        printf("  status  - 显示当前USB接口状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("USB MUX操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_agx(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: agx on|off|reset|recovery|status\n");
        return 1;
    }
    
    // 检查硬件控制是否已初始化
    if (!hardware_control_is_initialized()) {
        printf("错误: 硬件控制组件未初始化\n");
        printf("请检查设备接口初始化状态\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(argv[1], "on") == 0) {
        ret = agx_power_on();
        if (ret == ESP_OK) {
            printf("AGX设备已开机\n");
        }
    }
    else if (strcmp(argv[1], "off") == 0) {
        ret = agx_power_off();
        if (ret == ESP_OK) {
            printf("AGX设备已关机\n");
        }
    }
    else if (strcmp(argv[1], "reset") == 0) {
        printf("正在重启AGX设备...\n");
        ret = agx_reset();
        if (ret == ESP_OK) {
            printf("AGX设备重启完成\n");
        }
    }
    else if (strcmp(argv[1], "recovery") == 0) {
        printf("正在进入AGX恢复模式...\n");
        ret = agx_enter_recovery_mode();
        if (ret == ESP_OK) {
            printf("AGX设备已进入恢复模式\n");
            printf("USB-C接口已自动切换到AGX\n");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        power_state_t state;
        ret = agx_get_power_state(&state);
        if (ret == ESP_OK) {
            printf("AGX电源状态: %s\n", power_state_get_name(state));
        }
    }
    else {
        printf("用法: agx on|off|reset|recovery|status\n");
        printf("  on       - 开机AGX设备\n");
        printf("  off      - 关机AGX设备\n");
        printf("  reset    - 重启AGX设备\n");
        printf("  recovery - 进入恢复模式并切换USB到AGX\n");
        printf("  status   - 显示AGX电源状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("AGX操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_lpmu(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: lpmu toggle|reset|status\n");
        return 1;
    }
    
    // 检查硬件控制是否已初始化
    if (!hardware_control_is_initialized()) {
        printf("错误: 硬件控制组件未初始化\n");
        printf("请检查设备接口初始化状态\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(argv[1], "toggle") == 0) {
        printf("正在切换LPMU电源状态...\n");
        ret = lpmu_power_toggle();
        if (ret == ESP_OK) {
            power_state_t state;
            if (lpmu_get_power_state(&state) == ESP_OK) {
                printf("LPMU电源已切换到: %s\n", power_state_get_name(state));
            } else {
                printf("LPMU电源状态已切换\n");
            }
        }
    }
    else if (strcmp(argv[1], "reset") == 0) {
        printf("正在重启LPMU设备...\n");
        ret = lpmu_reset();
        if (ret == ESP_OK) {
            printf("LPMU设备重启完成\n");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        power_state_t state;
        ret = lpmu_get_power_state(&state);
        if (ret == ESP_OK) {
            printf("LPMU电源状态: %s\n", power_state_get_name(state));
        }
    }
    else {
        printf("用法: lpmu toggle|reset|status\n");
        printf("  toggle - 切换LPMU开机/关机状态\n");
        printf("  reset  - 重启LPMU设备\n");
        printf("  status - 显示LPMU电源状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("LPMU操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: test fan|bled|tled|gpio <pin>|gpio_input <pin>|agx|lpmu|all|quick|stress <ms>\n");
        return 1;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (strcmp(argv[1], "fan") == 0) {
        ret = hardware_test_fan();
    }
    else if (strcmp(argv[1], "bled") == 0) {
        ret = hardware_test_board_led();
    }
    else if (strcmp(argv[1], "tled") == 0) {
        ret = hardware_test_touch_led();
    }
    else if (strcmp(argv[1], "gpio") == 0) {
        if (argc < 3) {
            printf("用法: test gpio <pin>\n");
            return 1;
        }
        int pin = atoi(argv[2]);
        printf("开始安全GPIO输出测试 (无状态验证以避免干扰)...\n");
        ret = hardware_test_gpio(pin);
    }
    else if (strcmp(argv[1], "gpio_input") == 0) {
        if (argc < 3) {
            printf("用法: test gpio_input <pin>\n");
            return 1;
        }
        int pin = atoi(argv[2]);
        printf("开始GPIO输入模式测试...\n");
        ret = hardware_test_gpio_input(pin);
    }
    else if (strcmp(argv[1], "agx") == 0) {
        ret = hardware_test_agx_power();
    }
    else if (strcmp(argv[1], "lpmu") == 0) {
        ret = hardware_test_lpmu_power();
    }
    else if (strcmp(argv[1], "all") == 0) {
        ret = device_run_full_test();
    }
    else if (strcmp(argv[1], "quick") == 0) {
        ret = device_run_quick_test();
    }
    else if (strcmp(argv[1], "stress") == 0) {
        if (argc < 3) {
            printf("用法: test stress <ms>\n");
            return 1;
        }
        uint32_t duration = atoi(argv[2]);
        ret = device_run_stress_test(duration);
    }
    else {
        printf("未知测试项: %s\n", argv[1]);
        printf("可用测试:\n");
        printf("  fan          - 风扇测试\n");
        printf("  bled         - 板载LED测试\n");
        printf("  tled         - 触摸LED测试\n");
        printf("  gpio <pin>   - GPIO安全输出测试\n");
        printf("  gpio_input <pin> - GPIO输入测试\n");
        printf("  agx          - AGX电源控制测试\n");
        printf("  lpmu         - LPMU电源控制测试\n");
        printf("  all          - 完整测试\n");
        printf("  quick        - 快速测试\n");
        printf("  stress <ms>  - 压力测试\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("测试失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("测试完成！\n");
    return 0;
}

static int cmd_save(int argc, char **argv)
{
    esp_err_t ret = device_save_config();
    if (ret != ESP_OK) {
        printf("保存配置失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    printf("配置已保存到NVS\n");
    return 0;
}

static int cmd_load(int argc, char **argv)
{
    esp_err_t ret = device_load_config();
    if (ret != ESP_OK) {
        printf("加载配置失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    printf("配置已从NVS加载\n");
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    esp_err_t ret = device_clear_config();
    if (ret != ESP_OK) {
        printf("清除配置失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    printf("NVS配置已清除\n");
    return 0;
}

static int cmd_config(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: config <操作> [参数]\n");
        printf("操作:\n");
        printf("  show             - 显示当前完整配置\n");
        printf("  reset            - 重置为工厂默认配置\n");
        printf("  apply            - 应用当前配置到所有子系统\n");
        printf("  set fan <on> <off> [auto]  - 设置风扇默认速度\n");
        printf("  set led <bright> <br> <bg> <bb> <tr> <tg> <tb>  - 设置LED默认参数\n");
        printf("  set eth <ip> <gw> <mask> <dns>  - 设置以太网IP配置 (自动保存)\n");
        printf("  set dhcp <enable> <start> <end> <lease>  - 设置DHCP参数 (自动保存)\n");
        printf("  set gateway <enable> <nat> <firewall>  - 设置网关参数 (自动保存)\n");
        printf("\n注意: 网络配置(eth/dhcp/gateway)会自动保存到NVS，无需手动执行save命令\n");
        printf("示例:\n");
        printf("  config set fan 70 0 true     - 设置风扇开启70%%，关闭0%%，启用自动控制\n");
        printf("  config set led 80 0 0 255 0 255 0  - 设置LED亮度80%%，板载蓝色，触摸绿色\n");
        printf("  config set eth 10.10.99.98 10.10.99.1 255.255.255.0 8.8.8.8\n");
        printf("  config set dhcp true 10.10.99.101 10.10.99.110 24\n");
        printf("  config set gateway true true false\n");
        return 1;
    }

    if (strcmp(argv[1], "show") == 0) {
        // 显示当前配置
        config_manager_print_config();
        
    } else if (strcmp(argv[1], "reset") == 0) {
        printf("重置配置为工厂默认值...\n");
        esp_err_t ret = config_manager_reset_to_defaults();
        if (ret == ESP_OK) {
            printf("配置已重置为默认值\n");
        } else {
            printf("配置重置失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
    } else if (strcmp(argv[1], "apply") == 0) {
        printf("应用配置到所有子系统...\n");
        esp_err_t ret = config_manager_apply_config();
        if (ret == ESP_OK) {
            printf("配置已应用到所有子系统\n");
        } else {
            printf("配置应用失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            printf("set命令需要更多参数，请查看帮助\n");
            return 1;
        }
        
        if (strcmp(argv[2], "fan") == 0) {
            if (argc < 5) {
                printf("用法: config set fan <on_speed> <off_speed> [auto_enable]\n");
                return 1;
            }
            int speed_on = atoi(argv[3]);
            int speed_off = atoi(argv[4]);
            if (speed_on < 0 || speed_on > 100 || speed_off < 0 || speed_off > 100) {
                printf("风扇速度必须在0-100之间\n");
                return 1;
            }
            printf("设置风扇默认速度: 开启%d%%, 关闭%d%%\n", speed_on, speed_off);
            esp_err_t ret = config_manager_set_fan_speed(speed_on, speed_off);
            if (ret != ESP_OK) {
                printf("设置风扇配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
            printf("风扇配置设置成功\n");
            
        } else if (strcmp(argv[2], "led") == 0) {
            if (argc < 10) {
                printf("用法: config set led <brightness> <board_r> <board_g> <board_b> <touch_r> <touch_g> <touch_b>\n");
                return 1;
            }
            int brightness = atoi(argv[3]);
            int br = atoi(argv[4]), bg = atoi(argv[5]), bb = atoi(argv[6]);
            int tr = atoi(argv[7]), tg = atoi(argv[8]), tb = atoi(argv[9]);
            
            if (brightness < 0 || brightness > 100) {
                printf("LED亮度必须在0-100之间\n");
                return 1;
            }
            if (br < 0 || br > 255 || bg < 0 || bg > 255 || bb < 0 || bb > 255 ||
                tr < 0 || tr > 255 || tg < 0 || tg > 255 || tb < 0 || tb > 255) {
                printf("RGB颜色值必须在0-255之间\n");
                return 1;
            }
            
            printf("设置LED默认参数: 亮度%d%%, 板载RGB(%d,%d,%d), 触摸RGB(%d,%d,%d)\n",
                   brightness, br, bg, bb, tr, tg, tb);
            led_color_t board_color = {br, bg, bb};
            led_color_t touch_color = {tr, tg, tb};
            esp_err_t ret = config_manager_set_led_defaults(brightness, board_color, touch_color);
            if (ret != ESP_OK) {
                printf("设置LED配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
            printf("LED配置设置成功\n");
            
        } else if (strcmp(argv[2], "eth") == 0) {
            if (argc < 7) {
                printf("用法: config set eth <ip> <gateway> <netmask> <dns>\n");
                return 1;
            }
            printf("设置以太网配置: IP=%s, 网关=%s, 掩码=%s, DNS=%s\n",
                   argv[3], argv[4], argv[5], argv[6]);
            esp_err_t ret = config_manager_set_ethernet_ip_from_strings(argv[3], argv[4], argv[5], argv[6]);
            if (ret != ESP_OK) {
                printf("设置以太网配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
            printf("以太网配置设置成功\n");
            
            // 自动保存配置到NVS
            ret = config_manager_save();
            if (ret != ESP_OK) {
                printf("⚠️  警告: 以太网配置已更新但保存失败: %s\n", esp_err_to_name(ret));
                printf("请手动执行 'save' 命令保存配置\n");
            } else {
                printf("✅ 以太网配置已自动保存到NVS\n");
            }
            
        } else if (strcmp(argv[2], "dhcp") == 0) {
            if (argc < 7) {
                printf("用法: config set dhcp <enable> <start_ip> <end_ip> <lease_hours>\n");
                return 1;
            }
            bool enable = (strcmp(argv[3], "true") == 0);
            int lease_time = atoi(argv[6]);
            
            if (lease_time <= 0 || lease_time > 168) {
                printf("租约时间必须在1-168小时之间\n");
                return 1;
            }
            
            printf("设置DHCP参数: 启用=%s, 起始IP=%s, 结束IP=%s, 租约=%d小时\n",
                   enable ? "是" : "否", argv[4], argv[5], lease_time);
            esp_err_t ret = config_manager_set_dhcp_params(enable, argv[4], argv[5], lease_time);
            if (ret != ESP_OK) {
                printf("设置DHCP配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
            printf("DHCP配置设置成功\n");
            
            // 自动保存配置到NVS
            ret = config_manager_save();
            if (ret != ESP_OK) {
                printf("⚠️  警告: DHCP配置已更新但保存失败: %s\n", esp_err_to_name(ret));
                printf("请手动执行 'save' 命令保存配置\n");
            } else {
                printf("✅ DHCP配置已自动保存到NVS\n");
            }
            
        } else if (strcmp(argv[2], "gateway") == 0) {
            if (argc < 6) {
                printf("用法: config set gateway <enable> <nat_enable> <firewall_enable>\n");
                return 1;
            }
            bool enable = (strcmp(argv[3], "true") == 0);
            bool nat_enable = (strcmp(argv[4], "true") == 0);
            bool firewall_enable = (strcmp(argv[5], "true") == 0);
            
            printf("设置网关参数: 启用=%s, NAT=%s, 防火墙=%s\n",
                   enable ? "是" : "否", nat_enable ? "是" : "否", firewall_enable ? "是" : "否");
            esp_err_t ret = config_manager_set_gateway_params(enable, nat_enable, firewall_enable);
            if (ret != ESP_OK) {
                printf("设置网关配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
            printf("网关配置设置成功\n");
            
            // 自动保存配置到NVS
            ret = config_manager_save();
            if (ret != ESP_OK) {
                printf("⚠️  警告: 网关配置已更新但保存失败: %s\n", esp_err_to_name(ret));
                printf("请手动执行 'save' 命令保存配置\n");
            } else {
                printf("✅ 网关配置已自动保存到NVS\n");
            }
            
        } else {
            printf("未知的配置类型: %s\n", argv[2]);
            return 1;
        }
        
    } else {
        printf("未知的配置操作: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}

static int cmd_defaults(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: defaults <操作>\n");
        printf("操作:\n");
        printf("  show     - 显示所有默认参数\n");
        printf("  apply    - 应用所有默认参数\n");
        printf("  fan      - 应用风扇默认参数\n");
        printf("  led      - 应用LED默认参数\n");
        printf("  eth      - 应用以太网默认参数\n");
        printf("  dhcp     - 应用DHCP默认参数\n");
        printf("  gateway  - 应用网关默认参数\n");
        return 1;
    }

    if (strcmp(argv[1], "show") == 0) {
        printf("\n=== 系统默认参数 ===\n");
        printf("[风扇默认参数]\n");
        printf("  开启速度: 50%%\n");
        printf("  关闭速度: 0%%\n");
        printf("  自动控制: 关闭\n");
        printf("  温度阈值: 60°C\n");
        
        printf("\n[LED默认参数]\n");
        printf("  默认亮度: 50%%\n");
        printf("  板载LED颜色: 蓝色 RGB(0,0,255)\n");
        printf("  触摸LED颜色: 绿色 RGB(0,255,0)\n");
        printf("  LED效果: 关闭\n");
        printf("  彩虹速度: 100ms\n");
        
        printf("\n[以太网默认参数]\n");
        printf("  IP地址: 10.10.99.97\n");
        printf("  网关: 10.10.99.97\n");
        printf("  子网掩码: 255.255.255.0\n");
        printf("  DNS服务器: 8.8.8.8\n");
        printf("  自动启动: 是\n");
        
        printf("\n[DHCP服务器默认参数]\n");
        printf("  启用: 是\n");
        printf("  起始IP: 10.10.99.101\n");
        printf("  结束IP: 10.10.99.110\n");
        printf("  租约时间: 24小时\n");
        printf("  最大客户端: 8\n");
        printf("  自动启动: 是\n");
        
        printf("\n[网关服务默认参数]\n");
        printf("  启用: 是\n");
        printf("  NAT转发: 是\n");
        printf("  防火墙: 否\n");
        printf("  自动启动: 是\n");
        printf("=================\n\n");
        
    } else if (strcmp(argv[1], "apply") == 0) {
        printf("应用所有默认参数到当前配置...\n");
        esp_err_t ret = config_manager_reset_to_defaults();
        if (ret == ESP_OK) {
            ret = config_manager_apply_config();
            if (ret == ESP_OK) {
                printf("所有默认参数已应用\n");
            } else {
                printf("应用配置失败: %s\n", esp_err_to_name(ret));
                return 1;
            }
        } else {
            printf("重置默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
    } else if (strcmp(argv[1], "fan") == 0) {
        printf("应用风扇默认参数: 开启50%%, 关闭0%%\n");
        esp_err_t ret = config_manager_set_fan_speed(50, 0);
        if (ret != ESP_OK) {
            printf("设置风扇默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("风扇默认参数已应用\n");
        
    } else if (strcmp(argv[1], "led") == 0) {
        printf("应用LED默认参数: 亮度50%%, 板载蓝色, 触摸绿色\n");
        led_color_t blue = {0, 0, 255};
        led_color_t green = {0, 255, 0};
        esp_err_t ret = config_manager_set_led_defaults(50, blue, green);
        if (ret != ESP_OK) {
            printf("设置LED默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("LED默认参数已应用\n");
        
    } else if (strcmp(argv[1], "eth") == 0) {
        printf("应用以太网默认参数: IP=10.10.99.97\n");
        esp_err_t ret = config_manager_set_ethernet_ip_from_strings("10.10.99.97", "10.10.99.97", "255.255.255.0", "8.8.8.8");
        if (ret != ESP_OK) {
            printf("设置以太网默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("以太网默认参数已应用\n");
        
        // 自动保存配置到NVS
        ret = config_manager_save();
        if (ret != ESP_OK) {
            printf("⚠️  警告: 以太网默认参数已更新但保存失败: %s\n", esp_err_to_name(ret));
            printf("请手动执行 'save' 命令保存配置\n");
        } else {
            printf("✅ 以太网默认参数已自动保存到NVS\n");
        }
        
    } else if (strcmp(argv[1], "dhcp") == 0) {
        printf("应用DHCP默认参数: 启用, 池范围10.10.99.101-110\n");
        esp_err_t ret = config_manager_set_dhcp_params(true, "10.10.99.101", "10.10.99.110", 24);
        if (ret != ESP_OK) {
            printf("设置DHCP默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("DHCP默认参数已应用\n");
        
        // 自动保存配置到NVS
        ret = config_manager_save();
        if (ret != ESP_OK) {
            printf("⚠️  警告: DHCP默认参数已更新但保存失败: %s\n", esp_err_to_name(ret));
            printf("请手动执行 'save' 命令保存配置\n");
        } else {
            printf("✅ DHCP默认参数已自动保存到NVS\n");
        }
        
    } else if (strcmp(argv[1], "gateway") == 0) {
        printf("应用网关默认参数: 启用, NAT开启, 防火墙关闭\n");
        esp_err_t ret = config_manager_set_gateway_params(true, true, false);
        if (ret != ESP_OK) {
            printf("设置网关默认参数失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("网关默认参数已应用\n");
        
        // 自动保存配置到NVS
        ret = config_manager_save();
        if (ret != ESP_OK) {
            printf("⚠️  警告: 网关默认参数已更新但保存失败: %s\n", esp_err_to_name(ret));
            printf("请手动执行 'save' 命令保存配置\n");
        } else {
            printf("✅ 网关默认参数已自动保存到NVS\n");
        }
        
    } else {
        printf("未知的默认参数操作: %s\n", argv[1]);
        return 1;
    }
    
    return 0;
}

// 控制台任务实现
static void console_task(void *pvParameters)
{
    char input_buffer[CONSOLE_BUF_SIZE];
    int input_index = 0;
    
    // 等待系统完全初始化
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 显示启动横幅
    console_interface_show_banner();
    console_interface_print_prompt();
    
    while (s_console_state.running) {
        int c = getchar();
        
        if (c == '\n' || c == '\r') {
            // 处理输入
            input_buffer[input_index] = '\0';
            printf("\n");
            
            if (input_index > 0) {
                // 执行命令
                esp_err_t err = console_interface_execute_command(input_buffer);
                if (err == ESP_ERR_NOT_FOUND) {
                    printf("未知命令: '%s'\n", input_buffer);
                    printf("输入 'help' 查看可用命令\n");
                } else if (err == ESP_ERR_INVALID_ARG) {
                    printf("命令参数错误\n");
                } else if (err != ESP_OK) {
                    printf("命令执行错误: %s\n", esp_err_to_name(err));
                }
            }
            
            // 重置输入缓冲区
            input_index = 0;
            console_interface_print_prompt();
        } else if (c == '\b' || c == 127) {
            // 退格处理
            if (input_index > 0) {
                input_index--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (c >= 32 && c < 127 && input_index < CONSOLE_BUF_SIZE - 1) {
            // 可打印字符
            input_buffer[input_index++] = c;
            printf("%c", c);
            fflush(stdout);
        }
        
        // 短暂延迟，让其他任务有机会运行
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 任务结束
    vTaskDelete(NULL);
}
