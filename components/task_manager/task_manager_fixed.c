/**
 * @file task_manager.c
 * @brief 任务管理器实现 - 基于NVS的任务调度系统
 */

#include "task_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "task_manager";

// NVS命名空间
#define NVS_NAMESPACE_TASK_MANAGER  "task_mgr"
#define NVS_KEY_CONFIG              "config"

static task_manager_state_t s_state = {0};

// 内部函数声明
static esp_err_t resolve_dependencies(void);
static esp_err_t start_tasks_by_priority(void);
static bool check_dependencies_met(const task_info_t *task);
static task_info_t* find_task_by_name(const char *name);
static void task_monitor_task(void *pvParameters);
static esp_err_t register_all_system_tasks(void);
static const char* task_status_to_string(task_status_t status);
static const char* task_type_to_string(task_type_t type);
static const char* task_priority_to_string(task_priority_t priority);

// 预定义任务注册函数(需要各组件提供)
extern esp_err_t config_manager_task_init(void);
extern esp_err_t config_manager_task_start(void);
extern esp_err_t device_interface_task_init(void);
extern esp_err_t device_interface_task_start(void);
extern esp_err_t ethernet_interface_task_init(void);
extern esp_err_t ethernet_interface_task_start(void);
extern esp_err_t hardware_control_task_init(void);
extern esp_err_t hardware_control_task_start(void);
extern esp_err_t console_interface_task_init(void);
extern esp_err_t console_interface_task_start(void);
extern esp_err_t system_monitor_task_init(void);
extern esp_err_t system_monitor_task_start(void);

// =============================================================================
// 核心API实现
// =============================================================================

esp_err_t task_manager_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    // 创建互斥锁
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 从NVS加载配置
    ret = task_manager_load_config();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config from NVS, using defaults");
        // 设置默认配置
        s_state.config.auto_start_enabled = true;
        s_state.config.max_startup_time_ms = 30000;
        s_state.config.monitor_interval_ms = 5000;
        s_state.config.dependency_check_enabled = true;
        s_state.config.task_count = 0;
    }

    // 注册系统任务
    register_all_system_tasks();

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
        task_manager_stop_task(s_state.tasks[i].name);
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

esp_err_t task_manager_start_all(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting all enabled tasks...");

    // 解析依赖关系
    esp_err_t ret = resolve_dependencies();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resolve dependencies");
        return ret;
    }

    // 按优先级启动任务
    ret = start_tasks_by_priority();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start tasks by priority");
        return ret;
    }

    // 启动监控任务
    if (s_state.config.monitor_interval_ms > 0) {
        xTaskCreate(task_monitor_task, "task_monitor", 2048, NULL, 1, &s_state.monitor_task_handle);
    }

    ESP_LOGI(TAG, "All tasks started successfully");
    return ESP_OK;
}

