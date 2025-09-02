/**
 * @file web_server.c
 * @brief Web Server Component Implementation for ESP32S3
 * 
 * Based on reference implementation from thomas-hiddenpeak/rm01-bsp
 */

#include "web_server.h"
#include "config_manager.h"
#include "sdcard_interface.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "WEB_SERVER";

// Global variables
static httpd_handle_t server = NULL;
static web_server_status_t server_status = WEB_SERVER_STATUS_STOPPED;
static web_server_stats_t server_stats = {0};
static int64_t server_start_time = 0;

// Configuration
#define WEB_SERVER_PORT 80
#define WEB_SERVER_MAX_OPEN_SOCKETS 7
#define WEB_SERVER_STACK_SIZE 8192

// MIME type mappings
typedef struct {
    const char *ext;
    const char *type;
} mime_type_t;

static const mime_type_t mime_types[] = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".txt", "text/plain"},
    {".xml", "text/xml"},
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {NULL, NULL}
};

// Function declarations
static const char* get_mime_type(const char* path);
static esp_err_t static_file_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_info_handler(httpd_req_t *req);

/**
 * @brief Get MIME type for file extension
 */
static const char* get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext) {
        for (int i = 0; mime_types[i].ext; i++) {
            if (strcasecmp(ext, mime_types[i].ext) == 0) {
                return mime_types[i].type;
            }
        }
    }
    return "application/octet-stream";
}

/**
 * @brief 检查并返回存在的默认页面路径
 */
static bool find_default_page(const char* document_root, char* file_path, size_t path_size) {
    const char* default_pages[] = {"index.html", "index.htm", NULL};
    struct stat file_stat;
    
    for (int i = 0; default_pages[i]; i++) {
        int ret = snprintf(file_path, path_size, "%s/%s", document_root, default_pages[i]);
        if (ret < path_size && stat(file_path, &file_stat) == 0) {
            ESP_LOGI(TAG, "📄 Found default page: %s", default_pages[i]);
            return true;
        }
    }
    return false;
}

/**
 * @brief Static file handler
 */
static esp_err_t static_file_handler(httpd_req_t *req) {
    char file_path[1024];  // Increased buffer size again
    const web_server_config_t* web_config = config_manager_get_web_server_config();
    
    // Update statistics
    server_stats.total_requests++;
    
    // Debug: Print configuration and request info
    ESP_LOGI(TAG, "=== Static File Request ===");
    ESP_LOGI(TAG, "📡 Requested URI: %s", req->uri);
    ESP_LOGI(TAG, "📁 Document root: %s", web_config->document_root);
    ESP_LOGI(TAG, "🏠 Default index: %s", web_config->default_index);
    
    // Check if SD card is mounted by testing directory access
    DIR *dir = opendir(web_config->document_root);
    if (dir) {
        ESP_LOGI(TAG, "📁 Directory access check: %s ✅", web_config->document_root);
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "❌ Directory not accessible: %s (errno: %d - %s)", 
                 web_config->document_root, errno, strerror(errno));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Get the requested path with length checking
    if (strcmp(req->uri, "/") == 0) {
        // Root path - try to find a default page
        ESP_LOGI(TAG, "🏠 Root path request - looking for default page");
        if (!find_default_page(web_config->document_root, file_path, sizeof(file_path))) {
            // If no default page found, use configured default
            int ret = snprintf(file_path, sizeof(file_path), "%s/%s", web_config->document_root, web_config->default_index);
            if (ret >= sizeof(file_path)) {
                ESP_LOGE(TAG, "File path too long");
                httpd_resp_send_500(req);
                return ESP_FAIL;
            }
        }
    } else {
        // Non-root path - map directly to SD card
        ESP_LOGI(TAG, "📄 Static file request: %s", req->uri);
        int ret = snprintf(file_path, sizeof(file_path), "%s%s", web_config->document_root, req->uri);
        if (ret >= sizeof(file_path)) {
            ESP_LOGE(TAG, "File path too long");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
    }
    
    ESP_LOGI(TAG, "🗂️ URI mapping: '%s' -> '%s'", req->uri, file_path);
    
    // Check if file exists
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        ESP_LOGW(TAG, "❌ File not found: %s (errno: %d - %s)", 
                 file_path, errno, strerror(errno));
        
        // List directory contents for debugging
        char dir_path[1024];
        char *last_slash = strrchr(file_path, '/');
        if (last_slash) {
            size_t dir_len = last_slash - file_path;
            strncpy(dir_path, file_path, dir_len);
            dir_path[dir_len] = '\0';
            
            ESP_LOGI(TAG, "📁 Listing directory: %s", dir_path);
            DIR *dir = opendir(dir_path);
            if (dir) {
                struct dirent *entry;
                int file_count = 0;
                while ((entry = readdir(dir)) != NULL && file_count < 10) {
                    ESP_LOGI(TAG, "  📄 %s", entry->d_name);
                    file_count++;
                }
                closedir(dir);
                if (file_count == 0) {
                    ESP_LOGW(TAG, "  (Directory is empty)");
                }
            } else {
                ESP_LOGE(TAG, "  Cannot read directory: %s", strerror(errno));
            }
        }
        
        httpd_resp_send_404(req);
        server_stats.error_count++;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ File found: %s, size: %ld bytes", file_path, file_stat.st_size);
    
    // Open file
    FILE *file = fopen(file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ Failed to open file: %s (errno: %d - %s)", 
                 file_path, errno, strerror(errno));
        httpd_resp_send_500(req);
        server_stats.error_count++;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ File opened successfully, sending content...");
    
    // Set content type
    const char* mime_type = get_mime_type(file_path);
    httpd_resp_set_type(req, mime_type);
    ESP_LOGI(TAG, "📄 Content-Type: %s", mime_type);
    
    // Send file content
    char buffer[1024];
    size_t bytes_read;
    size_t total_sent = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, bytes_read) != ESP_OK) {
            ESP_LOGE(TAG, "❌ Failed to send file chunk");
            server_stats.error_count++;
            break;
        }
        server_stats.bytes_sent += bytes_read;
        total_sent += bytes_read;
    }
    
    // Finalize response
    httpd_resp_send_chunk(req, NULL, 0);
    
    fclose(file);
    ESP_LOGI(TAG, "✅ File sent successfully: %zu bytes", total_sent);
    return ESP_OK;
}

