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
#define CONSOLE_BUF_SIZE    1024

// 系统监控配置
#define MONITOR_INTERVAL_MS 10000   // 监控间隔(毫秒)
#define LOW_MEMORY_THRESHOLD 10000  // 低内存警告阈值(字节)

// 默认值配置
#define DEFAULT_LED_BRIGHTNESS  50  // 默认LED亮度(%)
#define DEFAULT_FAN_SPEED_ON    50  // 默认风扇开启速度(%)

// GPIO预定义(可扩展)
#define GPIO_LED_BUILTIN    2       // 内置LED引脚(如果有)
#define GPIO_BUTTON         0       // 按键引脚(如果有)

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
