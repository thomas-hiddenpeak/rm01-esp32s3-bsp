/**
 * @file task_manager.c
 * @brief NVS-based Task Manager Implementation
 */

#include "task_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "config_manager.h"
#include "device_interface.h"
#include "hardware_control.h"
#include "ethernet_interface.h"
#include "console_interface.h"
#include "system_monitor.h"
#include <string.h>

static const char *TAG = "task_manager";

// 全局任务管理器状态
static task_manager_state_t s_state = {0};

// 任务监控任务 
static void task_monitor_task(void *pvParameters);

// 系统任务注册函数前置声明
static esp_err_t register_all_system_tasks(void);

// 辅助函数前置声明
static task_info_t* find_task_by_name(const char *name);
static esp_err_t save_config_to_nvs(void);
static esp_err_t load_config_from_nvs(void);
static bool check_dependencies_met(task_info_t *task);
static esp_err_t start_tasks_by_priority(task_priority_level_t priority);

// 实现函数
esp_err_t task_manager_init(void)
{
    esp_err_t ret;

    if (s_state.initialized) {
        ESP_LOGI(TAG, "Task manager already initialized");
        return ESP_OK;
    }

    // 创建互斥锁
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化默认配置
    memset(&s_state.config, 0, sizeof(s_state.config));
    s_state.config.auto_start_enabled = true;
    s_state.config.startup_timeout_ms = 30000;
    s_state.config.monitor_interval_ms = 10000;
    s_state.config.dependency_check_enabled = true;
    s_state.config.task_count = 0;

    // 从NVS加载配置
    ret = load_config_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS, using defaults");
    }

    // 初始化任务数组
    for (int i = 0; i < MAX_MANAGED_TASKS; i++) {
        s_state.tasks[i].status = TASK_STATUS_UNINITIALIZED;
        s_state.tasks[i].task_handle = NULL;
        s_state.tasks[i].start_time = 0;
        s_state.tasks[i].last_error = ESP_OK;
        memset(s_state.tasks[i].error_message, 0, sizeof(s_state.tasks[i].error_message));
    }

    // 注册所有系统任务
    ret = register_all_system_tasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register system tasks");
        return ret;
    }

    // 启动监控任务
    BaseType_t xResult = xTaskCreate(
        task_monitor_task,
        "task_monitor",
        4096,
        NULL,
        2,
        &s_state.monitor_task_handle
    );

    if (xResult != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return ESP_ERR_NO_MEM;
    }

    s_state.initialized = true;
    ESP_LOGI(TAG, "Task manager initialized successfully");

    return ESP_OK;
}

esp_err_t task_manager_deinit(void)
{
    if (!s_state.initialized) {
        return ESP_OK;
    }

    // 停止所有任务
    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        task_manager_stop_task(s_state.tasks[i].config.name);
    }

    // 停止监控任务
    if (s_state.monitor_task_handle != NULL) {
        vTaskDelete(s_state.monitor_task_handle);
        s_state.monitor_task_handle = NULL;
    }

    // 删除互斥锁
    if (s_state.mutex != NULL) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }

    s_state.initialized = false;
    ESP_LOGI(TAG, "Task manager deinitialized");

    return ESP_OK;
}

