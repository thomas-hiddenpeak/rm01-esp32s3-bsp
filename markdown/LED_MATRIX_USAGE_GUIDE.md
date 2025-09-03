# LED矩阵控制使用指南

## 概述

ESP32S3 BSP支持32x32 WS2812 LED矩阵控制，总计1024个可独立控制的RGB LED。LED矩阵连接到GPIO 9，支持亮度调节、像素控制、动画加载和配置持久化。

## 硬件规格

- **矩阵尺寸**: 32x32 像素 (1024个LED)
- **LED类型**: WS2812 (可寻址RGB LED)
- **控制引脚**: GPIO 9
- **驱动方式**: RMT硬件驱动
- **颜色深度**: 24位RGB (每通道0-255)
- **亮度范围**: 0-100%

## 控制台命令详解

### 基本控制命令

#### 1. 清空矩阵
```bash
matrix clear
```
- 功能：将所有LED设置为黑色(关闭状态)
- 用途：重置显示，准备新的图案

#### 2. 显示测试图案
```bash
matrix test
```
- 功能：显示内置测试图案
- 图案包含：
  - 红色边框
  - 绿色对角线
  - 蓝色中心十字
- 用途：验证矩阵工作状态

#### 3. 设置亮度
```bash
matrix bright <亮度值>
```
- 参数：亮度值 (0-100)
- 功能：调整整个矩阵的亮度
- 示例：
  ```bash
  matrix bright 30    # 设置亮度为30%
  matrix bright 80    # 设置亮度为80%
  ```
- 特点：立即生效，影响当前显示的所有像素

### 像素控制命令

#### 4. 设置单个像素
```bash
matrix pixel <x> <y> <r> <g> <b>
```
- 参数：
  - x, y：坐标 (0-31)
  - r, g, b：RGB颜色值 (0-255)
- 功能：设置指定位置的像素颜色
- 示例：
  ```bash
  matrix pixel 15 15 255 0 0     # 中心红色像素
  matrix pixel 0 0 0 255 0       # 左上角绿色像素
  matrix pixel 31 31 0 0 255     # 右下角蓝色像素
  ```

### 动画加载命令

#### 5. 加载动画
```bash
matrix load <动画名称>
```
- 参数：动画名称 (如: Logo)
- 功能：从SD卡的matrix.json文件加载指定动画
- 示例：
  ```bash
  matrix load Logo               # 加载Logo动画
  ```
- 要求：
  - SD卡已挂载
  - 存在 `/sdcard/matrix.json` 文件
  - JSON文件格式正确

### 配置管理命令

#### 6. 保存配置
```bash
matrix config save
```
- 功能：保存当前LED矩阵设置为启动默认配置
- 保存内容：
  - 当前亮度设置
  - 自动启动状态 (设为true)
  - 默认动画 (设为Logo)
  - 启用状态 (设为true)
- 用途：确保重启后恢复当前设置

#### 7. 显示配置
```bash
matrix config show
```
- 功能：显示当前LED矩阵配置信息
- 显示内容：
  - 矩阵亮度设置
  - 启动动画名称
  - 自动启动状态
  - 启用状态

### 测试命令

#### 8. 矩阵测试
```bash
test matrix
```
- 功能：执行LED矩阵硬件测试
- 等同于 `matrix test` 命令
- 用途：验证矩阵硬件工作状态

## 动画文件格式

### JSON文件结构
LED矩阵动画存储在 `/sdcard/matrix.json` 文件中：

```json
{
  "animations": [
    {
      "name": "Logo",
      "points": [
        {
          "type": "point",
          "x": 11,
          "y": 8,
          "r": 151,
          "g": 189,
          "b": 246
        }
      ]
    }
  ]
}
```

### 字段说明
- `animations`: 动画数组
- `name`: 动画名称 (用于`matrix load`命令)
- `points`: 像素点数组
- `x`, `y`: 像素坐标 (0-31)
- `r`, `g`, `b`: RGB颜色值 (0-255)

## 使用场景示例

### 1. 快速测试
```bash
# 显示测试图案
matrix test

# 调整亮度
matrix bright 25

# 保存设置
matrix config save
```