/**
 * @brief API status endpoint
 */
static esp_err_t api_status_handler(httpd_req_t *req) {
    server_stats.total_requests++;
    
    cJSON *json = cJSON_CreateObject();
    cJSON *status = cJSON_CreateString((server_status == WEB_SERVER_STATUS_RUNNING) ? "running" : "stopped");
    cJSON *uptime = cJSON_CreateNumber((esp_timer_get_time() - server_start_time) / 1000000);
    cJSON *requests = cJSON_CreateNumber(server_stats.total_requests);
    
    cJSON_AddItemToObject(json, "status", status);
    cJSON_AddItemToObject(json, "uptime_seconds", uptime);
    cJSON_AddItemToObject(json, "total_requests", requests);
    
    char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    server_stats.bytes_sent += strlen(json_string);
    
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/**
 * @brief API info endpoint
 */
static esp_err_t api_info_handler(httpd_req_t *req) {
    server_stats.total_requests++;
    
    cJSON *json = cJSON_CreateObject();
    cJSON *device = cJSON_CreateString("ESP32S3-BSP");
    cJSON *version = cJSON_CreateString("1.0.0");
    cJSON *heap = cJSON_CreateNumber(esp_get_free_heap_size());
    
    cJSON_AddItemToObject(json, "device", device);
    cJSON_AddItemToObject(json, "version", version);
    cJSON_AddItemToObject(json, "free_heap", heap);
    
    char *json_string = cJSON_Print(json);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));
    
    server_stats.bytes_sent += strlen(json_string);
    
    free(json_string);
    cJSON_Delete(json);
    
    return ESP_OK;
}

/**
 * @brief Start the web server
 */
