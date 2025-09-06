# WS2812色彩校正系统使用指南

## 概述

WS2812色彩校正系统是专为RM-01设备设计的颜色增强解决方案，可以改善所有WS2812 LED的显示效果，包括：
- 板载LED (GPIO 42, 28颗LED)
- 触摸LED (GPIO 45, 1颗LED)  
- LED矩阵 (GPIO 9, 32x32=1024颗LED)

## 核心功能

### 🎨 色彩校正特性

1. **白点平衡校正**
   - 消除LED制造差异导致的色温偏差
   - 确保白色LED显示真正的白色而非偏黄或偏蓝

2. **伽马校正**
   - 改善视觉亮度线性度
   - 让颜色渐变更加自然和平滑

3. **亮度增强**
   - 提升整体亮度，改善可见度
   - 在保持色彩平衡的同时增强显示效果

4. **饱和度增强**
   - 增强颜色鲜艳度
   - 让色彩更加生动和引人注目

### 💾 配置管理

- **NVS持久化存储**: 所有设置自动保存到非易失性存储
- **启动自动加载**: 系统重启后自动恢复上次的配置
- **实时生效**: 参数调整立即应用到所有LED输出

## 控制台命令详解

### 基本控制

```bash
# 启用/禁用色彩校正
color enable                    # 启用色彩校正功能
color disable                   # 禁用色彩校正功能
```

### 参数调整

```bash
# 白点校正 (RGB值范围: 0-255)
color white 255 248 240         # 设置暖白色校正
color white 255 255 255         # 设置纯白色 (默认)

# 伽马校正 (范围: 0.1-5.0, 建议: 1.8-2.8)
color gamma 2.2                 # 标准sRGB伽马值
color gamma 1.8                 # 较低对比度
color gamma 2.8                 # 较高对比度

# 亮度提升 (范围: 0.1-3.0, 建议: 0.8-1.5)
color bright 1.0                # 无变化 (默认)
color bright 1.2                # 提升20%亮度
color bright 0.8                # 降低20%亮度

# 饱和度提升 (范围: 0.1-3.0, 建议: 0.8-1.5)
color sat 1.0                   # 无变化 (默认)
color sat 1.1                   # 提升10%饱和度
color sat 0.9                   # 降低10%饱和度
```

### 配置管理

```bash
# 查看当前配置
color show                      # 显示所有当前参数

# 配置持久化
color save                      # 保存当前配置到NVS
color load                      # 从NVS重新加载配置
color reset                     # 重置为出厂默认配置
```

## 使用场景和建议

### 🎯 典型使用场景

#### 1. 室内办公环境
```bash
color enable
color white 255 248 240         # 暖白色，减少蓝光
color gamma 2.2                 # 标准显示器伽马
color bright 0.8                # 降低亮度，护眼
color save
```

#### 2. 演示展示环境
```bash
color enable  
color white 255 255 255         # 纯白色，色彩准确
color gamma 2.4                 # 增强对比度
color bright 1.3                # 提升亮度，更醒目
color sat 1.2                   # 增强色彩饱和度
color save
```

#### 3. 夜间使用环境
```bash
color enable
color white 255 200 150         # 温暖橙色调
color gamma 1.8                 # 降低对比度
color bright 0.6                # 大幅降低亮度
color save
```

### 📊 参数调整指南

| 参数 | 建议范围 | 效果说明 |
|------|----------|----------|
| **白点R** | 240-255 | 减少红色偏差 |
| **白点G** | 240-255 | 减少绿色偏差 |  
| **白点B** | 200-255 | 减少蓝色偏差，暖色调 |
| **伽马** | 1.8-2.8 | 低值=柔和，高值=鲜明 |
| **亮度** | 0.6-1.5 | 根据环境光调整 |
| **饱和度** | 0.8-1.3 | 过高会失真 |

## 技术实现原理

### 色彩处理流程

1. **输入颜色**: 用户设置的RGB值 (0-255)
2. **白点校正**: 根据设定的白点进行色温校准
3. **饱和度调整**: 在HSL色彩空间中调整饱和度
4. **伽马校正**: 应用伽马曲线校正
5. **亮度调整**: 最终亮度调整
6. **输出**: 校正后的RGB值发送给WS2812

### 配置存储

```c
typedef struct {
    bool enabled;                   // 是否启用色彩校正
    rgb_color_t white_point;        // 白点校正值
    float gamma_correction;         // 伽马校正值
    float brightness_boost;         // 亮度提升系数
    float saturation_boost;         // 饱和度提升系数
} color_correction_config_t;
```

### NVS存储机制

- **命名空间**: `color_correct`
- **存储键**: `config` 
- **数据结构**: 完整的配置结构体
- **自动加载**: 系统启动时自动从NVS读取

## 故障排除

### 常见问题

**Q: 色彩校正不生效？**
```bash
color show                      # 检查是否启用
color enable                    # 确保已启用
color load                      # 重新加载配置
```

**Q: 颜色显示异常？**
```bash
color reset                     # 重置为默认值
color save                      # 保存重置后的配置
```

**Q: 配置丢失？**
```bash
# 重新设置并保存
color enable
color white 255 255 255
color gamma 2.2
color save
```

**Q: LED不亮或颜色错误？**
1. 检查硬件连接
2. 确认LED驱动是否正常
3. 使用 `color disable` 测试原始输出

### 调试命令

```bash
# 系统信息
info                            # 查看系统状态

# LED测试
bled 255 0 0                    # 测试板载LED红色
tled 0 255 0                    # 测试触摸LED绿色  
matrix test                     # 测试LED矩阵

# 色彩校正状态
color show                      # 显示详细配置
```

## 最佳实践

### 🚀 快速配置流程

1. **初始设置**
   ```bash
   color enable                 # 启用功能
   color show                   # 查看默认值
   ```

2. **基础调整**
   ```bash
   color white 255 248 240      # 设置暖白点
   color gamma 2.2              # 设置标准伽马
   ```

3. **微调优化**
   ```bash
   bled 255 255 255             # 用白色LED测试效果
   color bright 1.1             # 微调亮度
   color sat 1.05               # 微调饱和度
   ```

4. **保存配置**
   ```bash
   color save                   # 保存到NVS
   ```

### 💡 使用技巧

- **渐进调整**: 每次只调整一个参数，观察效果
- **测试验证**: 使用不同颜色LED测试校正效果
- **环境适配**: 根据使用环境调整亮度和色温
- **定期检查**: 定期使用 `color show` 检查配置状态

---

> 📖 **相关文档**: 
> - 硬件控制指南: `markdown/HARDWARE_DEBUG_GUIDE.md`
> - LED矩阵使用: `markdown/LED_MATRIX_USAGE_GUIDE.md`
> - 控制台命令: `markdown/CONSOLE_USAGE_GUIDE.md`
