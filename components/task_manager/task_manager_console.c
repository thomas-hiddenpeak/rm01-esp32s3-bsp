/**
 * @file task_manager_console.c
 * @brief 任务管理器控制台命令实现
 */

#include "task_manager.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "task_console";

// 命令参数结构体
static struct {
    struct arg_lit *help;
    struct arg_end *end;
} task_help_args;

static struct {
    struct arg_str *name;
    struct arg_end *end;
} task_start_args;

static struct {
    struct arg_str *name;
    struct arg_end *end;
} task_stop_args;

static struct {
    struct arg_str *name;
    struct arg_end *end;
} task_info_args;

static struct {
    struct arg_lit *save;
    struct arg_end *end;
} task_config_args;

// 命令处理函数声明
static int cmd_task_help(int argc, char **argv);
static int cmd_task_list(int argc, char **argv);
static int cmd_task_start(int argc, char **argv);
static int cmd_task_stop(int argc, char **argv);
static int cmd_task_info(int argc, char **argv);
static int cmd_task_stats(int argc, char **argv);
static int cmd_task_config(int argc, char **argv);

esp_err_t task_manager_console_init(void)
{
    ESP_LOGI(TAG, "初始化任务管理器控制台命令");

    // 注册 task help 命令
    task_help_args.help = arg_lit0("h", "help", "显示帮助信息");
    task_help_args.end = arg_end(2);

    const esp_console_cmd_t task_help_cmd = {
        .command = "task",
        .help = "任务管理器命令",
        .hint = NULL,
        .func = &cmd_task_help,
        .argtable = &task_help_args
    };
    esp_console_cmd_register(&task_help_cmd);

    // 注册 task list 命令
    const esp_console_cmd_t task_list_cmd = {
        .command = "task-list",
        .help = "列出所有任务",
        .hint = NULL,
        .func = &cmd_task_list,
        .argtable = NULL
    };
    esp_console_cmd_register(&task_list_cmd);

    // 注册 task start 命令
    task_start_args.name = arg_str1(NULL, NULL, "<name>", "要启动的任务名称");
    task_start_args.end = arg_end(2);

    const esp_console_cmd_t task_start_cmd = {
        .command = "task-start",
        .help = "启动指定任务",
        .hint = NULL,
        .func = &cmd_task_start,
        .argtable = &task_start_args
    };
    esp_console_cmd_register(&task_start_cmd);

    // 注册 task stop 命令
    task_stop_args.name = arg_str1(NULL, NULL, "<name>", "要停止的任务名称");
    task_stop_args.end = arg_end(2);

    const esp_console_cmd_t task_stop_cmd = {
        .command = "task-stop",
        .help = "停止指定任务",
        .hint = NULL,
        .func = &cmd_task_stop,
        .argtable = &task_stop_args
    };
    esp_console_cmd_register(&task_stop_cmd);

    // 注册 task info 命令
    task_info_args.name = arg_str1(NULL, NULL, "<name>", "要查看的任务名称");
    task_info_args.end = arg_end(2);

    const esp_console_cmd_t task_info_cmd = {
        .command = "task-info",
        .help = "显示任务详细信息",
        .hint = NULL,
        .func = &cmd_task_info,
        .argtable = &task_info_args
    };
    esp_console_cmd_register(&task_info_cmd);

    // 注册 task stats 命令
    const esp_console_cmd_t task_stats_cmd = {
        .command = "task-stats",
        .help = "显示任务统计信息",
        .hint = NULL,
        .func = &cmd_task_stats,
        .argtable = NULL
    };
    esp_console_cmd_register(&task_stats_cmd);

    // 注册 task config 命令
    task_config_args.save = arg_lit0("s", "save", "保存当前配置到NVS");
    task_config_args.end = arg_end(2);

    const esp_console_cmd_t task_config_cmd = {
        .command = "task-config",
        .help = "任务管理器配置操作",
        .hint = NULL,
        .func = &cmd_task_config,
        .argtable = &task_config_args
    };
    esp_console_cmd_register(&task_config_cmd);

    ESP_LOGI(TAG, "任务管理器控制台命令初始化完成");
    return ESP_OK;
}

// =============================================================================
// 命令处理函数实现
// =============================================================================