esp_err_t web_server_start(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }
    
    server_status = WEB_SERVER_STATUS_STARTING;
    
    // Get configuration
    const web_server_config_t* web_config = config_manager_get_web_server_config();
    
    ESP_LOGI(TAG, "Starting web server on port %d, serving from: %s", 
             web_config->port, web_config->document_root);
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = web_config->port;
    config.max_open_sockets = WEB_SERVER_MAX_OPEN_SOCKETS;
    config.stack_size = WEB_SERVER_STACK_SIZE;
    config.task_priority = 5;
    
    // CRITICAL: Set error handler to catch all unmatched URIs
    config.uri_match_fn = httpd_uri_match_wildcard;
    
    // Start the server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        server_status = WEB_SERVER_STATUS_ERROR;
        return ret;
    }
    
    // Register URI handlers
    // Register API endpoints first (specific paths)
    httpd_uri_t api_status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL
    };
    esp_err_t api_status_result = httpd_register_uri_handler(server, &api_status_uri);
    ESP_LOGI(TAG, "📝 API status route registration: %s", esp_err_to_name(api_status_result));
    
    httpd_uri_t api_info_uri = {
        .uri = "/api/info",
        .method = HTTP_GET,
        .handler = api_info_handler,
        .user_ctx = NULL
    };
    esp_err_t api_info_result = httpd_register_uri_handler(server, &api_info_uri);
    ESP_LOGI(TAG, "📝 API info route registration: %s", esp_err_to_name(api_info_result));
    
    // Register root explicitly
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    };
    esp_err_t root_result = httpd_register_uri_handler(server, &root_uri);
    ESP_LOGI(TAG, "📝 Root route '/' registration: %s", esp_err_to_name(root_result));
    
    // Register wildcard route for all static files (with wildcard matching enabled)
    httpd_uri_t static_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = static_file_handler,
        .user_ctx = NULL
    };
    esp_err_t static_result = httpd_register_uri_handler(server, &static_uri);
    ESP_LOGI(TAG, "📝 Wildcard route '/*' registration: %s", esp_err_to_name(static_result));
    
    // Reset statistics
    memset(&server_stats, 0, sizeof(server_stats));
    server_start_time = esp_timer_get_time();
    
    server_status = WEB_SERVER_STATUS_RUNNING;
    ESP_LOGI(TAG, "Web server started successfully");
    
    return ESP_OK;
}

/**
 * @brief Stop the web server
 */
esp_err_t web_server_stop(void) {
    if (server == NULL) {
        ESP_LOGW(TAG, "Web server not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping web server");
    
    esp_err_t ret = httpd_stop(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    server = NULL;
    server_status = WEB_SERVER_STATUS_STOPPED;
    
    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

/**
 * @brief Get web server status
 */
web_server_status_t web_server_get_status(void) {
    return server_status;
}

/**
 * @brief Get web server statistics
 */
esp_err_t web_server_get_stats(web_server_stats_t *stats) {
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Update uptime
    if (server_status == WEB_SERVER_STATUS_RUNNING && server_start_time > 0) {
        server_stats.uptime_seconds = (esp_timer_get_time() - server_start_time) / 1000000;
    }
    
    // Copy current statistics
    memcpy(stats, &server_stats, sizeof(web_server_stats_t));
    
    return ESP_OK;
}

/**
 * @brief Reset web server statistics
 */
esp_err_t web_server_reset_stats(void) {
    memset(&server_stats, 0, sizeof(server_stats));
    server_start_time = esp_timer_get_time();
    
    ESP_LOGI(TAG, "Web server statistics reset");
    return ESP_OK;
}

/**
 * @brief 递归列出目录内容的辅助函数
 */
static void list_directory_recursive(const char *path, int depth) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    
    if (depth > 3) return; // 限制递归深度
    
    dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "%*s❌ 无法打开目录: %s (%s)", depth * 2, "", path, strerror(errno));
        return;
    }
    
    ESP_LOGI(TAG, "%*s📁 %s/", depth * 2, "", path);
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        if (stat(full_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                list_directory_recursive(full_path, depth + 1);
            } else {
                const char* mime = get_mime_type(entry->d_name);
                ESP_LOGI(TAG, "%*s📄 %s (%ld bytes, %s)", (depth + 1) * 2, "", 
                         entry->d_name, file_stat.st_size, mime);
            }
        } else {
            ESP_LOGW(TAG, "%*s❓ %s (无法获取信息: %s)", (depth + 1) * 2, "", 
                     entry->d_name, strerror(errno));
        }
    }
    
    closedir(dir);
}

/**
 * @brief 检查HTML文件中的资源引用
 */
