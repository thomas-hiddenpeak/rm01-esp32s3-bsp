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
static int cmd_orin(int argc, char **argv);
static int cmd_n305(int argc, char **argv);
static int cmd_debug(int argc, char **argv);
static int cmd_test(int argc, char **argv);
static int cmd_save(int argc, char **argv);
static int cmd_load(int argc, char **argv);
static int cmd_clear(int argc, char **argv);

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
            .help = "USB MUX控制: usbmux esp32s3|agx|n305|status",
            .func = &cmd_usbmux,
        },
        {
            .command = "orin",
            .help = "Orin电源控制: orin on|off|reset|recovery|status",
            .func = &cmd_orin,
        },
        {
            .command = "n305",
            .help = "N305电源控制: n305 toggle|reset|status",
            .func = &cmd_n305,
        },
        {
            .command = "debug",
            .help = "调试信息: debug status|hardware|device",
            .func = &cmd_debug,
        },
        {
            .command = "test",
            .help = "硬件测试: test fan|bled|tled|gpio <pin>|gpio_input <pin>|orin|n305|all|quick|stress <ms>",
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
        }
    };

    for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }

    ESP_LOGI(TAG, "Config commands registered");
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
    printf("\n风扇控制:\n");
    printf("  fan <0-100>   - 设置风扇速度 (0-100%%)\n");
    printf("  fan off       - 关闭风扇\n");
    printf("  fan on        - 打开风扇(50%%)\n");
    printf("\n板载LED控制:\n");
    printf("  bled <r> <g> <b>     - 设置板载LED颜色 (0-255)\n");
    printf("  bled bright <0-100>  - 设置板载LED亮度\n");
    printf("  bled off             - 关闭板载LED\n");
    printf("  bled rainbow         - 彩虹效果\n");
    printf("\n触摸LED控制:\n");
    printf("  tled <r> <g> <b>     - 设置触摸LED颜色 (0-255)\n");
    printf("  tled bright <0-100>  - 设置触摸LED亮度\n");
    printf("  tled off             - 关闭触摸LED\n");
    printf("\nIO控制:\n");
    printf("  gpio <pin> high      - 设置GPIO引脚为高电平\n");
    printf("  gpio <pin> low       - 设置GPIO引脚为低电平\n");
    printf("  gpio <pin> input     - 读取GPIO引脚输入状态\n");
    printf("\n测试命令:\n");
    printf("  test fan             - 测试风扇功能\n");
    printf("  test bled            - 测试板载LED\n");
    printf("  test tled            - 测试触摸LED\n");
    printf("  test gpio <pin>      - 安全测试GPIO输出功能\n");
    printf("  test gpio_input <pin> - 测试GPIO输入功能\n");
    printf("  test all             - 测试所有硬件\n");
    printf("  test quick           - 快速测试\n");
    printf("  test stress <ms>     - 压力测试\n");
    printf("\n提示：使用 TAB 键自动补全，上下箭头浏览历史\n");
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
    }
    else if (strcmp(argv[1], "on") == 0) {
        ret = fan_start();
    }
    else {
        int speed = atoi(argv[1]);
        if (speed >= 0 && speed <= 100) {
            ret = fan_set_speed(speed);
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
    else if (strcmp(argv[1], "n305") == 0) {
        ret = usb_mux_set_target(USB_MUX_N305);
        if (ret == ESP_OK) {
            printf("USB-C接口已切换到N305\n");
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
        printf("用法: usbmux esp32s3|agx|n305|status\n");
        printf("  esp32s3 - 切换到ESP32S3 USB接口\n");
        printf("  agx     - 切换到AGX USB接口\n");
        printf("  n305    - 切换到N305 USB接口\n");
        printf("  status  - 显示当前USB接口状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("USB MUX操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_orin(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: orin on|off|reset|recovery|status\n");
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
        ret = orin_power_on();
        if (ret == ESP_OK) {
            printf("Orin设备已开机\n");
        }
    }
    else if (strcmp(argv[1], "off") == 0) {
        ret = orin_power_off();
        if (ret == ESP_OK) {
            printf("Orin设备已关机\n");
        }
    }
    else if (strcmp(argv[1], "reset") == 0) {
        printf("正在重启Orin设备...\n");
        ret = orin_reset();
        if (ret == ESP_OK) {
            printf("Orin设备重启完成\n");
        }
    }
    else if (strcmp(argv[1], "recovery") == 0) {
        printf("正在进入Orin恢复模式...\n");
        ret = orin_enter_recovery_mode();
        if (ret == ESP_OK) {
            printf("Orin设备已进入恢复模式\n");
            printf("USB-C接口已自动切换到AGX\n");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        power_state_t state;
        ret = orin_get_power_state(&state);
        if (ret == ESP_OK) {
            printf("Orin电源状态: %s\n", power_state_get_name(state));
        }
    }
    else {
        printf("用法: orin on|off|reset|recovery|status\n");
        printf("  on       - 开机Orin设备\n");
        printf("  off      - 关机Orin设备\n");
        printf("  reset    - 重启Orin设备\n");
        printf("  recovery - 进入恢复模式并切换USB到AGX\n");
        printf("  status   - 显示Orin电源状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("Orin操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_n305(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: n305 toggle|reset|status\n");
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
        printf("正在切换N305电源状态...\n");
        ret = n305_power_toggle();
        if (ret == ESP_OK) {
            power_state_t state;
            if (n305_get_power_state(&state) == ESP_OK) {
                printf("N305电源已切换到: %s\n", power_state_get_name(state));
            } else {
                printf("N305电源状态已切换\n");
            }
        }
    }
    else if (strcmp(argv[1], "reset") == 0) {
        printf("正在重启N305设备...\n");
        ret = n305_reset();
        if (ret == ESP_OK) {
            printf("N305设备重启完成\n");
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        power_state_t state;
        ret = n305_get_power_state(&state);
        if (ret == ESP_OK) {
            printf("N305电源状态: %s\n", power_state_get_name(state));
        }
    }
    else {
        printf("用法: n305 toggle|reset|status\n");
        printf("  toggle - 切换N305开机/关机状态\n");
        printf("  reset  - 重启N305设备\n");
        printf("  status - 显示N305电源状态\n");
        return 1;
    }
    
    if (ret != ESP_OK) {
        printf("N305操作失败: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

static int cmd_debug(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: debug status|hardware|device\n");
        return 1;
    }
    
    if (strcmp(argv[1], "status") == 0) {
        printf("=== 系统初始化状态 ===\n");
        printf("控制台接口: %s\n", s_console_state.initialized ? "已初始化" : "未初始化");
        printf("硬件控制: %s\n", hardware_control_is_initialized() ? "已初始化" : "未初始化");
        
        // 检查设备接口状态
        device_status_t status;
        esp_err_t ret = device_get_full_status(&status);
        if (ret == ESP_OK) {
            printf("设备接口: 已初始化\n");
            printf("硬件控制可用: %s\n", status.hardware_available ? "是" : "否");
            printf("系统监控可用: %s\n", status.monitor_available ? "是" : "否");
        } else {
            printf("设备接口: 未初始化或获取状态失败\n");
        }
        printf("====================\n");
    }
    else if (strcmp(argv[1], "hardware") == 0) {
        if (hardware_control_is_initialized()) {
            hardware_print_status();
        } else {
            printf("硬件控制组件未初始化\n");
        }
    }
    else if (strcmp(argv[1], "device") == 0) {
        device_print_full_status();
    }
    else {
        printf("用法: debug status|hardware|device\n");
        printf("  status   - 显示系统初始化状态\n");
        printf("  hardware - 显示硬件状态\n");
        printf("  device   - 显示设备状态\n");
        return 1;
    }
    
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        printf("用法: test fan|bled|tled|gpio <pin>|gpio_input <pin>|orin|n305|all|quick|stress <ms>\n");
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
    else if (strcmp(argv[1], "orin") == 0) {
        ret = hardware_test_orin_power();
    }
    else if (strcmp(argv[1], "n305") == 0) {
        ret = hardware_test_n305_power();
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
        printf("  orin         - Orin电源测试\n");
        printf("  n305         - N305电源测试\n");
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
