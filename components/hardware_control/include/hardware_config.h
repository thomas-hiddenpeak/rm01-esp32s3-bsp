/*
 * ESP32S3 硬件配置文件
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

// ==================== 默认值配置 ====================

// 风扇默认参数
#define DEFAULT_FAN_SPEED_ON        50      // 默认风扇开启速度(%)
#define DEFAULT_FAN_SPEED_OFF       0       // 默认风扇关闭速度(%)
#define DEFAULT_FAN_AUTO_ENABLE     false   // 默认自动风扇控制关闭

// LED默认控制参数
#define DEFAULT_LED_BRIGHTNESS      50      // 默认LED亮度(%)
#define DEFAULT_BOARD_LED_COLOR     COLOR_BLUE   // 默认板载LED颜色
#define DEFAULT_TOUCH_LED_COLOR     COLOR_GREEN  // 默认触摸LED颜色
#define DEFAULT_LED_EFFECT_ENABLE   false   // 默认LED效果关闭
#define DEFAULT_LED_RAINBOW_SPEED   100     // 默认彩虹效果速度(毫秒)

// 以太网默认配置
#define DEFAULT_ETH_IP_ADDR         "10.10.99.97"    // 默认IP地址
#define DEFAULT_ETH_GATEWAY         "10.10.99.97"    // 默认网关地址
#define DEFAULT_ETH_NETMASK         "255.255.255.0"  // 默认子网掩码
#define DEFAULT_ETH_DNS_SERVER      "8.8.8.8"        // 默认DNS服务器

// DHCP服务器默认配置
#define DEFAULT_DHCP_ENABLE         true             // 默认启用DHCP服务器
#define DEFAULT_DHCP_START_IP       "10.10.99.100"   // 默认DHCP起始IP
#define DEFAULT_DHCP_END_IP         "10.10.99.101"   // 默认DHCP结束IP
#define DEFAULT_DHCP_LEASE_TIME     24               // 默认租约时间(小时)
#define DEFAULT_DHCP_MAX_CLIENTS    8                // 默认最大客户端数

// 网关服务默认配置
#define DEFAULT_GATEWAY_ENABLE      true             // 默认启用网关服务
#define DEFAULT_NAT_ENABLE          true             // 默认启用NAT转发
#define DEFAULT_FIREWALL_ENABLE     false            // 默认关闭防火墙

// 系统默认配置
#define DEFAULT_AUTO_SAVE_CONFIG    true             // 默认自动保存配置
#define DEFAULT_CONFIG_SAVE_INTERVAL 300000         // 默认配置保存间隔(毫秒)
#define DEFAULT_STARTUP_LOAD_CONFIG true             // 默认启动时加载配置

// GPIO预定义(可扩展)
#define GPIO_LED_BUILTIN    2       // 内置LED引脚(如果有)
#define GPIO_BUTTON         0       // 按键引脚(如果有)

// USB MUX控制引脚
#define ESP32_MUX1_SEL      8       // USB MUX1选择引脚 (GPIO8)
#define ESP32_MUX2_SEL      48      // USB MUX2选择引脚 (GPIO48)

// AGX电源控制引脚
#define AGX_POWER_PIN      3       // AGX关机引脚 (GPIO3)
#define AGX_RESET_PIN      1       // AGX重启引脚 (GPIO1)
#define AGX_RECOVERY_PIN   40      // AGX恢复模式引脚 (GPIO40)

// LPMU电源控制引脚
#define LPMU_POWER_BTN_PIN  46      // LPMU电源按钮引脚 (GPIO46)
#define LPMU_RESET_PIN      2       // LPMU重启引脚 (GPIO2)

// W5500 (Ethernet) 引脚配置
#define BSP_W5500_RST_PIN        39      // W5500复位引脚
#define BSP_W5500_INT_PIN        38      // W5500中断引脚
#define BSP_W5500_MISO_PIN       13      // W5500 SPI MISO引脚
#define BSP_W5500_SCLK_PIN       12      // W5500 SPI SCK引脚
#define BSP_W5500_MOSI_PIN       11      // W5500 SPI MOSI引脚
#define BSP_W5500_CS_PIN         10      // W5500 SPI CS引脚

// TF Card (SDMMC 4-bit) 引脚配置
#define BSP_TF_D0_PIN            4       // TF卡数据线0
#define BSP_TF_D1_PIN            5       // TF卡数据线1
#define BSP_TF_D2_PIN            6       // TF卡数据线2
#define BSP_TF_D3_PIN            7       // TF卡数据线3
#define BSP_TF_CMD_PIN           15      // TF卡命令线
#define BSP_TF_CK_PIN            16      // TF卡时钟线

// 电源控制时序配置
#define AGX_RESET_PULSE_MS     1000    // AGX重启脉冲持续时间(毫秒)
#define LPMU_POWER_PULSE_MS     300     // LPMU电源按钮脉冲持续时间(毫秒)
#define LPMU_RESET_PULSE_MS     300     // LPMU重启脉冲持续时间(毫秒)

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