static void check_html_resources(const char *html_file_path, const char *web_root) {
    FILE *file = fopen(html_file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "❌ 无法打开HTML文件: %s", html_file_path);
        return;
    }
    
    ESP_LOGI(TAG, "🔍 检查HTML文件中的资源引用: %s", html_file_path);
    
    char line[512];
    int line_num = 0;
    int found_resources = 0;
    
    while (fgets(line, sizeof(line), file) && line_num < 100) { // 限制检查行数
        line_num++;
        
        // 检查常见的资源引用
        char *patterns[] = {"href=\"", "src=\"", "url("};
        
        for (int i = 0; i < 3; i++) {
            char *pos = strstr(line, patterns[i]);
            if (pos) {
                char *start = pos + strlen(patterns[i]);
                char *end = strpbrk(start, "\"')");
                if (end) {
                    int len = end - start;
                    if (len > 0 && len < 256) {
                        char resource[256];
                        strncpy(resource, start, len);
                        resource[len] = '\0';
                        
                        // 跳过外部链接和数据URL
                        if (strstr(resource, "http") == resource || 
                            strstr(resource, "data:") == resource ||
                            strstr(resource, "//") == resource) {
                            continue;
                        }
                        
                        // 构建完整的文件路径
                        char full_resource_path[512];
                        if (resource[0] == '/') {
                            snprintf(full_resource_path, sizeof(full_resource_path), "%s%s", web_root, resource);
                        } else {
                            snprintf(full_resource_path, sizeof(full_resource_path), "%s/%s", web_root, resource);
                        }
                        
                        // 检查文件是否存在
                        struct stat file_stat;
                        if (stat(full_resource_path, &file_stat) == 0) {
                            ESP_LOGI(TAG, "  ✅ 第%d行: %s -> 存在 (%ld bytes)", 
                                     line_num, resource, file_stat.st_size);
                        } else {
                            ESP_LOGW(TAG, "  ❌ 第%d行: %s -> 不存在 (%s)", 
                                     line_num, resource, strerror(errno));
                        }
                        found_resources++;
                    }
                }
            }
        }
    }
    
    if (found_resources == 0) {
        ESP_LOGI(TAG, "  📝 未发现外部资源引用 (可能使用内联样式/脚本)");
    }
    
    fclose(file);
}

/**
 * @brief 诊断Web文件系统
 */
esp_err_t web_server_diagnose_files(void) {
    const web_server_config_t* web_config = config_manager_get_web_server_config();
    
    ESP_LOGI(TAG, "=== ESP32S3 Web服务器文件诊断 ===");
    
    // 1. 检查web根目录
    ESP_LOGI(TAG, "1. 检查Web根目录: %s", web_config->document_root);
    DIR *dir = opendir(web_config->document_root);
    if (dir) {
        ESP_LOGI(TAG, "✅ Web根目录可访问");
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "❌ Web根目录不可访问: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    // 2. 递归列出所有文件
    ESP_LOGI(TAG, "2. 文件结构:");
    list_directory_recursive(web_config->document_root, 0);
    
    // 3. 检查index.html文件
    char index_path[512];
    snprintf(index_path, sizeof(index_path), "%s/%s", web_config->document_root, web_config->default_index);
    
    struct stat index_stat;
    ESP_LOGI(TAG, "3. 检查默认首页: %s", web_config->default_index);
    if (stat(index_path, &index_stat) == 0) {
        ESP_LOGI(TAG, "✅ 默认首页存在 (%ld bytes)", index_stat.st_size);
        check_html_resources(index_path, web_config->document_root);
    } else {
        ESP_LOGW(TAG, "❌ 默认首页不存在: %s", strerror(errno));
        
        // 查找其他HTML文件
        dir = opendir(web_config->document_root);
        if (dir) {
            struct dirent *entry;
            ESP_LOGI(TAG, "📋 查找其他HTML文件:");
            while ((entry = readdir(dir)) != NULL) {
                if (strstr(entry->d_name, ".html") || strstr(entry->d_name, ".htm")) {
                    char html_path[512];
                    snprintf(html_path, sizeof(html_path), "%s/%s", web_config->document_root, entry->d_name);
                    ESP_LOGI(TAG, "  📄 找到HTML文件: %s", entry->d_name);
                    check_html_resources(html_path, web_config->document_root);
                    break; // 只检查第一个找到的HTML文件
                }
            }
            closedir(dir);
        }
    }
    
    ESP_LOGI(TAG, "=== 诊断完成 ===");
    return ESP_OK;
}
