/**
 * @file web_diagnostics.c
 * @brief Web服务器诊断工具 - 检查文件结构和资源引用
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// 递归列出目录内容
void list_directory_recursive(const char *path, int depth) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char full_path[1024];
    
    // 打开目录
    dir = opendir(path);
    if (dir == NULL) {
        printf("%*s❌ 无法打开目录: %s (%s)\n", depth * 2, "", path, strerror(errno));
        return;
    }
    
    printf("%*s📁 %s/\n", depth * 2, "", path);
    
    // 读取目录内容
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // 构建完整路径
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        // 获取文件信息
        if (stat(full_path, &file_stat) == 0) {
            if (S_ISDIR(file_stat.st_mode)) {
                // 递归处理子目录
                if (depth < 5) {  // 限制递归深度
                    list_directory_recursive(full_path, depth + 1);
                } else {
                    printf("%*s📁 %s/ (递归深度限制)\n", (depth + 1) * 2, "", entry->d_name);
                }
            } else {
                // 显示文件信息
                printf("%*s📄 %s (%ld bytes)\n", (depth + 1) * 2, "", entry->d_name, file_stat.st_size);
            }
        } else {
            printf("%*s❓ %s (无法获取信息: %s)\n", (depth + 1) * 2, "", entry->d_name, strerror(errno));
        }
    }
    
    closedir(dir);
}

// 检查HTML文件中的资源引用
void check_html_resources(const char *html_file_path, const char *web_root) {
    FILE *file = fopen(html_file_path, "r");
    if (!file) {
        printf("❌ 无法打开HTML文件: %s\n", html_file_path);
        return;
    }
    
    printf("\n🔍 检查HTML文件中的资源引用: %s\n", html_file_path);
    
    char line[1024];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_num++;
        
        // 检查常见的资源引用
        char *patterns[] = {
            "href=\"",
            "src=\"",
            "url(",
            "import "
        };
        
        for (int i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
            char *pos = strstr(line, patterns[i]);
            if (pos) {
                // 提取资源路径
                char *start = pos + strlen(patterns[i]);
                char *end = strpbrk(start, "\"')");
                if (end) {
                    int len = end - start;
                    if (len > 0 && len < 512) {
                        char resource[512];
                        strncpy(resource, start, len);
                        resource[len] = '\0';
                        
                        // 跳过外部链接和数据URL
                        if (strstr(resource, "http") == resource || 
                            strstr(resource, "data:") == resource ||
                            strstr(resource, "//") == resource) {
                            continue;
                        }
                        
                        // 构建完整的文件路径
                        char full_resource_path[1024];
                        if (resource[0] == '/') {
                            snprintf(full_resource_path, sizeof(full_resource_path), "%s%s", web_root, resource);
                        } else {
                            snprintf(full_resource_path, sizeof(full_resource_path), "%s/%s", web_root, resource);
                        }
                        
                        // 检查文件是否存在
                        struct stat file_stat;
                        if (stat(full_resource_path, &file_stat) == 0) {
                            printf("  ✅ 第%d行: %s -> %s (%ld bytes)\n", 
                                   line_num, resource, full_resource_path, file_stat.st_size);
                        } else {
                            printf("  ❌ 第%d行: %s -> %s (文件不存在: %s)\n", 
                                   line_num, resource, full_resource_path, strerror(errno));
                        }
                    }
                }
            }
        }
    }
    
    fclose(file);
}

// 主诊断函数
void diagnose_web_files(const char *web_root) {
    printf("=== ESP32S3 Web服务器文件诊断 ===\n\n");
    
    // 1. 检查web根目录
    printf("1. 检查Web根目录: %s\n", web_root);
    DIR *dir = opendir(web_root);
    if (dir) {
        printf("✅ Web根目录可访问\n");
        closedir(dir);
    } else {
        printf("❌ Web根目录不可访问: %s\n", strerror(errno));
        return;
    }
    
    // 2. 递归列出所有文件
    printf("\n2. 文件结构:\n");
    list_directory_recursive(web_root, 0);
    
    // 3. 检查index.html文件
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index.html", web_root);
    
    struct stat index_stat;
    if (stat(index_path, &index_stat) == 0) {
        printf("\n3. index.html文件分析:\n");
        printf("✅ index.html存在 (%ld bytes)\n", index_stat.st_size);
        check_html_resources(index_path, web_root);
    } else {
        printf("\n3. index.html文件分析:\n");
        printf("❌ index.html不存在: %s\n", strerror(errno));
        
        // 查找其他HTML文件
        dir = opendir(web_root);
        if (dir) {
            struct dirent *entry;
            printf("\n📋 查找其他HTML文件:\n");
            while ((entry = readdir(dir)) != NULL) {
                if (strstr(entry->d_name, ".html") || strstr(entry->d_name, ".htm")) {
                    char html_path[1024];
                    snprintf(html_path, sizeof(html_path), "%s/%s", web_root, entry->d_name);
                    printf("  📄 找到: %s\n", entry->d_name);
                    check_html_resources(html_path, web_root);
                }
            }
            closedir(dir);
        }
    }
    
    printf("\n=== 诊断完成 ===\n");
}