static int cmd_task_help(int argc, char **argv)
{
    printf("\n=== 任务管理器命令帮助 ===\n\n");
    printf("可用命令:\n");
    printf("  task                  - 显示此帮助信息\n");
    printf("  task-list             - 列出所有注册的任务\n");
    printf("  task-start <name>     - 启动指定任务\n");
    printf("  task-stop <name>      - 停止指定任务\n");
    printf("  task-info <name>      - 显示任务详细信息\n");
    printf("  task-stats            - 显示任务统计信息\n");
    printf("  task-config [-s]      - 显示/保存任务管理器配置\n\n");
    
    printf("预定义任务ID:\n");
    printf("  %s  - 配置管理器\n", TASK_ID_CONFIG_MANAGER);
    printf("  %s    - 设备接口\n", TASK_ID_DEVICE_INTERFACE);
    printf("  %s - 以太网接口\n", TASK_ID_ETHERNET_INTERFACE);
    printf("  %s   - 硬件控制\n", TASK_ID_HARDWARE_CONTROL);
    printf("  %s   - 控制台接口\n", TASK_ID_CONSOLE_INTERFACE);
    printf("  %s      - 系统监控\n\n", TASK_ID_SYSTEM_MONITOR);
    
    return 0;
}

static int cmd_task_list(int argc, char **argv)
{
    task_info_t tasks[MAX_MANAGED_TASKS];
    uint32_t count;
    
    esp_err_t ret = task_manager_list_tasks(tasks, MAX_MANAGED_TASKS, &count);
    if (ret != ESP_OK) {
        printf("获取任务列表失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("\n=== 任务列表 ===\n\n");
    printf("%-20s %-8s %-8s %-8s %-12s\n", "任务名称", "类型", "优先级", "自启", "状态");
    printf("-------------------- -------- -------- -------- ------------\n");

    for (uint32_t i = 0; i < count; i++) {
        const task_info_t *task = &tasks[i];
        
        const char *type_str;
        switch (task->config.type) {
            case TASK_TYPE_SERVICE: type_str = "服务"; break;
            case TASK_TYPE_BACKGROUND: type_str = "后台"; break;
            case TASK_TYPE_INTERFACE: type_str = "接口"; break;
            case TASK_TYPE_MONITOR: type_str = "监控"; break;
            default: type_str = "未知"; break;
        }

        const char *priority_str;
        switch (task->config.priority) {
            case TASK_PRIORITY_LOW: priority_str = "低"; break;
            case TASK_PRIORITY_NORMAL: priority_str = "普通"; break;
            case TASK_PRIORITY_HIGH: priority_str = "高"; break;
            case TASK_PRIORITY_CRITICAL: priority_str = "关键"; break;
            default: priority_str = "未知"; break;
        }

        const char *status_str;
        switch (task->status) {
            case TASK_STATUS_UNINITIALIZED: status_str = "未初始化"; break;
            case TASK_STATUS_INITIALIZED: status_str = "已初始化"; break;
            case TASK_STATUS_STARTING: status_str = "启动中"; break;
            case TASK_STATUS_RUNNING: status_str = "运行中"; break;
            case TASK_STATUS_STOPPING: status_str = "停止中"; break;
            case TASK_STATUS_STOPPED: status_str = "已停止"; break;
            case TASK_STATUS_ERROR: status_str = "错误"; break;
            default: status_str = "未知"; break;
        }

        printf("%-20s %-8s %-8s %-8s %-12s\n", 
               task->config.name,
               type_str,
               priority_str,
               task->config.auto_start ? "是" : "否",
               status_str);
    }

    printf("\n总计: %lu 个任务\n\n", count);
    return 0;
}

static int cmd_task_start(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&task_start_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, task_start_args.end, argv[0]);
        return 1;
    }

    const char *task_name = task_start_args.name->sval[0];
    printf("启动任务: %s\n", task_name);

    esp_err_t ret = task_manager_start_task(task_name);
    if (ret == ESP_OK) {
        printf("任务 '%s' 启动成功\n", task_name);
    } else {
        printf("任务 '%s' 启动失败: %s\n", task_name, esp_err_to_name(ret));
        return 1;
    }

    return 0;
}

static int cmd_task_stop(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&task_stop_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, task_stop_args.end, argv[0]);
        return 1;
    }

    const char *task_name = task_stop_args.name->sval[0];
    printf("停止任务: %s\n", task_name);

    esp_err_t ret = task_manager_stop_task(task_name);
    if (ret == ESP_OK) {
        printf("任务 '%s' 停止成功\n", task_name);
    } else {
        printf("任务 '%s' 停止失败: %s\n", task_name, esp_err_to_name(ret));
        return 1;
    }

    return 0;
}

