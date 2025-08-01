/*
 * ESP32S3 控制台程序硬件配置
 * 修改此文件来适配不同的硬件设计
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// 风扇控制配置
#define FAN_PWM_PIN         41      // 风扇PWM控制引脚
#define FAN_PWM_TIMER       LEDC_TIMER_0
#define FAN_PWM_MODE        LEDC_LOW_SPEED_MODE
#define FAN_PWM_CHANNEL     LEDC_CHANNEL_0
#define FAN_PWM_RESOLUTION  LEDC_TIMER_8_BIT
#define FAN_PWM_FREQUENCY   25000   // 25kHz PWM频率

// WS2812 LED配置
#define BOARD_WS2812_PIN    42      // 板载WS2812控制引脚
#define BOARD_WS2812_NUM    28      // 板载WS2812数量
#define TOUCH_WS2812_PIN    45      // 触摸开关WS2812引脚
#define TOUCH_WS2812_NUM    1       // 触摸开关WS2812数量

// LED Strip RMT配置
#define LED_RMT_CLK_FREQ    10000000  // 10MHz RMT时钟频率

// 串口配置
#define CONSOLE_UART_NUM    UART_NUM_0
#define CONSOLE_BAUD_RATE   115200
// CONSOLE_BUF_SIZE 现在定义在 console_interface.h 中

// 系统监控配置
#define MONITOR_INTERVAL_MS 10000   // 监控间隔(毫秒)
#define LOW_MEMORY_THRESHOLD 10000  // 低内存警告阈值(字节)

// 默认值配置
#define DEFAULT_LED_BRIGHTNESS  50  // 默认LED亮度(%)
#define DEFAULT_FAN_SPEED_ON    50  // 默认风扇开启速度(%)

// GPIO预定义(可扩展)
#define GPIO_LED_BUILTIN    2       // 内置LED引脚(如果有)
#define GPIO_BUTTON         0       // 按键引脚(如果有)

// USB MUX控制引脚
#define ESP32_MUX1_SEL      8       // USB MUX1选择引脚 (GPIO8)
#define ESP32_MUX2_SEL      48      // USB MUX2选择引脚 (GPIO48)

// Orin电源控制引脚
#define ORIN_POWER_PIN      3       // Orin关机引脚 (GPIO3)
#define ORIN_RESET_PIN      1       // Orin重启引脚 (GPIO1)
#define ORIN_RECOVERY_PIN   40      // Orin恢复模式引脚 (GPIO40)

// N305电源控制引脚
#define N305_POWER_BTN_PIN  46      // N305电源按钮引脚 (GPIO46)
#define N305_RESET_PIN      2       // N305重启引脚 (GPIO2)

// 电源控制时序配置
#define ORIN_RESET_PULSE_MS     1000    // Orin重启脉冲持续时间(毫秒)
#define N305_POWER_PULSE_MS     300     // N305电源按钮脉冲持续时间(毫秒)
#define N305_RESET_PULSE_MS     300     // N305重启脉冲持续时间(毫秒)

// 颜色预定义
#define COLOR_RED           {255, 0, 0}
#define COLOR_GREEN         {0, 255, 0}
#define COLOR_BLUE          {0, 0, 255}
#define COLOR_WHITE         {255, 255, 255}
#define COLOR_YELLOW        {255, 255, 0}
#define COLOR_CYAN          {0, 255, 255}
#define COLOR_MAGENTA       {255, 0, 255}
#define COLOR_ORANGE        {255, 165, 0}
#define COLOR_PURPLE        {128, 0, 128}
#define COLOR_OFF           {0, 0, 0}

#endif // HARDWARE_CONFIG_H
