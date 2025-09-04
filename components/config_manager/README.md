# Configuration Manager Component

ESP32S3 配置管理组件，提供统一的配置管理接口。

## 功能特性

- **完整的配置管理**: 风扇、LED、以太网、DHCP、网关服务配置
- **NVS存储**: 安全可靠的配置保存和加载
- **默认参数**: 完整的工厂默认参数设置
- **配置验证**: 数据完整性检查和校验
- **自动应用**: 启动时自动加载和应用配置
- **事件回调**: 配置操作事件通知

## 配置类型

### 1. 风扇配置
- 默认开启/关闭速度
- 自动温控功能
- 温度阈值设置

### 2. LED配置
- 默认亮度设置
- 板载和触摸LED默认颜色
- LED效果控制
- 彩虹效果速度

### 3. 以太网配置
- IP地址、网关、子网掩码
- DNS服务器设置
- 自动启动控制

### 4. DHCP服务器配置
- 服务启用/禁用
- IP地址池范围
- 租约时间设置
- 最大客户端数量

### 5. 网关服务配置
- 网关功能启用/禁用
- NAT转发设置
- 防火墙控制

### 6. 系统配置
- 自动保存设置
- 保存间隔时间
- 启动加载配置
- 调试日志控制

## 使用方法

### 初始化
```c
#include "config_manager.h"

// 初始化配置管理器
esp_err_t ret = config_manager_init();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "配置管理器初始化失败");
}
```

### 加载配置
```c
// 从NVS加载配置
ret = config_manager_load();
if (ret == ESP_ERR_NOT_FOUND) {
    ESP_LOGI(TAG, "使用默认配置");
} else if (ret != ESP_OK) {
    ESP_LOGE(TAG, "配置加载失败");
}

// 应用配置到所有子系统
config_manager_apply_config();
```

### 保存配置
```c
// 保存当前配置到NVS
ret = config_manager_save();
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "配置保存失败");
}
```

### 设置参数
```c
// 设置风扇默认速度
config_manager_set_fan_speed(70, 0);

// 设置LED默认参数
led_color_config_t blue = {0, 0, 255};
led_color_config_t green = {0, 255, 0};
config_manager_set_led_defaults(80, blue, green);

// 设置以太网IP配置
config_manager_set_ethernet_ip("192.168.1.100", "192.168.1.1", 
                               "255.255.255.0", "8.8.8.8");

// 设置DHCP参数
config_manager_set_dhcp_params(true, "192.168.1.101", 
                               "192.168.1.110", 24);
```

### 查看配置
```c
// 打印当前配置
config_manager_print_config();

// 获取特定配置
const fan_config_t *fan_cfg = config_manager_get_fan_config();
const led_config_t *led_cfg = config_manager_get_led_config();
```

## 事件回调

```c
void config_event_handler(config_event_t event, const char *message)
{
    switch (event) {
        case CONFIG_EVENT_SAVED:
            printf("配置已保存: %s\n", message);
            break;
        case CONFIG_EVENT_LOADED:
            printf("配置已加载: %s\n", message);
            break;
        case CONFIG_EVENT_RESET:
            printf("配置已重置: %s\n", message);
            break;
        case CONFIG_EVENT_ERROR:
            printf("配置错误: %s\n", message);
            break;
    }
}

// 注册事件回调
config_manager_register_event_callback(config_event_handler);
```

## 默认参数

所有配置项都有预定义的默认值：

- **风扇**: 开启速度50%，关闭速度0%
- **LED**: 亮度50%，板载蓝色，触摸绿色
- **以太网**: IP 10.10.99.97，自动启动
- **DHCP**: 启用，池范围 10.10.99.100-110，租约24小时
- **网关**: 启用，NAT开启，防火墙关闭
- **系统**: 自动保存，间隔5分钟，启动加载

## API参考

详细的API文档请参考 `include/config_manager.h` 文件。
