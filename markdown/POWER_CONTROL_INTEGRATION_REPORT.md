# Orin和N305电源控制功能集成报告

## 概述

成功为ESP32S3硬件控制组件增加了Orin和N305设备的电源控制功能，包括开机、关机、重启和恢复模式等操作。

## 新增功能

### 1. GPIO定义
- **Orin电源控制引脚**:
  - 关机引脚: GPIO3 (ORIN_POWER_PIN)
  - 重启引脚: GPIO1 (ORIN_RESET_PIN)  
  - 恢复模式引脚: GPIO40 (ORIN_RECOVERY_PIN)

- **N305电源控制引脚**:
  - 电源按钮引脚: GPIO46 (N305_POWER_BTN_PIN)
  - 重启引脚: GPIO2 (N305_RESET_PIN)

- **时序配置**:
  - Orin重启脉冲: 1000ms
  - N305电源按钮脉冲: 300ms
  - N305重启脉冲: 300ms

### 2. 新增数据类型

```c
typedef enum {
    POWER_STATE_OFF = 0,     /*!< 设备已关机 */
    POWER_STATE_ON = 1,      /*!< 设备已开机 */
    POWER_STATE_UNKNOWN = 2  /*!< 设备状态未知 */
} power_state_t;
```

### 3. 硬件状态结构体扩展

在`hardware_status_t`中新增：
- `power_state_t orin_power_state;` - Orin电源状态
- `power_state_t n305_power_state;` - N305电源状态

### 4. Orin电源控制接口

#### `esp_err_t orin_power_on(void)`
- **功能**: Orin设备开机
- **操作**: 将GPIO3拉低
- **说明**: GPIO3低电平时Orin处于开机状态

#### `esp_err_t orin_power_off(void)`
- **功能**: Orin设备关机
- **操作**: 将GPIO3持续拉高
- **说明**: GPIO3高电平时Orin关机

#### `esp_err_t orin_reset(void)`
- **功能**: Orin设备重启
- **操作**: GPIO1拉高1000ms后拉低

#### `esp_err_t orin_enter_recovery_mode(void)`
- **功能**: Orin设备进入恢复模式
- **操作步骤**:
  1. 将GPIO40拉高
  2. 执行Orin重启
  3. 将GPIO40拉低
  4. 将USB MUX切换到AGX

### 5. N305电源控制接口

#### `esp_err_t n305_power_toggle(void)`
- **功能**: N305设备开机/关机切换
- **操作**: GPIO46拉高300ms后拉低
- **说明**: 此操作会在开机和关机状态间切换

#### `esp_err_t n305_reset(void)`
- **功能**: N305设备重启
- **操作**: GPIO2拉高300ms后拉低

### 6. 状态查询接口

#### `esp_err_t orin_get_power_state(power_state_t *state)`
- **功能**: 获取Orin电源状态

#### `esp_err_t n305_get_power_state(power_state_t *state)`
- **功能**: 获取N305电源状态

#### `const char *power_state_get_name(power_state_t state)`
- **功能**: 获取电源状态名称字符串
- **返回值**: "OFF"、"ON"、"UNKNOWN"或"INVALID"

### 7. 测试接口

#### `esp_err_t hardware_test_orin_power(void)`
- **功能**: 测试Orin电源控制功能
- **测试内容**: 开机 → 延时 → 关机 → 延时

#### `esp_err_t hardware_test_n305_power(void)`
- **功能**: 测试N305电源控制功能
- **测试内容**: 电源切换 → 延时 → 再次切换 → 延时

## 初始化过程

### 新增静态函数 `init_power_control_gpio()`
1. 配置所有电源控制GPIO为输出模式
2. 设置初始状态：
   - Orin默认开机状态 (GPIO3 = LOW)
   - 其他控制引脚默认为低电平
3. 更新电源状态：
   - Orin: POWER_STATE_ON
   - N305: POWER_STATE_UNKNOWN

## 状态显示更新

`hardware_print_status()`函数现在包含：
- USB MUX目标显示
- Orin电源状态显示
- N305电源状态显示

## 使用示例

```c
// 初始化硬件控制
hardware_control_init();

// Orin操作
orin_power_on();      // 开机
orin_reset();         // 重启
orin_enter_recovery_mode(); // 进入恢复模式
orin_power_off();     // 关机

// N305操作
n305_power_toggle();  // 开机/关机切换
n305_reset();         // 重启

// 状态查询
power_state_t orin_state, n305_state;
orin_get_power_state(&orin_state);
n305_get_power_state(&n305_state);

// 测试功能
hardware_test_orin_power();
hardware_test_n305_power();

// 查看完整状态
hardware_print_status();
```

## 文件修改清单

1. **hardware_config.h** - 添加GPIO定义和时序配置
2. **hardware_control.h** - 添加接口声明、数据类型和常量定义
3. **hardware_control.c** - 完全重写，添加所有电源控制功能实现

## 构建状态

✅ 项目构建成功，无编译错误
✅ 所有新增接口已实现
✅ 测试函数已集成
✅ 状态管理已完善

## 注意事项

1. **GPIO冲突检查**: 确保新使用的GPIO引脚不与其他功能冲突
2. **时序要求**: 严格按照设备要求的时序执行电源操作
3. **状态管理**: 电源状态基于软件记录，实际状态可能需要硬件反馈确认
4. **安全考虑**: 建议在实际部署前进行充分的硬件测试

## 后续建议

1. 添加电源状态的硬件反馈检测
2. 实现更智能的电源状态管理
3. 添加电源操作的错误恢复机制
4. 考虑添加操作日志记录功能
