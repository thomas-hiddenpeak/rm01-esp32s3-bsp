# Web服务器问题修复报告

## 🔧 已修复的问题

### 问题1: 配置版本不匹配
```
W (1155) CONFIG_MANAGER: Configuration version mismatch (loaded: 0, expected: 1), using defaults
W (1165) ESP32S3_MAIN: 配置加载失败，使用默认配置: ESP_ERR_INVALID_VERSION
```

**原因**: 首次运行或配置结构更新时，NVS中保存的配置版本号不匹配，导致使用默认配置但未保存到NVS。

**修复方案**: 
- 在 `main.c` 中添加了自动保存逻辑
- 当配置加载失败时，重置为默认配置并立即保存到NVS
- 确保下次启动时能正确加载配置

**修改文件**: `/main/main.c`

### 问题2: 根路径404错误
```
W (48215) httpd_uri: httpd_uri: URI '/' not found
W (48215) httpd_txrx: httpd_resp_send_err: 404 Not Found - Nothing matches the given URI
```

**原因**: ESP-IDF HTTP服务器中，通配符 `/*` 的匹配规则可能不包括根路径 `/`，需要显式注册。

**修复方案**:
- 重新排序URI注册顺序，先注册API端点
- 显式注册根路径 `/` 
- 保留通配符 `/*` 用于其他静态文件

**修改文件**: `/components/web_server/web_server.c`

## 🚀 测试步骤

### 步骤1: 烧录新固件
```bash
cd /Users/thomas/rm01/rm01-esp32s3-bsp
idf.py flash monitor
```

### 步骤2: 验证配置修复
启动后应该看到：
```
I (xxx) ESP32S3_MAIN: 配置加载成功
```
或者如果是首次运行：
```
I (xxx) ESP32S3_MAIN: 未找到保存的配置，使用默认配置
I (xxx) ESP32S3_MAIN: 默认配置已保存到NVS
```

### 步骤3: 准备测试文件
将测试HTML文件复制到SD卡：
```bash
# 在SD卡上创建目录结构
mkdir -p /sdcard/web
# 复制测试文件
cp /Users/thomas/rm01/rm01-esp32s3-bsp/test_web_files/index.html /sdcard/web/
```

### 步骤4: 启动web服务器
```bash
ESP32S3> web start
✅ Web服务已启动，访问地址: http://10.10.99.97/
```

### 步骤5: 测试访问
在浏览器中访问：
- `http://10.10.99.97/` - 应该显示测试页面
- `http://10.10.99.97/api/status` - 应该返回JSON状态信息
- `http://10.10.99.97/api/info` - 应该返回设备信息

## 🔧 核心修改

### 1. main.c - 配置保存修复
```c
// 配置加载失败时自动保存默认配置
if (ret != ESP_OK) {
    ESP_LOGW(TAG, "配置加载失败，使用默认配置: %s", esp_err_to_name(ret));
    config_manager_reset_to_defaults();
    // 保存默认配置到NVS
    esp_err_t save_ret = config_manager_save();
    if (save_ret == ESP_OK) {
        ESP_LOGI(TAG, "默认配置已保存到NVS");
    }
}
```

### 2. web_server.c - URI注册修复
```c
// 先注册API端点
httpd_register_uri_handler(server, &api_status_uri);
httpd_register_uri_handler(server, &api_info_uri);

// 显式注册根路径
httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = static_file_handler,
    .user_ctx = NULL
};
httpd_register_uri_handler(server, &root_uri);

// 最后注册通配符
httpd_register_uri_handler(server, &static_uri);
```

## 🎯 默认配置

系统现在使用以下默认web服务器配置：
```c
.document_root = "/sdcard/web"
.port = 80
.auto_start = false
.enable_cors = true
.default_index = "index.html"
```

## 📝 使用说明

1. **启动服务器**: `web start`
2. **查看状态**: `web status`
3. **查看配置**: `web config`
4. **启用自动启动**: 通过config命令修改配置并保存
5. **重置配置**: `defaults apply`

## ✅ 验证清单

- [ ] 系统启动无配置版本错误
- [ ] 根路径 `/` 可正常访问
- [ ] API端点正常响应
- [ ] 静态文件正常服务
- [ ] 配置持久化正常
- [ ] 控制台命令正常工作

修复完成！现在web服务器应该能够正常工作。
