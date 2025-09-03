# ESP32S3 Web服务器修复验证指南

## 🔧 修复内容

### 1. 静态文件路径映射修复
- **问题**: `http://10.10.99.97/index.htm` 返回 "URI not found"
- **修复**: 增强了通配符路由处理，确保所有静态文件请求都能正确映射到 `/sdcard/web/*`

### 2. 多默认页面支持
- **新功能**: 支持多个默认页面文件
- **支持文件**: `index.html`, `index.htm`
- **逻辑**: 按顺序查找存在的默认页面

### 3. 增强的调试日志
- **新增**: URI到文件路径的详细映射日志
- **新增**: 通配符路由注册状态日志
- **新增**: 默认页面查找日志

## 🧪 测试步骤

### 1. 烧录固件并启动Web服务器
```bash
cd /Users/thomas/rm01/rm01-esp32s3-bsp
idf.py flash monitor
```

### 2. 在控制台中启动Web服务器
```
ESP32S3> web start
```

### 3. 使用新的诊断功能
```
ESP32S3> web diagnose
```
这将显示：
- SD卡web目录的完整文件结构
- 每个文件的大小和MIME类型
- HTML文件中的资源引用检查

### 4. 测试各种URL访问

测试这些URL并观察日志输出：

1. **根路径（自动查找默认页面）**
   ```
   http://10.10.99.97/
   ```
   
2. **直接访问默认页面**
   ```
   http://10.10.99.97/index.html
   http://10.10.99.97/index.htm
   ```

3. **其他静态资源**
   ```
   http://10.10.99.97/style.css
   http://10.10.99.97/script.js
   http://10.10.99.97/images/logo.png
   ```

4. **API端点（应该仍然正常工作）**
   ```
   http://10.10.99.97/api/status
   http://10.10.99.97/api/info
   ```

## 📋 日志解读

### 成功的静态文件请求日志示例：
```
I (12345) WEB_SERVER: === Static File Request ===
I (12345) WEB_SERVER: 📡 Requested URI: /index.htm
I (12345) WEB_SERVER: 📁 Document root: /sdcard/web
I (12345) WEB_SERVER: 🏠 Default index: index.html
I (12345) WEB_SERVER: 📁 Directory access check: /sdcard/web ✅
I (12345) WEB_SERVER: 🗂️ URI mapping: '/index.htm' -> '/sdcard/web/index.htm'
I (12345) WEB_SERVER: ✅ File found: /sdcard/web/index.htm, size: 5234 bytes
I (12345) WEB_SERVER: ✅ File opened successfully, sending content...
I (12345) WEB_SERVER: 📄 Content-Type: text/html
I (12345) WEB_SERVER: ✅ File sent successfully: 5234 bytes
```

### 默认页面查找日志示例：
```
I (12345) WEB_SERVER: 📄 Found default page: index.htm
I (12345) WEB_SERVER: 🗂️ URI mapping: '/' -> '/sdcard/web/index.htm'
```

### 文件不存在时的日志示例：
```
W (12345) WEB_SERVER: ❌ File not found: /sdcard/web/missing.css (errno: 2 - No such file or directory)
I (12345) WEB_SERVER: 📁 Listing directory: /sdcard/web
I (12345) WEB_SERVER:   📄 index.htm
I (12345) WEB_SERVER:   📄 style.css
I (12345) WEB_SERVER:   📄 script.js
```

## 🔍 故障排除

### 如果仍然出现 "URI not found"：
1. 检查通配符路由注册日志：
   ```
   I (12345) WEB_SERVER: 📝 Wildcard route registration result: ESP_OK
   ```

2. 运行诊断命令：
   ```
   ESP32S3> web diagnose
   ```

3. 确认文件存在于SD卡的正确位置：
   ```
   /sdcard/web/index.html
   /sdcard/web/index.htm
   ```

### 如果HTML页面加载但资源文件失败：
1. 诊断命令会显示所有缺失的资源文件
2. 检查HTML文件中的资源路径是否正确
3. 确保资源文件存在于SD卡的对应位置

## 📝 预期结果

修复后，您应该能够：
- ✅ 访问 `http://10.10.99.97/` 并自动加载 index.html 或 index.htm
- ✅ 直接访问 `http://10.10.99.97/index.htm` 
- ✅ 访问任何存在于 `/sdcard/web/` 目录中的静态文件
- ✅ 在控制台中看到详细的文件映射和访问日志
- ✅ 使用 `web diagnose` 命令分析文件系统结构和资源引用