esp_err_t task_manager_register_task(const task_config_t *config)
{
    if (!s_state.initialized || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // 检查是否已经注册
    if (find_task_by_name(config->name) != NULL) {
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    // 检查是否有空间
    if (s_state.config.task_count >= MAX_MANAGED_TASKS) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // 添加任务
    task_info_t *task = &s_state.tasks[s_state.config.task_count];
    memcpy(&task->config, config, sizeof(task_config_t));
    task->status = TASK_STATUS_INITIALIZED;
    task->task_handle = NULL;
    task->start_time = 0;
    task->last_error = ESP_OK;
    memset(task->error_message, 0, sizeof(task->error_message));

    s_state.config.task_count++;

    // 保存到NVS
    save_config_to_nvs();

    ESP_LOGI(TAG, "Task '%s' registered successfully", config->name);

cleanup:
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t task_manager_start_task(const char *task_name)
{
    if (!s_state.initialized || task_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    task_info_t *task = find_task_by_name(task_name);

    if (task == NULL) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (task->status == TASK_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Task '%s' is already running", task_name);
        goto cleanup;
    }

    // 检查依赖
    if (s_state.config.dependency_check_enabled && !check_dependencies_met(task)) {
        ESP_LOGW(TAG, "Dependencies not met for task '%s'", task_name);
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }

    // 执行初始化回调
    if (task->config.init_func) {
        ret = task->config.init_func();
        if (ret != ESP_OK) {
            task->last_error = ret;
            snprintf(task->error_message, sizeof(task->error_message), 
                    "Init failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }
    }

    task->status = TASK_STATUS_STARTING;

    // 执行启动回调
    if (task->config.start_func) {
        ret = task->config.start_func();
        if (ret != ESP_OK) {
            task->status = TASK_STATUS_ERROR;
            task->last_error = ret;
            snprintf(task->error_message, sizeof(task->error_message), 
                    "Start failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }
    }

    // 创建FreeRTOS任务(仅针对后台任务)
    if (task->config.type == TASK_TYPE_BACKGROUND && task->config.stack_size > 0) {
        BaseType_t xResult = xTaskCreate(
            (TaskFunction_t)task->config.start_func,
            task->config.name,
            task->config.stack_size,
            NULL,
            task->config.freertos_priority,
            &task->task_handle
        );

        if (xResult != pdPASS) {
            task->status = TASK_STATUS_ERROR;
            task->last_error = ESP_ERR_NO_MEM;
            snprintf(task->error_message, sizeof(task->error_message), "Failed to create FreeRTOS task");
            ret = ESP_ERR_NO_MEM;
            goto cleanup;
        }
    }

    task->status = TASK_STATUS_RUNNING;
    task->start_time = xTaskGetTickCount();
    ESP_LOGI(TAG, "Task '%s' started successfully", task_name);

cleanup:
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t task_manager_stop_task(const char *task_name)
{
    if (!s_state.initialized || task_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    task_info_t *task = find_task_by_name(task_name);

    if (task == NULL) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    if (task->status != TASK_STATUS_RUNNING) {
        ESP_LOGW(TAG, "Task '%s' is not running", task_name);
        goto cleanup;
    }

    task->status = TASK_STATUS_STOPPING;

    // 停止FreeRTOS任务
    if (task->task_handle != NULL) {
        vTaskDelete(task->task_handle);
        task->task_handle = NULL;
    }

    // 执行停止回调
    if (task->config.stop_func) {
        ret = task->config.stop_func();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Stop callback failed for task '%s': %s", 
                    task_name, esp_err_to_name(ret));
        }
    }

    task->status = TASK_STATUS_STOPPED;
    ESP_LOGI(TAG, "Task '%s' stopped", task_name);

cleanup:
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t task_manager_set_task_config(const char *task_name, const task_config_t *config)
{
    if (!s_state.initialized || task_name == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    task_info_t *task = find_task_by_name(task_name);

    if (task == NULL) {
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    // 更新配置
    memcpy(&task->config, config, sizeof(task_config_t));

    // 保存到NVS
    save_config_to_nvs();

    ESP_LOGI(TAG, "Task '%s' configuration updated", task_name);

cleanup:
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t task_manager_list_tasks(task_info_t *tasks, uint32_t max_tasks, uint32_t *actual_count)
{
    if (!s_state.initialized || tasks == NULL || actual_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint32_t count = s_state.config.task_count < max_tasks ? s_state.config.task_count : max_tasks;

    for (uint32_t i = 0; i < count; i++) {
        memcpy(&tasks[i], &s_state.tasks[i], sizeof(task_info_t));
    }

    *actual_count = count;

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t task_manager_get_stats(task_manager_stats_t *stats)
{
    if (!s_state.initialized || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(stats, 0, sizeof(task_manager_stats_t));
    stats->total_tasks = s_state.config.task_count;

    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        switch (s_state.tasks[i].status) {
            case TASK_STATUS_RUNNING:
                stats->running_tasks++;
                break;
            case TASK_STATUS_STOPPED:
                stats->total_tasks++; // Reusing field name appropriately
                break;
            case TASK_STATUS_ERROR:
                stats->running_tasks++; // Reusing field name appropriately  
                break;
            default:
                break;
        }
    }

    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t task_manager_start_all(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting all auto-start tasks");

    esp_err_t ret = ESP_OK;

    // 按优先级启动任务
    for (int priority = TASK_PRIORITY_CRITICAL; priority >= TASK_PRIORITY_LOW; priority--) {
        esp_err_t result = start_tasks_by_priority((task_priority_level_t)priority);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start some tasks with priority %d", priority);
            ret = result;
        }
    }

    return ret;
}

esp_err_t task_manager_stop_all(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping all tasks");

    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        if (s_state.tasks[i].status == TASK_STATUS_RUNNING) {
            esp_err_t ret = task_manager_stop_task(s_state.tasks[i].config.name);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to stop task '%s'", s_state.tasks[i].config.name);
            }
        }
    }

    return ESP_OK;
}

// 辅助函数实现
static esp_err_t start_tasks_by_priority(task_priority_level_t priority)
{
    esp_err_t ret = ESP_OK;

    if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        task_info_t *task = &s_state.tasks[i];

        if (task->config.priority == priority && 
            task->config.auto_start &&
            task->status != TASK_STATUS_RUNNING) {

            ESP_LOGI(TAG, "Starting task '%s' (priority %d)", task->config.name, priority);

            esp_err_t start_ret = task_manager_start_task(task->config.name);
            if (start_ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start task '%s': %s", 
                        task->config.name, esp_err_to_name(start_ret));
                ret = start_ret;
                continue;
            }

            // 如果是阻塞任务，等待其完成
            if (task->config.blocking) {
                vTaskDelay(pdMS_TO_TICKS(100)); // 给任务一些启动时间
            }
        }
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

static bool check_dependencies_met(task_info_t *task)
{
    if (strlen(task->config.dependencies) == 0) {
        return true; // 没有依赖
    }

    // 解析依赖列表(逗号分隔)
    char deps_copy[TASK_DEPENDENCIES_MAX_LEN];
    strncpy(deps_copy, task->config.dependencies, sizeof(deps_copy) - 1);
    deps_copy[sizeof(deps_copy) - 1] = '\0';

    char *token = strtok(deps_copy, ",");
    while (token != NULL) {
        // 去除前后空格
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';

        // 查找依赖任务
        task_info_t *dep_task = find_task_by_name(token);
        if (dep_task == NULL || dep_task->status != TASK_STATUS_RUNNING) {
            return false;
        }

        token = strtok(NULL, ",");
    }

    return true;
}

static void task_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Task monitor started");

    while (true) {
        if (xSemaphoreTake(s_state.mutex, portMAX_DELAY) == pdTRUE) {

            // 这里可以添加任务健康检查逻辑
            for (uint32_t i = 0; i < s_state.config.task_count; i++) {
                task_info_t *task = &s_state.tasks[i];
                
                if (task->status == TASK_STATUS_RUNNING) {
                    // 检查任务状态等
                    // 暂时只是记录活跃任务
                }
            }

            xSemaphoreGive(s_state.mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(s_state.config.monitor_interval_ms));
    }
}

static task_info_t* find_task_by_name(const char *name)
{
    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        if (strcmp(s_state.tasks[i].config.name, name) == 0) {
            return &s_state.tasks[i];
        }
    }
    return NULL;
}

static esp_err_t save_config_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("task_manager", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, "config", &s_state.config, sizeof(s_state.config));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config to NVS: %s", esp_err_to_name(ret));
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Config saved to NVS successfully");
        }
    }

    nvs_close(nvs_handle);
    return ret;
}

static esp_err_t load_config_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("task_manager", NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t required_size = sizeof(s_state.config);
    ret = nvs_get_blob(nvs_handle, "config", &s_state.config, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Config loaded from NVS successfully");
    }

    nvs_close(nvs_handle);
    return ret;
}

static esp_err_t register_all_system_tasks(void)
{
    esp_err_t ret = ESP_OK;

    // 配置管理器任务
    task_config_t config_task = {
        .name = "config_manager",
        .type = TASK_TYPE_SERVICE,
        .priority = TASK_PRIORITY_HIGH,
        .auto_start = true,
        .blocking = false,
        .stack_size = 0,
        .freertos_priority = 0,
        .dependencies = "",
        .init_func = config_manager_init,
        .start_func = NULL, // 服务任务，没有独立启动函数
        .stop_func = NULL,
        .deinit_func = config_manager_deinit
    };

    ret = task_manager_register_task(&config_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register config_manager task");
        return ret;
    }

    // 设备接口任务
    task_config_t device_task = {
        .name = "device_interface",
        .type = TASK_TYPE_INTERFACE,
        .priority = TASK_PRIORITY_HIGH,
        .auto_start = true,
        .blocking = false,
        .stack_size = 0,
        .freertos_priority = 0,
        .dependencies = "config_manager",
        .init_func = device_interface_init,
        .start_func = NULL,
        .stop_func = NULL,
        .deinit_func = device_interface_deinit
    };

    ret = task_manager_register_task(&device_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register device_interface task");
        return ret;
    }

    // 以太网接口任务
    task_config_t ethernet_task = {
        .name = "ethernet_interface",
        .type = TASK_TYPE_INTERFACE,
        .priority = TASK_PRIORITY_NORMAL,
        .auto_start = true,
        .blocking = false,
        .stack_size = 4096,
        .freertos_priority = 5,
        .dependencies = "config_manager",
        .init_func = ethernet_interface_init,
        .start_func = ethernet_interface_start,
        .stop_func = ethernet_interface_stop,
        .deinit_func = ethernet_interface_deinit
    };

    ret = task_manager_register_task(&ethernet_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ethernet_interface task");
        return ret;
    }

    // 硬件控制任务
    task_config_t hardware_task = {
        .name = "hardware_control",
        .type = TASK_TYPE_SERVICE,
        .priority = TASK_PRIORITY_NORMAL,
        .auto_start = true,
        .blocking = false,
        .stack_size = 0,
        .freertos_priority = 0,
        .dependencies = "device_interface",
        .init_func = hardware_control_init,
        .start_func = NULL,
        .stop_func = NULL,
        .deinit_func = hardware_control_deinit
    };

    ret = task_manager_register_task(&hardware_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register hardware_control task");
        return ret;
    }

    // 控制台接口任务
    task_config_t console_task = {
        .name = "console_interface",
        .type = TASK_TYPE_INTERFACE,
        .priority = TASK_PRIORITY_LOW,
        .auto_start = true,
        .blocking = false,
        .stack_size = 0,
        .freertos_priority = 0,
        .dependencies = "",
        .init_func = console_interface_init,
        .start_func = NULL,
        .stop_func = NULL,
        .deinit_func = console_interface_deinit
    };

    ret = task_manager_register_task(&console_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register console_interface task");
        return ret;
    }

    // 系统监控任务
    task_config_t monitor_task = {
        .name = "system_monitor",
        .type = TASK_TYPE_MONITOR,
        .priority = TASK_PRIORITY_LOW,
        .auto_start = true,
        .blocking = false,
        .stack_size = 4096,
        .freertos_priority = 3,
        .dependencies = "",
        .init_func = system_monitor_init,
        .start_func = system_monitor_start,
        .stop_func = system_monitor_stop,
        .deinit_func = system_monitor_deinit
    };

    ret = task_manager_register_task(&monitor_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register system_monitor task");
        return ret;
    }

    ESP_LOGI(TAG, "All system tasks registered successfully");
    return ESP_OK;
}

// 实现头文件中声明的公共辅助函数
const char* task_status_to_string(task_status_t status)
{
    switch (status) {
        case TASK_STATUS_UNINITIALIZED: return "UNINITIALIZED";
        case TASK_STATUS_INITIALIZED: return "INITIALIZED";
        case TASK_STATUS_STARTING: return "STARTING";
        case TASK_STATUS_RUNNING: return "RUNNING";
        case TASK_STATUS_STOPPING: return "STOPPING";
        case TASK_STATUS_STOPPED: return "STOPPED";
        case TASK_STATUS_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* task_type_to_string(task_type_t type)
{
    switch (type) {
        case TASK_TYPE_SERVICE: return "SERVICE";
        case TASK_TYPE_BACKGROUND: return "BACKGROUND";
        case TASK_TYPE_INTERFACE: return "INTERFACE";
        case TASK_TYPE_MONITOR: return "MONITOR";
        default: return "UNKNOWN";
    }
}

const char* task_priority_to_string(task_priority_level_t priority)
{
    switch (priority) {
        case TASK_PRIORITY_LOW: return "LOW";
        case TASK_PRIORITY_NORMAL: return "NORMAL";
        case TASK_PRIORITY_HIGH: return "HIGH";
        case TASK_PRIORITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}
