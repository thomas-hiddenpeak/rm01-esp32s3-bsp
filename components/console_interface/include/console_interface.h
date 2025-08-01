/**
 * @file console_interface.h
 * @brief ESP32S3 Console Interface Component
 * 
 * This component provides a comprehensive console interface for ESP32S3 systems,
 * including command registration, input handling, and device control commands.
 */

#ifndef CONSOLE_INTERFACE_H
#define CONSOLE_INTERFACE_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_console.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONSOLE_BUF_SIZE 256
#define CONSOLE_MAX_CMDLINE_ARGS 32
#define CONSOLE_HISTORY_LEN 100

/**
 * @brief Console interface configuration structure
 */
typedef struct {
    uint16_t max_cmdline_length;    ///< Maximum command line length
    uint8_t max_cmdline_args;       ///< Maximum number of command line arguments
    uint16_t history_length;        ///< Command history length
    bool enable_color_hints;        ///< Enable colored hints
    bool enable_multiline;          ///< Enable multiline input
    const char *prompt;             ///< Console prompt string
} console_interface_config_t;

/**
 * @brief Console event types
 */
typedef enum {
    CONSOLE_EVENT_READY,           ///< Console is ready for input
    CONSOLE_EVENT_COMMAND_SUCCESS, ///< Command executed successfully
    CONSOLE_EVENT_COMMAND_ERROR,   ///< Command execution error
    CONSOLE_EVENT_SHUTDOWN         ///< Console is shutting down
} console_event_t;

/**
 * @brief Console event callback function type
 */
typedef void (*console_event_callback_t)(console_event_t event, const char *data);

/**
 * @brief Default console configuration
 */
#define CONSOLE_INTERFACE_DEFAULT_CONFIG() { \
    .max_cmdline_length = CONSOLE_BUF_SIZE, \
    .max_cmdline_args = CONSOLE_MAX_CMDLINE_ARGS, \
    .history_length = CONSOLE_HISTORY_LEN, \
    .enable_color_hints = false, \
    .enable_multiline = true, \
    .prompt = "ESP32S3> " \
}

/**
 * @brief Initialize the console interface
 * 
 * @param config Console configuration structure
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_init(const console_interface_config_t *config);

/**
 * @brief Start the console task
 * 
 * @param stack_size Task stack size in bytes
 * @param priority Task priority
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_start(uint32_t stack_size, uint8_t priority);

/**
 * @brief Stop the console task
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_stop(void);

/**
 * @brief Register a console event callback
 * 
 * @param callback Callback function to register
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_register_event_callback(console_event_callback_t callback);

/**
 * @brief Register all device control commands
 * 
 * This function registers all the device-specific commands like fan, led, gpio, etc.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_register_device_commands(void);

/**
 * @brief Register basic system commands
 * 
 * This function registers basic system commands like help, info, status, reboot.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_register_system_commands(void);

/**
 * @brief Register configuration commands
 * 
 * This function registers configuration management commands like save, load, clear.
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_register_config_commands(void);

/**
 * @brief Execute a console command programmatically
 * 
 * @param command Command string to execute
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_execute_command(const char *command);

/**
 * @brief Print a message to the console
 * 
 * @param format Printf-style format string
 * @param ... Arguments for format string
 */
void console_interface_print(const char *format, ...);

/**
 * @brief Print the console prompt
 */
void console_interface_print_prompt(void);

/**
 * @brief Show startup banner and help
 */
void console_interface_show_banner(void);

/**
 * @brief Check if console is ready for input
 * 
 * @return true if console is ready, false otherwise
 */
bool console_interface_is_ready(void);

/**
 * @brief Get console statistics
 * 
 * @param commands_executed Pointer to store number of commands executed
 * @param uptime_ms Pointer to store console uptime in milliseconds
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t console_interface_get_stats(uint32_t *commands_executed, uint64_t *uptime_ms);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_INTERFACE_H