esp_err_t task_manager_register_task(const task_config_t *task_config)
{
    if (!s_state.initialized || task_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    // 检查是否已经存在同名任务
    if (find_task_by_name(task_config->name) != NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_ARG; // 使用 ESP_ERR_INVALID_ARG 代替 ESP_ERR_DUPLICATE_ENTRY
    }

    if (s_state.config.task_count >= MAX_MANAGED_TASKS) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    // 添加任务信息
    task_info_t *task = &s_state.tasks[s_state.config.task_count];
    memcpy(&task->config, task_config, sizeof(task_config_t));
    task->status = TASK_STATUS_STOPPED;
    task->handle = NULL;
    task->last_heartbeat = 0;

    s_state.config.task_count++;

    xSemaphoreGive(s_state.mutex);

    ESP_LOGI(TAG, "Task '%s' registered successfully", task_config->name);
    return ESP_OK;
}

esp_err_t task_manager_start_task(const char *task_name)
{
    if (!s_state.initialized || task_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    task_info_t *task = find_task_by_name(task_name);
    if (task == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (task->status == TASK_STATUS_RUNNING) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK; // 已经在运行
    }

    // 检查依赖关系
    if (s_state.config.dependency_check_enabled && !check_dependencies_met(task)) {
        xSemaphoreGive(s_state.mutex);
        ESP_LOGW(TAG, "Task '%s' dependencies not met", task_name);
        return ESP_ERR_INVALID_STATE;
    }

    // 启动任务
    esp_err_t ret = ESP_OK;
    if (task->config.init_func != NULL) {
        ret = task->config.init_func();
        if (ret != ESP_OK) {
            xSemaphoreGive(s_state.mutex);
            ESP_LOGE(TAG, "Task '%s' init failed: %s", task_name, esp_err_to_name(ret));
            return ret;
        }
    }

    if (task->config.start_func != NULL) {
        ret = task->config.start_func();
        if (ret != ESP_OK) {
            xSemaphoreGive(s_state.mutex);
            ESP_LOGE(TAG, "Task '%s' start failed: %s", task_name, esp_err_to_name(ret));
            return ret;
        }
    }

    task->status = TASK_STATUS_RUNNING;
    task->last_heartbeat = xTaskGetTickCount();

    xSemaphoreGive(s_state.mutex);

    ESP_LOGI(TAG, "Task '%s' started successfully", task_name);
    return ESP_OK;
}

esp_err_t task_manager_stop_task(const char *task_name)
{
    if (!s_state.initialized || task_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    task_info_t *task = find_task_by_name(task_name);
    if (task == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (task->status == TASK_STATUS_STOPPED) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK; // 已经停止
    }

    // 停止任务
    if (task->config.stop_func != NULL) {
        esp_err_t ret = task->config.stop_func();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Task '%s' stop function failed: %s", task_name, esp_err_to_name(ret));
        }
    }

    if (task->handle != NULL) {
        vTaskDelete(task->handle);
        task->handle = NULL;
    }

    task->status = TASK_STATUS_STOPPED;

    xSemaphoreGive(s_state.mutex);

    ESP_LOGI(TAG, "Task '%s' stopped successfully", task_name);
    return ESP_OK;
}

esp_err_t task_manager_restart_task(const char *task_name)
{
    esp_err_t ret = task_manager_stop_task(task_name);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // 等待100ms

    return task_manager_start_task(task_name);
}

esp_err_t task_manager_set_task_enabled(const char *task_name, bool enabled)
{
    if (!s_state.initialized || task_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    // 查找配置中的任务
    task_config_t *config = NULL;
    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        if (strcmp(s_state.config.tasks[i].name, task_name) == 0) {
            config = &s_state.config.tasks[i];
            break;
        }
    }

    if (config == NULL) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    config->enabled = enabled;

    xSemaphoreGive(s_state.mutex);

    ESP_LOGI(TAG, "Task '%s' %s", task_name, enabled ? "enabled" : "disabled");
    return ESP_OK;
}

uint32_t task_manager_list_tasks(task_info_t *tasks, uint32_t max_tasks)
{
    if (!s_state.initialized || tasks == NULL || max_tasks == 0) {
        return 0;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    uint32_t count = s_state.config.task_count < max_tasks ? s_state.config.task_count : max_tasks;
    memcpy(tasks, s_state.tasks, count * sizeof(task_info_t));

    xSemaphoreGive(s_state.mutex);

    return count;
}

esp_err_t task_manager_get_stats(task_manager_stats_t *stats)
{
    if (!s_state.initialized || stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_state.mutex, portMAX_DELAY);

    memset(stats, 0, sizeof(task_manager_stats_t));
    stats->total_tasks = s_state.config.task_count;

    for (uint32_t i = 0; i < s_state.config.task_count; i++) {
        switch (s_state.tasks[i].status) {
            case TASK_STATUS_RUNNING:
                stats->running_tasks++;
                break;
            case TASK_STATUS_STOPPED:
                stats->stopped_tasks++;
                break;
            case TASK_STATUS_ERROR:
                stats->error_tasks++;
                break;
        }
    }

    xSemaphoreGive(s_state.mutex);

    return ESP_OK;
}

esp_err_t task_manager_load_config(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_TASK_MANAGER, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    size_t required_size = sizeof(task_manager_config_t);
    ret = nvs_get_blob(nvs_handle, NVS_KEY_CONFIG, &s_state.config, &required_size);
    
    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Configuration loaded from NVS");
    }

    return ret;
}

esp_err_t task_manager_save_config(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_TASK_MANAGER, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(nvs_handle, NVS_KEY_CONFIG, &s_state.config, sizeof(task_manager_config_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(ret));
    }

    return ret;
}

// =============================================================================
// 内部函数实现
// =============================================================================

static esp_err_t resolve_dependencies(void)
{
    // 简单的依赖解析 - 实际实现需要更复杂的拓扑排序
    ESP_LOGI(TAG, "Resolving task dependencies...");
    
    // TODO: 实现真正的依赖解析逻辑
    return ESP_OK;
}

static esp_err_t start_tasks_by_priority(void)
{
    ESP_LOGI(TAG, "Starting tasks by priority...");

    // 按优先级排序并启动任务
    for (int priority = TASK_PRIORITY_CRITICAL; priority >= TASK_PRIORITY_LOW; priority--) {
        for (uint32_t i = 0; i < s_state.config.task_count; i++) {
            task_info_t *task = &s_state.tasks[i];
            
            // 检查配置中对应的任务是否启用
            task_config_t *config = NULL;
            for (uint32_t j = 0; j < s_state.config.task_count; j++) {
                if (strcmp(s_state.config.tasks[j].name, task->config.name) == 0) {
                    config = &s_state.config.tasks[j];
                    break;
                }
            }
            
            if (config && config->enabled && 
                task->config.priority == priority && 
                task->status == TASK_STATUS_STOPPED) {
                
                esp_err_t ret = task_manager_start_task(task->config.name);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start task '%s': %s", 
                            task->config.name, esp_err_to_name(ret));
                    // 继续启动其他任务
                }
                
                // 如果是阻塞类型，等待启动完成
                if (task->config.type == TASK_TYPE_BLOCKING) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }
    }

    return ESP_OK;
}

static bool check_dependencies_met(const task_info_t *task)
{
    if (task->config.dependency_count == 0) {
        return true;
    }

    for (uint32_t i = 0; i < task->config.dependency_count; i++) {
        task_info_t *dep_task = find_task_by_name(task->config.dependencies[i]);
        if (dep_task == NULL || dep_task->status != TASK_STATUS_RUNNING) {
            return false;
        }
    }

    return true;
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

static void task_monitor_task(void *pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "Task monitor started");

    while (1) {
        // 监控任务状态
        xSemaphoreTake(s_state.mutex, portMAX_DELAY);
        
        for (uint32_t i = 0; i < s_state.config.task_count; i++) {
            task_info_t *task = &s_state.tasks[i];
            if (task->status == TASK_STATUS_RUNNING) {
                // 检查任务心跳（如果实现了心跳机制）
                TickType_t current_time = xTaskGetTickCount();
                if (task->config.heartbeat_timeout_ms > 0) {
                    TickType_t timeout_ticks = pdMS_TO_TICKS(task->config.heartbeat_timeout_ms);
                    if ((current_time - task->last_heartbeat) > timeout_ticks) {
                        ESP_LOGW(TAG, "Task '%s' heartbeat timeout", task->config.name);
                        task->status = TASK_STATUS_ERROR;
                    }
                }
            }
        }
        
        xSemaphoreGive(s_state.mutex);
        
        // 等待下一次监控
        vTaskDelay(pdMS_TO_TICKS(s_state.config.monitor_interval_ms));
    }
}

static esp_err_t register_all_system_tasks(void)
{
    ESP_LOGI(TAG, "Registering system tasks...");

    // 配置管理器任务
    task_config_t config_task = {
        .name = "config_manager",
        .priority = TASK_PRIORITY_HIGH,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = config_manager_task_init,
        .start_func = config_manager_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 10000,
        .dependency_count = 0
    };
    task_manager_register_task(&config_task);

    // 设备接口任务
    task_config_t device_task = {
        .name = "device_interface",
        .priority = TASK_PRIORITY_HIGH,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = device_interface_task_init,
        .start_func = device_interface_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 10000,
        .dependency_count = 1
    };
    strcpy(device_task.dependencies[0], "config_manager");
    task_manager_register_task(&device_task);

    // 以太网接口任务
    task_config_t ethernet_task = {
        .name = "ethernet_interface",
        .priority = TASK_PRIORITY_MEDIUM,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = ethernet_interface_task_init,
        .start_func = ethernet_interface_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 15000,
        .dependency_count = 1
    };
    strcpy(ethernet_task.dependencies[0], "config_manager");
    task_manager_register_task(&ethernet_task);

    // 硬件控制任务
    task_config_t hardware_task = {
        .name = "hardware_control",
        .priority = TASK_PRIORITY_MEDIUM,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = hardware_control_task_init,
        .start_func = hardware_control_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 10000,
        .dependency_count = 1
    };
    strcpy(hardware_task.dependencies[0], "device_interface");
    task_manager_register_task(&hardware_task);

    // 控制台接口任务
    task_config_t console_task = {
        .name = "console_interface",
        .priority = TASK_PRIORITY_LOW,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = console_interface_task_init,
        .start_func = console_interface_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 0, // 控制台任务不需要心跳检查
        .dependency_count = 0
    };
    task_manager_register_task(&console_task);

    // 系统监控任务
    task_config_t monitor_task = {
        .name = "system_monitor",
        .priority = TASK_PRIORITY_LOW,
        .type = TASK_TYPE_INDEPENDENT,
        .enabled = true,
        .init_func = system_monitor_task_init,
        .start_func = system_monitor_task_start,
        .stop_func = NULL,
        .heartbeat_timeout_ms = 30000,
        .dependency_count = 0
    };
    task_manager_register_task(&monitor_task);

    ESP_LOGI(TAG, "System tasks registered successfully");
    return ESP_OK;
}

// =============================================================================
// 辅助函数实现
// =============================================================================

static const char* task_status_to_string(task_status_t status)
{
    switch (status) {
        case TASK_STATUS_STOPPED: return "STOPPED";
        case TASK_STATUS_RUNNING: return "RUNNING";
        case TASK_STATUS_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static const char* task_type_to_string(task_type_t type)
{
    switch (type) {
        case TASK_TYPE_BLOCKING: return "BLOCKING";
        case TASK_TYPE_INDEPENDENT: return "INDEPENDENT";
        default: return "UNKNOWN";
    }
}

static const char* task_priority_to_string(task_priority_t priority)
{
    switch (priority) {
        case TASK_PRIORITY_CRITICAL: return "CRITICAL";
        case TASK_PRIORITY_HIGH: return "HIGH";
        case TASK_PRIORITY_MEDIUM: return "MEDIUM";
        case TASK_PRIORITY_LOW: return "LOW";
        default: return "UNKNOWN";
    }
}