### 2. 手动绘制
```bash
# 清空画布
matrix clear

# 绘制简单图案
matrix pixel 15 15 255 0 0      # 中心红点
matrix pixel 14 15 255 255 0    # 左侧黄点  
matrix pixel 16 15 0 255 255    # 右侧青点

# 保存当前设置
matrix config save
```

### 3. 动画演示
```bash
# 设置合适的亮度
matrix bright 40

# 加载Logo动画
matrix load Logo

# 保存为启动配置
matrix config save
```

### 4. 亮度调节
```bash
# 显示内容
matrix load Logo

# 实时调节亮度
matrix bright 10    # 很暗
matrix bright 50    # 中等
matrix bright 90    # 很亮
```

## 自动启动功能

### 配置自动启动
1. 设置理想的显示效果：
   ```bash
   matrix bright 30
   matrix load Logo
   ```

2. 保存为启动配置：
   ```bash
   matrix config save
   ```

3. 重启验证：
   ```bash
   reboot
   ```

### 启动流程
系统启动时的LED矩阵自动启动流程：
1. SD卡挂载完成
2. 检查LED矩阵配置
3. 如果启用自动启动：
   - 初始化LED矩阵
   - 设置保存的亮度
   - 加载指定动画
   - 如果动画文件不存在，显示测试图案

### 配置参数
自动启动配置包含以下参数：
- `matrix_brightness`: 启动亮度 (默认30%)
- `matrix_startup_animation`: 启动动画 (默认"Logo") 
- `matrix_auto_start`: 自动启动开关 (默认true)
- `matrix_enable`: 矩阵功能开关 (默认true)

## 故障排除

### 1. 矩阵不显示
```bash
# 检查硬件初始化
test matrix

# 检查亮度设置
matrix bright 50

# 尝试显示测试图案
matrix test
```

### 2. 动画加载失败
```bash
# 检查SD卡状态
sdcard_info

# 检查动画文件
sdcard_cat /sdcard/matrix.json

# 验证文件格式
sdcard_stat /sdcard/matrix.json
```

### 3. 配置保存失败
```bash
# 检查配置状态
matrix config show

# 重试保存
matrix config save

# 检查NVS状态
load
```

### 4. 自动启动不工作
```bash
# 检查配置
matrix config show

# 重新配置自动启动
matrix config save

# 检查SD卡挂载
sdcard_info
```

## 性能优化建议

### 1. 亮度控制
- 建议启动亮度不超过50%以节省功耗
- 在明亮环境下可适当提高亮度
- 夜间使用建议亮度在10-20%

### 2. 动画设计
- 避免全白色显示以减少功耗
- 复杂动画建议控制像素点数量
- 推荐使用中等亮度的彩色图案

### 3. 系统资源
- LED矩阵使用RMT硬件驱动，CPU占用较低
- 内存缓冲区占用约3KB (1024像素 × 3字节)
- 支持与其他功能同时使用

## 技术规格

| 参数 | 数值 | 说明 |
|------|------|------|
| 矩阵尺寸 | 32×32 | 总共1024个像素 |
| 控制引脚 | GPIO 9 | RMT硬件驱动 |
| 颜色深度 | 24位 | 每通道8位，1600万色 |
| 亮度级别 | 101级 | 0-100%，支持PWM调光 |
| 更新频率 | >60Hz | 流畅显示 |
| 功耗 | 最大60W | 全白满亮度时 |
| 驱动方式 | WS2812协议 | 单线串行通信 |

## 常用命令速查

| 命令 | 功能 | 示例 |
|------|------|------|
| `matrix clear` | 清空矩阵 | - |
| `matrix test` | 显示测试图案 | - |
| `matrix bright 30` | 设置亮度30% | - |
| `matrix pixel 15 15 255 0 0` | 中心红色像素 | - |
| `matrix load Logo` | 加载Logo动画 | - |
| `matrix config save` | 保存配置 | - |
| `matrix config show` | 显示配置 | - |
| `test matrix` | 硬件测试 | - |

这个LED矩阵控制系统提供了完整的显示控制功能，支持从简单的像素控制到复杂的动画显示，适合各种应用场景。
