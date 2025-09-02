/**
 * @file web_server.h
 * @brief Web Server Component for ESP32S3
 * 
 * This component provides HTTP web server functionality for serving static files
 * and handling REST API requests.
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Web server status enumeration
 */
typedef enum {
    WEB_SERVER_STATUS_STOPPED = 0,     /**< Server is stopped */
    WEB_SERVER_STATUS_STARTING,        /**< Server is starting */
    WEB_SERVER_STATUS_RUNNING,         /**< Server is running */
    WEB_SERVER_STATUS_ERROR            /**< Server encountered an error */
} web_server_status_t;

/**
 * @brief Web server statistics structure
 */
typedef struct {
    uint32_t total_requests;    /**< Total number of requests handled */
    uint32_t active_sessions;   /**< Number of active sessions */
    uint32_t uptime_seconds;    /**< Server uptime in seconds */
    uint64_t bytes_sent;        /**< Total bytes sent */
    uint64_t bytes_received;    /**< Total bytes received */
    uint32_t error_count;       /**< Number of errors encountered */
} web_server_stats_t;

/**
 * @brief Start the web server
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the web server
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t web_server_stop(void);

/**
 * @brief Get web server status
 * 
 * @return web_server_status_t Current server status
 */
web_server_status_t web_server_get_status(void);

/**
 * @brief Get web server statistics
 * 
 * @param stats Pointer to statistics structure to fill
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t web_server_get_stats(web_server_stats_t *stats);

/**
 * @brief Reset web server statistics
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t web_server_reset_stats(void);

/**
 * @brief 诊断Web文件系统 - 检查文件结构和资源引用
 * 
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t web_server_diagnose_files(void);

#ifdef __cplusplus
}
#endif

#endif // WEB_SERVER_H
