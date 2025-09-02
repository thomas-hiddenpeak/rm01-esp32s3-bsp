# Web服务器组件使用指南

## 概述

Web服务器组件为ESP32S3提供HTTP web服务功能，可以从SD卡的指定目录提供静态文件服务，并提供配置和状态的REST API接口。

## 主要特性

- **静态文件服务**: 从SD卡提供HTML、CSS、JS、图片等静态文件
- **REST API**: 提供系统状态、配置和统计信息的JSON API
- **NVS配置保存**: 配置参数自动保存到非易失存储
- **自动启动**: 支持系统启动时自动启动web服务
- **CORS支持**: 支持跨域资源共享
- **内容类型检测**: 自动识别文件类型并设置正确的Content-Type

## 配置参数

### 默认配置
```c
web_server_config_t config = {
    .document_root = "/sdcard/web",        // 文档根目录
    .port = 80,                           // HTTP端口
    .auto_start = false,                  // 自动启动（默认禁用）
    .enable_cors = true,                  // CORS支持
    .default_index = "index.html"         // 默认首页文件
};
```

## 控制台命令

### 基本操作
```bash
# 启动web服务
web start

# 停止web服务  
web stop

# 重启web服务
web restart

# 查看服务状态
web status

# 查看详细配置
web config

# 查看服务统计
web stats

# 重置统计信息
web reset-stats
```

### 配置管理
```bash
# 设置文档根目录
web set root /sdcard/web

# 设置端口号
web set port 8080

# 启用自动启动
web set autostart on

# 禁用自动启动  
web set autostart off

# 保存配置到NVS
web save

# 从NVS加载配置
web load

# 检查文档根目录是否可访问
web check
```

### 通过config命令配置
```bash
# 设置web服务参数（自动保存到NVS）
config set web /sdcard/web 80 true

# 应用web服务默认参数
defaults web
```

## REST API 接口

### 获取服务器状态
```
GET /api/status
```
返回示例：
```json
{
  "status": "running",
  "port": 80,
  "document_root": "/sdcard/web", 
  "auto_start": true,
  "uptime_seconds": 3600
}
```

### 获取服务器配置
```
GET /api/config
```
返回示例：
```json
{
  "document_root": "/sdcard/web",
  "port": 80,
  "auto_start": true,
  "max_uri_handlers": 16,
  "enable_cors": true,
  "default_index": "index.html"
}
```

### 获取服务器统计
```
GET /api/stats
```
返回示例：
```json
{
  "total_requests": 150,
  "active_sessions": 2,
  "uptime_seconds": 3600,
  "bytes_sent": 1048576,
  "bytes_received": 2048,
  "error_count": 5
}
```

## SD卡目录结构

推荐的SD卡web目录结构：
```
/sdcard/
├── web/                    # web服务根目录
│   ├── index.html         # 默认首页
│   ├── css/
│   │   └── style.css      # 样式文件
│   ├── js/
│   │   └── app.js         # JavaScript文件
│   ├── images/
│   │   ├── logo.png       # 图片文件
│   │   └── favicon.ico    # 网站图标
│   └── api/               # 可选：自定义API文件
└── config/                 # 其他配置文件
```

## 使用步骤

### 1. 准备SD卡内容
将网页文件复制到SD卡的`/web`目录中。项目提供了示例页面：
```bash
# 复制示例网页到SD卡
cp sdcard_config_examples/web/* /path/to/sdcard/web/
```

### 2. 插入SD卡并挂载
```bash
# 挂载SD卡
sdcard mount

# 检查web目录
ls /sdcard/web
```

### 3. 配置web服务器
```bash
# 设置配置参数
config set web /sdcard/web 80 true

# 或使用web命令
web set root /sdcard/web
web set port 80
web set autostart on
web save
```

### 4. 启动web服务
```bash
# 手动启动
web start

# 或启用自动启动后重启系统
```

### 5. 访问网页
在浏览器中访问：`http://10.10.99.97/`

## 故障排除

### 网页无法访问
1. 检查网络连接：`ping 10.10.99.97`
2. 检查web服务状态：`web status`
3. 检查端口设置：`web config`
4. 检查文档根目录：`web check`

### SD卡相关问题
1. 检查SD卡挂载：`sdcard info`
2. 检查文件权限：`ls -la /sdcard/web`
3. 重新挂载SD卡：`sdcard unmount && sdcard mount`

### 配置问题
1. 查看当前配置：`web config`
2. 重置为默认配置：`defaults web`
3. 检查NVS保存状态：`web load`

## 示例网页

项目包含一个功能完整的示例网页（`sdcard_config_examples/web/index.html`），提供：

- 实时系统状态显示
- REST API数据展示  
- 设备控制模拟界面
- 响应式设计，支持移动设备
- 自动刷新功能

## 开发建议

### 自定义网页开发
1. 使用示例页面作为模板
2. 利用提供的REST API获取系统信息
3. 保持文件结构清晰
4. 注意移动设备兼容性

### 性能优化
1. 压缩CSS和JavaScript文件
2. 优化图片大小和格式
3. 使用浏览器缓存
4. 避免过大的文件

### 安全考虑
1. 不要在网页中包含敏感信息
2. 使用HTTPS（需要额外配置）
3. 限制访问IP范围（可通过网络配置实现）

## 技术细节

- **HTTP服务器**: 基于ESP-IDF的httpd组件
- **文件系统**: 支持FAT32格式的SD卡
- **内存管理**: 使用流式传输处理大文件
- **并发处理**: 支持多个并发连接
- **错误处理**: 完整的HTTP状态码支持

## 更新日志

- v1.0.0: 初始版本，支持基本的静态文件服务和REST API