static int cmd_task_info(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&task_info_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, task_info_args.end, argv[0]);
        return 1;
    }

    const char *task_name = task_info_args.name->sval[0];
    
    task_info_t task_info;
    esp_err_t ret = task_manager_get_task_info(task_name, &task_info);
    
    if (ret != ESP_OK) {
        printf("任务 '%s' 不存在或获取信息失败: %s\n", task_name, esp_err_to_name(ret));
        return 1;
    }

    printf("\n=== 任务信息: %s ===\n\n", task_name);
    printf("基本信息:\n");
    printf("  名称: %s\n", task_info.config.name);
    printf("  类型: ");
    switch (task_info.config.type) {
        case TASK_TYPE_SERVICE: printf("服务任务\n"); break;
        case TASK_TYPE_BACKGROUND: printf("后台任务\n"); break;
        case TASK_TYPE_INTERFACE: printf("接口任务\n"); break;
        case TASK_TYPE_MONITOR: printf("监控任务\n"); break;
        default: printf("未知类型\n"); break;
    }
    
    printf("  优先级: ");
    switch (task_info.config.priority) {
        case TASK_PRIORITY_LOW: printf("低\n"); break;
        case TASK_PRIORITY_NORMAL: printf("普通\n"); break;
        case TASK_PRIORITY_HIGH: printf("高\n"); break;
        case TASK_PRIORITY_CRITICAL: printf("关键\n"); break;
        default: printf("未知\n"); break;
    }
    
    printf("  自动启动: %s\n", task_info.config.auto_start ? "是" : "否");
    printf("  阻塞任务: %s\n", task_info.config.blocking ? "是" : "否");
    printf("  栈大小: %lu 字节\n", task_info.config.stack_size);
    printf("  FreeRTOS优先级: %d\n", task_info.config.freertos_priority);
    
    if (strlen(task_info.config.dependencies) > 0) {
        printf("  依赖任务: %s\n", task_info.config.dependencies);
    } else {
        printf("  依赖任务: 无\n");
    }

    printf("\n运行状态:\n");
    printf("  当前状态: ");
    switch (task_info.status) {
        case TASK_STATUS_UNINITIALIZED: printf("未初始化\n"); break;
        case TASK_STATUS_INITIALIZED: printf("已初始化\n"); break;
        case TASK_STATUS_STARTING: printf("启动中\n"); break;
        case TASK_STATUS_RUNNING: printf("运行中\n"); break;
        case TASK_STATUS_STOPPING: printf("停止中\n"); break;
        case TASK_STATUS_STOPPED: printf("已停止\n"); break;
        case TASK_STATUS_ERROR: printf("错误\n"); break;
        default: printf("未知状态\n"); break;
    }
    
    if (task_info.start_time > 0) {
        printf("  启动时间: %lu ms\n", task_info.start_time);
        printf("  运行时长: %lu ms\n", esp_log_timestamp() - task_info.start_time);
    }
    
    if (task_info.last_error != ESP_OK) {
        printf("  最后错误: %s\n", esp_err_to_name(task_info.last_error));
    }
    
    if (strlen(task_info.error_message) > 0) {
        printf("  错误信息: %s\n", task_info.error_message);
    }

    printf("\n");
    return 0;
}

static int cmd_task_stats(int argc, char **argv)
{
    task_manager_stats_t stats;
    esp_err_t ret = task_manager_get_stats(&stats);
    
    if (ret != ESP_OK) {
        printf("获取任务统计信息失败: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("\n=== 任务统计信息 ===\n\n");
    printf("总任务数: %lu\n", stats.total_tasks);
    printf("运行中: %lu\n", stats.running_tasks);
    printf("失败任务: %lu\n", stats.failed_tasks);
    printf("停止任务: %lu\n", stats.total_tasks - stats.running_tasks - stats.failed_tasks);
    printf("系统运行时间: %lu ms\n", stats.startup_time_ms);
    
    if (stats.total_tasks > 0) {
        printf("运行率: %.1f%%\n", (float)stats.running_tasks / stats.total_tasks * 100.0f);
    }
    
    printf("\n");
    return 0;
}

static int cmd_task_config(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&task_config_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, task_config_args.end, argv[0]);
        return 1;
    }

    if (task_config_args.save->count > 0) {
        // 保存配置
        esp_err_t ret = task_manager_save_config();
        if (ret == ESP_OK) {
            printf("任务管理器配置已保存到NVS\n");
        } else {
            printf("保存配置失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
    } else {
        // 显示当前配置
        task_manager_config_t config;
        esp_err_t ret = task_manager_get_config(&config);
        
        if (ret != ESP_OK) {
            printf("获取配置失败: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
        printf("\n=== 任务管理器配置 ===\n\n");
        printf("自动启动: %s\n", config.auto_start_enabled ? "启用" : "禁用");
        printf("启动超时: %lu ms\n", config.startup_timeout_ms);
        printf("监控间隔: %lu ms\n", config.monitor_interval_ms);
        printf("依赖检查: %s\n", config.dependency_check_enabled ? "启用" : "禁用");
        printf("配置任务数: %d\n", config.task_count);
        
        if (config.task_count > 0) {
            printf("\n配置的任务:\n");
            for (int i = 0; i < config.task_count; i++) {
                const task_config_t *task = &config.tasks[i];
                printf("  %d. %s (优先级: %d, 自启: %s)\n", 
                       i + 1, 
                       task->name,
                       task->priority,
                       task->auto_start ? "是" : "否");
            }
        }
        printf("\n");
    }

    return 0;
}
